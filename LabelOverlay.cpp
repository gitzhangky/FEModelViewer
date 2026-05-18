#include "LabelOverlay.h"

#include "GLWidget.h"
#include "LabelLayout.h"
#include "PickRenderer.h"

#include <QFontMetrics>
#include <QPainter>
#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

LabelOverlay::LabelOverlay(GLWidget& w) : w_(w) {}

void LabelOverlay::setAxesMvp(const glm::mat4& mvp) {
    axesMVP_ = mvp;
}

void LabelOverlay::render(QPainter& painter, const glm::mat4& sceneMvp) {
    drawAxesLabels(painter);
    if (w_.showLabels_ && w_.selection_.hasSelection()) {
        drawIdLabels(painter, sceneMvp);
    } else {
        lastLabelMode_ = -1;
        lastLabelIds_.clear();
    }
}

void LabelOverlay::drawAxesLabels(QPainter& painter) {
    const int axesSize = 120;
    const int margin = 8;

    auto project = [&](glm::vec3 pt) -> QPointF {
        glm::vec4 clip = axesMVP_ * glm::vec4(pt, 1.0f);
        float sx = margin + (clip.x / clip.w * 0.5f + 0.5f) * axesSize;
        float sy = w_.height() - margin - (clip.y / clip.w * 0.5f + 0.5f) * axesSize;
        return QPointF(sx, sy);
    };

    struct AxisLabel { glm::vec3 dir; QString name; QColor color; };
    AxisLabel labels[] = {
        {{1,0,0}, "X", QColor(240, 80, 80)},
        {{0,1,0}, "Y", QColor(90, 220, 90)},
        {{0,0,1}, "Z", QColor(90, 140, 255)},
    };

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(14);
    painter.setFont(font);

    for (const auto& l : labels) {
        QPointF pos = project(l.dir * 1.15f);
        painter.setPen(l.color);
        painter.drawText(QRectF(pos.x() - 12, pos.y() - 12, 24, 24),
                         Qt::AlignCenter, l.name);
    }
}

void LabelOverlay::drawIdLabels(QPainter& painter, const glm::mat4& mvp) {
    int w = w_.width();
    int h = w_.height();

    // 世界坐标 → 屏幕坐标
    auto project = [&](const glm::vec3& pos) -> QPointF {
        glm::vec4 clip = mvp * glm::vec4(pos, 1.0f);
        if (clip.w <= 0.0f) return QPointF(-1, -1);
        float nx = clip.x / clip.w;
        float ny = clip.y / clip.w;
        float sx = (nx * 0.5f + 0.5f) * w;
        float sy = (1.0f - (ny * 0.5f + 0.5f)) * h;
        return QPointF(sx, sy);
    };

    QFont font = painter.font();
    font.setPixelSize(11);
    font.setBold(true);
    painter.setFont(font);

    // 描边文字：深色轮廓 + 亮色正文（避免 drawRect 导致 GL 状态崩溃）
    QColor outlineColor(0, 0, 0, 220);
    QColor textColor(255, 200, 0);
    const int offsetY = -14;  // 标签偏移到实体上方

    // 绘制带描边的文字（4方向偏移描边 + 正文叠加）
    auto drawOutlinedText = [&](int x, int y, const QString& text) {
        painter.setPen(outlineColor);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx != 0 || dy != 0)
                    painter.drawText(x + dx, y + dy, text);
        painter.setPen(textColor);
        painter.drawText(x, y, text);
    };

    QFontMetrics fm(font);

    // 屏幕剔除 + bin 去重：大模型选中几十万节点时，全画每帧 9*N 次 drawText
    // 会爆性能。先用 bin (≈一个字号高的格子) 抑制重叠，每格只画第一个；
    // 完全在屏幕外的标签直接跳过。
    const int binPx = std::max(16, fm.height());
    auto& usedBins = labelBinScratch_;
    usedBins.clear();
    const int wPx = w_.width(), hPx = w_.height();
    const int maxBinCols = std::max(1, (wPx + 100 + binPx - 1) / binPx);
    const int maxBinRows = std::max(1, (hPx + 100 + binPx - 1) / binPx);
    const size_t maxReservedBins = static_cast<size_t>(maxBinCols * maxBinRows);
    auto reserveBin = [&](const QPointF& sp) -> bool {
        if (sp.x() < -50.0 || sp.x() > wPx + 50.0 ||
            sp.y() < -50.0 || sp.y() > hPx + 50.0)
            return false;   // 屏外
        int bx = static_cast<int>(sp.x()) / binPx;
        int by = static_cast<int>(sp.y()) / binPx;
        long long key = (static_cast<long long>(bx) << 32) ^ static_cast<unsigned int>(by);
        if (!usedBins.insert(key).second) return false;   // 同格已占用
        return true;
    };

    auto isTriangleVisible = [&](int triIndex) -> bool {
        return w_.isTriangleVisible(triIndex);
    };

    int modeKey = static_cast<int>(w_.pickMode_);
    const std::vector<int> previousLabelIds =
        (lastLabelMode_ == modeKey) ? lastLabelIds_ : std::vector<int>{};
    std::vector<int> drawnLabelIds;
    bool placementUpdated = false;

    auto rememberPlacedLabels = [&]() {
        lastLabelMode_ = modeKey;
        lastLabelIds_ = drawnLabelIds;
        placementUpdated = true;
    };

    if (w_.pickMode_ == PickMode::Node) {
        // ── 节点标签 ──
        if (!w_.selection_.selectedNodes.empty() && !w_.nodeToFirstVertex_.empty()) {
            std::unordered_set<int> visibleNodes;
            bool hasHiddenPart = false;
            for (const auto& entry : w_.partVisibility_) {
                if (!entry.second) {
                    hasHiddenPart = true;
                    break;
                }
            }
            // 正常路径由 isNodeVisible() 通过 nodeToElems_ → elemToPart_ 判断。
            // 只有缺少邻接表时才退回到旧的可见三角扫描。
            bool filterNodesByVisibility = hasHiddenPart && !w_.triToPart_.empty()
                                       && w_.nodeToElems_.empty();
            if (filterNodesByVisibility) {
                int triCount = std::min(static_cast<int>(w_.triToPart_.size()),
                                        static_cast<int>(w_.mesh_.indices.size() / 3));
                for (int t = 0; t < triCount; ++t) {
                    if (!isTriangleVisible(t)) continue;
                    for (int k = 0; k < 3; ++k) {
                        unsigned int vi = w_.mesh_.indices[t * 3 + k];
                        if (vi < w_.vertexToNode_.size()) {
                            int nid = w_.vertexToNode_[vi];
                            if (nid >= 0) visibleNodes.insert(nid);
                        }
                    }
                }
            }

            std::vector<int> nodeIds(w_.selection_.selectedNodes.begin(),
                                     w_.selection_.selectedNodes.end());
            auto orderedNodeIds = LabelLayout::stablePriorityOrder(nodeIds, previousLabelIds);

            for (int nid : orderedNodeIds) {
                if (!w_.isNodeVisible(nid)) continue;
                if (filterNodesByVisibility && visibleNodes.count(nid) == 0)
                    continue;
                auto it = w_.nodeToFirstVertex_.find(nid);
                if (it == w_.nodeToFirstVertex_.end()) continue;
                int vi = it->second;
                if (vi * 6 + 2 >= static_cast<int>(w_.mesh_.vertices.size())) continue;

                glm::vec3 pos(w_.mesh_.vertices[vi * 6],
                              w_.mesh_.vertices[vi * 6 + 1],
                              w_.mesh_.vertices[vi * 6 + 2]);
                QPointF sp = project(pos);
                if (sp.x() < 0) continue;
                if (!reserveBin(sp)) continue;

                QString text = QString::number(nid);
                int tx = static_cast<int>(sp.x()) - fm.horizontalAdvance(text) / 2;
                int ty = static_cast<int>(sp.y()) + offsetY;
                drawOutlinedText(tx, ty, text);
                drawnLabelIds.push_back(nid);
                if (usedBins.size() >= maxReservedBins) break;
            }
            rememberPlacedLabels();
        }

    } else if (w_.pickMode_ == PickMode::Part) {
        // ── 部件标签（在部件重心位置显示部件索引） ──
        if (!w_.selection_.selectedElements.empty() && !w_.triToElem_.empty() && !w_.triToPart_.empty()) {
            // 收集选中的部件索引
            std::set<int> selectedParts;
            for (int pi = 0; pi < static_cast<int>(w_.partElementIds_.size()); ++pi) {
                if (!w_.isPartVisible(pi)) continue;
                if (w_.pickRenderer_->isPartFullySelected(pi))
                    selectedParts.insert(pi);
            }

            std::unordered_map<long long, int> partLabelStacks;
            int drawnPartLabels = 0;
            std::vector<int> partIds(selectedParts.begin(), selectedParts.end());
            auto orderedPartIds = LabelLayout::stablePriorityOrder(partIds, previousLabelIds);

            // 计算每个选中部件的重心
            for (int pi : orderedPartIds) {
                if (pi < 0 || pi >= static_cast<int>(w_.partTriangles_.size())) continue;
                float sx = 0, sy = 0, sz = 0;
                int count = 0;
                for (int t : w_.partTriangles_[pi]) {
                    if (t * 3 + 2 >= static_cast<int>(w_.mesh_.indices.size())) continue;
                    for (int k = 0; k < 3; ++k) {
                        unsigned int vi = w_.mesh_.indices[t * 3 + k];
                        if (vi * 6 + 2 < w_.mesh_.vertices.size()) {
                            sx += w_.mesh_.vertices[vi * 6];
                            sy += w_.mesh_.vertices[vi * 6 + 1];
                            sz += w_.mesh_.vertices[vi * 6 + 2];
                            count++;
                        }
                    }
                }
                if (count == 0) continue;
                glm::vec3 center(sx / count, sy / count, sz / count);
                QPointF sp = project(center);
                if (sp.x() < -50.0 || sp.x() > wPx + 50.0 ||
                    sp.y() < -50.0 || sp.y() > hPx + 50.0)
                    continue;

                QString text = QString("Part %1").arg(pi + 1);
                int tx = static_cast<int>(sp.x()) - fm.horizontalAdvance(text) / 2;
                int stackOffset = LabelLayout::nextStackOffset(
                    sp.x(), sp.y(), binPx, fm.height(), partLabelStacks);
                int ty = static_cast<int>(sp.y()) + offsetY + stackOffset;
                drawOutlinedText(tx, ty, text);
                drawnLabelIds.push_back(pi);
                if (++drawnPartLabels >= static_cast<int>(maxReservedBins)) break;
            }
            rememberPlacedLabels();
        }

    } else {
        // ── 单元标签（在单元重心位置显示） ──
        if (!w_.selection_.selectedElements.empty() && !w_.triToElem_.empty()) {
            struct ElemAccum { float sx = 0, sy = 0, sz = 0; int count = 0; };
            std::unordered_map<int, ElemAccum> elemCentroids;

            int triCount = static_cast<int>(w_.triToElem_.size());
            int idxCount = static_cast<int>(w_.mesh_.indices.size());
            for (int t = 0; t < triCount; ++t) {
                if (t * 3 + 2 >= idxCount) break;
                if (!isTriangleVisible(t)) continue;
                int eid = w_.triToElem_[t];
                if (!w_.isElementVisible(eid)) continue;
                if (w_.selection_.selectedElements.count(eid) == 0) continue;
                auto& acc = elemCentroids[eid];
                for (int k = 0; k < 3; ++k) {
                    unsigned int vi = w_.mesh_.indices[t * 3 + k];
                    if (vi * 6 + 2 < w_.mesh_.vertices.size()) {
                        acc.sx += w_.mesh_.vertices[vi * 6];
                        acc.sy += w_.mesh_.vertices[vi * 6 + 1];
                        acc.sz += w_.mesh_.vertices[vi * 6 + 2];
                        acc.count++;
                    }
                }
            }

            std::vector<int> elemIds;
            elemIds.reserve(elemCentroids.size());
            for (const auto& entry : elemCentroids)
                elemIds.push_back(entry.first);
            auto orderedElemIds = LabelLayout::stablePriorityOrder(elemIds, previousLabelIds);

            for (int eid : orderedElemIds) {
                auto elemIt = elemCentroids.find(eid);
                if (elemIt == elemCentroids.end()) continue;
                const ElemAccum& acc = elemIt->second;
                if (acc.count == 0) continue;
                glm::vec3 center(acc.sx / acc.count, acc.sy / acc.count, acc.sz / acc.count);
                QPointF sp = project(center);
                if (sp.x() < 0) continue;
                if (!reserveBin(sp)) continue;

                QString text = QString::number(eid);
                int tx = static_cast<int>(sp.x()) - fm.horizontalAdvance(text) / 2;
                int ty = static_cast<int>(sp.y()) + offsetY;
                drawOutlinedText(tx, ty, text);
                drawnLabelIds.push_back(eid);
                if (usedBins.size() >= maxReservedBins) break;
            }
            rememberPlacedLabels();
        }
    }

    if (!placementUpdated) {
        lastLabelMode_ = -1;
        lastLabelIds_.clear();
    }
}
