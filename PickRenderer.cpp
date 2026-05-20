#include "PickRenderer.h"
#include "GLWidget.h"
#include "GLStateGuards.h"
#include "SelectionRenderer.h"

#include <QOpenGLShaderProgram>
#include <QOpenGLFramebufferObject>
#include <QFile>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <unordered_set>

static QByteArray loadShaderSource(const QString& resourcePath) {
    QFile f(resourcePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return f.readAll();
}

// ============================================================
// 初始化 / 窗口大小变化
// ============================================================

void PickRenderer::initGL() {
    pickShader_ = new QOpenGLShaderProgram();
    pickShader_->addShaderFromSourceCode(QOpenGLShader::Vertex, loadShaderSource(":/shaders/pick.vert"));
    pickShader_->addShaderFromSourceCode(QOpenGLShader::Fragment, loadShaderSource(":/shaders/pick.frag"));
    pickShader_->link();
    pickVao_.create();
}

PickRenderer::~PickRenderer() {
    delete pickFbo_;
    delete pickShader_;
}

void PickRenderer::resizeFbo(int w, int h, int dpr) {
    delete pickFbo_;
    pickFbo_ = nullptr;
    if (w <= 0 || h <= 0 || dpr <= 0) return;
    pickFbo_ = new QOpenGLFramebufferObject(w * dpr, h * dpr,
        QOpenGLFramebufferObject::Depth);
}

// ============================================================
// 延迟拾取分发
// ============================================================

void PickRenderer::processDeferredPicks() {
    if (pickPointPending_) {
        pickPointPending_ = false;
        pickAtPoint(pendingPickPos_, pendingPickCtrl_);
    }
    if (pickRectPending_) {
        pickRectPending_ = false;
        pickInRect(pendingPickRect_, pendingPickCtrl_);
    }
    if (deselectPointPending_) {
        deselectPointPending_ = false;
        deselectAtPoint(pendingDeselectPos_);
    }
    if (deselectRectPending_) {
        deselectRectPending_ = false;
        deselectInRect(pendingDeselectRect_);
    }
}

void PickRenderer::requestPickPoint(const QPoint& pos, bool ctrlHeld) {
    pickPointPending_ = true;
    pendingPickPos_ = pos;
    pendingPickCtrl_ = ctrlHeld;
}

void PickRenderer::requestPickRect(const QRect& rect, bool ctrlHeld) {
    pickRectPending_ = true;
    pendingPickRect_ = rect;
    pendingPickCtrl_ = ctrlHeld;
}

void PickRenderer::requestDeselectPoint(const QPoint& pos) {
    deselectPointPending_ = true;
    pendingDeselectPos_ = pos;
}

void PickRenderer::requestDeselectRect(const QRect& rect) {
    deselectRectPending_ = true;
    pendingDeselectRect_ = rect;
}

// ============================================================
// ID ↔ 颜色编码
// ============================================================

glm::vec3 PickRenderer::idToColor(int id) {
    id += 1;
    int r = (id      ) & 0xFF;
    int g = (id >>  8) & 0xFF;
    int b = (id >> 16) & 0xFF;
    return glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
}

int PickRenderer::colorToId(unsigned char r, unsigned char g, unsigned char b) {
    if (r == 0 && g == 0 && b == 0) return -1;
    int id = r | (g << 8) | (b << 16);
    return id - 1;
}

// ============================================================
// 辅助
// ============================================================

glm::mat4 PickRenderer::buildPickMvp() const {
    float aspect = (w_.height() > 0) ? static_cast<float>(w_.width()) / w_.height() : 1.0f;
    glm::mat4 proj = w_.projectionMatrix(aspect,
        w_.cam_.distance * 0.01f, w_.cam_.distance * 10.0f);
    return proj * w_.cam_.viewMatrix();
}

void PickRenderer::emitSelectionSignals() {
    w_.selectionRenderer_->markSelectionDirty();
    {
        std::vector<int> ids;
        if (!w_.selection_.selectedNodes.empty())
            ids.assign(w_.selection_.selectedNodes.begin(), w_.selection_.selectedNodes.end());
        else
            ids.assign(w_.selection_.selectedElements.begin(), w_.selection_.selectedElements.end());
        std::sort(ids.begin(), ids.end());
        emit w_.selectionChanged(w_.pickMode_, static_cast<int>(ids.size()), ids);
    }
    if (w_.pickMode_ == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(w_.partElementIds_.size()); ++pi)
            if (isPartFullySelected(pi)) pickedParts.push_back(pi);
        emit w_.partsPicked(pickedParts);
    }
}

// ============================================================
// 部件级选择
// ============================================================

void PickRenderer::selectPart(int partIndex) {
    if (partIndex < 0 || partIndex >= static_cast<int>(w_.partElementIds_.size())) return;
    for (int eid : w_.partElementIds_[partIndex])
        w_.selection_.selectedElements.insert(eid);
}

void PickRenderer::deselectPart(int partIndex) {
    if (partIndex < 0 || partIndex >= static_cast<int>(w_.partElementIds_.size())) return;
    for (int eid : w_.partElementIds_[partIndex])
        w_.selection_.selectedElements.erase(eid);
}

bool PickRenderer::isPartFullySelected(int partIndex) const {
    if (partIndex < 0 || partIndex >= static_cast<int>(w_.partElementIds_.size())) return false;
    const auto& elems = w_.partElementIds_[partIndex];
    if (elems.empty()) return false;
    for (int eid : elems)
        if (!w_.selection_.isElementSelected(eid)) return false;
    return true;
}

// ============================================================
// 离屏拾取渲染
// ============================================================

void PickRenderer::renderPickBuffer(const glm::mat4& mvp) {
    if (!pickFbo_ || w_.triToElem_.empty()) return;

    ScopedViewport   svp(&w_);
    ScopedClearColor scc(&w_);
    ScopedDepthTest  sdt(&w_, true);
    ScopedBlend      sbl(&w_, false);

    // raw glBindFramebuffer：避免污染 Qt 的 current_fbo 追踪指针
    ScopedFramebufferBind fbo(&w_, pickFbo_->handle(), [&]() { w_.bindWidgetFramebuffer(); });

    int dpr = w_.devicePixelRatio();
    w_.glViewport(0, 0, w_.width() * dpr, w_.height() * dpr);

    w_.glClearColor(0, 0, 0, 1);
    w_.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    pickShader_->bind();
    pickShader_->setUniformValue("uMVP",
        QMatrix4x4(glm::value_ptr(glm::transpose(mvp))));

    GLint pickColorLoc = pickShader_->uniformLocation("uPickColor");

    pickVao_.bind();
    ScopedBufferBind bindVbo(w_.vbo_);
    w_.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    w_.glEnableVertexAttribArray(0);

    GLuint rawIbo = 0;
    w_.glGenBuffers(1, &rawIbo);
    {
        ScopedRawBufferBind bindRawIbo(&w_, GL_ELEMENT_ARRAY_BUFFER, rawIbo);
        w_.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                        static_cast<GLsizeiptr>(w_.allTriIndices_.size() * sizeof(unsigned int)),
                        w_.allTriIndices_.data(), GL_STATIC_DRAW);

        int triCount = static_cast<int>(w_.triToElem_.size());
        int i = 0;
        while (i < triCount) {
            int elemId = w_.triToElem_[i];
            int start = i;
        while (i < triCount && w_.triToElem_[i] == elemId) ++i;
            if (!w_.isElementRenderable(elemId))
                continue;

            glm::vec3 c = idToColor(elemId);
            w_.glUniform3f(pickColorLoc, c.x, c.y, c.z);
            w_.glDrawElements(GL_TRIANGLES, (i - start) * 3, GL_UNSIGNED_INT,
                              reinterpret_cast<void*>(start * 3 * sizeof(unsigned int)));
        }
    }

    w_.glDeleteBuffers(1, &rawIbo);

    pickVao_.release();
    pickShader_->release();
}

// ============================================================
// 点选
// ============================================================

void PickRenderer::pickAtPoint(const QPoint& pos, bool ctrlHeld) {
    if (!pickFbo_ || w_.triToElem_.empty()) return;

    glm::mat4 mvp = buildPickMvp();
    renderPickBuffer(mvp);

    unsigned char pixel[4] = {0};
    {
        ScopedFramebufferBind fbo(&w_, pickFbo_->handle(), [&]() { w_.bindWidgetFramebuffer(); });
        int dpr = w_.devicePixelRatio();
        int px = pos.x() * dpr;
        int py = (w_.height() - pos.y()) * dpr;
        w_.glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    }

    int elemId = colorToId(pixel[0], pixel[1], pixel[2]);

    if (w_.pickMode_ == PickMode::Node) {
        int closestNode = -1;
        if (elemId >= 0) {
            float ndcX = (2.0f * pos.x() / w_.width()) - 1.0f;
            float ndcY = 1.0f - (2.0f * pos.y() / w_.height());
            float minDist2 = 1e30f;
            int triCount = static_cast<int>(w_.triToElem_.size());
            for (int t = 0; t < triCount; ++t) {
                if (w_.triToElem_[t] != elemId) continue;
                for (int v = 0; v < 3; ++v) {
                    unsigned int vi = w_.mesh_.indices[t * 3 + v];
                    glm::vec4 wp(w_.mesh_.vertices[vi * 6],
                                 w_.mesh_.vertices[vi * 6 + 1],
                                 w_.mesh_.vertices[vi * 6 + 2], 1.0f);
                    glm::vec4 clip = mvp * wp;
                    if (clip.w <= 0) continue;
                    float sx = clip.x / clip.w;
                    float sy = clip.y / clip.w;
                    float d2 = (sx - ndcX) * (sx - ndcX) + (sy - ndcY) * (sy - ndcY);
                    if (d2 < minDist2) {
                        minDist2 = d2;
                        closestNode = (vi < w_.vertexToNode_.size()) ? w_.vertexToNode_[vi] : static_cast<int>(vi);
                    }
                }
            }
        }
        if (!ctrlHeld) {
            w_.selection_.clear();
            if (closestNode >= 0) w_.selection_.selectedNodes.insert(closestNode);
        } else {
            if (closestNode >= 0) w_.selection_.toggleNode(closestNode);
        }

    } else if (w_.pickMode_ == PickMode::Part) {
        int hitPart = -1;
        if (elemId >= 0 && !w_.elemToPart_.empty()) {
            auto it = w_.elemToPart_.find(elemId);
            if (it != w_.elemToPart_.end()) hitPart = it->second;
        }
        if (!ctrlHeld) {
            w_.selection_.clear();
            if (hitPart >= 0) selectPart(hitPart);
        } else {
            if (hitPart >= 0) {
                if (isPartFullySelected(hitPart)) deselectPart(hitPart);
                else                              selectPart(hitPart);
            }
        }

    } else {
        if (!ctrlHeld) {
            w_.selection_.clear();
            if (elemId >= 0) w_.selection_.selectedElements.insert(elemId);
        } else {
            if (elemId >= 0) w_.selection_.toggleElement(elemId);
        }
    }

    emitSelectionSignals();
}

// ============================================================
// 框选
// ============================================================

void PickRenderer::pickInRect(const QRect& rect, bool ctrlHeld) {
    if (w_.triToElem_.empty()) return;

    glm::mat4 mvp = buildPickMvp();

    float ndcL = (2.0f * rect.left() / w_.width()) - 1.0f;
    float ndcR = (2.0f * rect.right() / w_.width()) - 1.0f;
    float ndcT = 1.0f - (2.0f * rect.top() / w_.height());
    float ndcB = 1.0f - (2.0f * rect.bottom() / w_.height());
    if (ndcL > ndcR) std::swap(ndcL, ndcR);
    if (ndcB > ndcT) std::swap(ndcB, ndcT);

    if (!ctrlHeld) w_.selection_.clear();

    int vertCount = static_cast<int>(w_.mesh_.vertices.size() / 6);

    auto inside = [&](const glm::vec3& p) {
        glm::vec4 c = mvp * glm::vec4(p, 1.0f);
        if (c.w <= 0) return false;
        float sx = c.x / c.w, sy = c.y / c.w;
        return sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT;
    };
    // 有表面缓存时走"全单元节点投影 + 包含式"：单元所有节点都落在框内才选中，
    // 从而能框到内部/被遮挡的实体单元（商软常见的 enclosed through-depth 框选）。
    const bool useAll = w_.hasSurfaceCache_ && !w_.surfaceCache_.coords.empty()
                        && !w_.elemToNodes_.empty();
    // 单元形心落在框内即选中（through-depth）：比"所有节点入框"更直觉，
    // 不会因表层/边缘单元未被完整框住而漏选，框一块即可整块选中。
    auto centerInside = [&](const auto& nodes) {
        glm::vec3 c(0.0f); int cnt = 0;
        for (int nid : nodes) {
            auto cit = w_.surfaceCache_.coords.find(nid);
            if (cit != w_.surfaceCache_.coords.end()) { c += cit->second; ++cnt; }
        }
        return cnt > 0 && inside(c / static_cast<float>(cnt));
    };

    if (w_.pickMode_ == PickMode::Node) {
        if (useAll) {
            for (const auto& [nodeId, pos] : w_.surfaceCache_.coords) {
                if (nodeId < 0 || !w_.isNodeVisible(nodeId)) continue;
                if (inside(pos)) w_.selection_.selectedNodes.insert(nodeId);
            }
        } else {
            std::unordered_set<int> addedNodes;
            for (int vi = 0; vi < vertCount; ++vi) {
                int nodeId = (vi < static_cast<int>(w_.vertexToNode_.size())) ? w_.vertexToNode_[vi] : vi;
                if (nodeId < 0 || !w_.isNodeVisible(nodeId)) continue;
                glm::vec3 p(w_.mesh_.vertices[vi * 6], w_.mesh_.vertices[vi * 6 + 1], w_.mesh_.vertices[vi * 6 + 2]);
                if (inside(p) && addedNodes.insert(nodeId).second)
                    w_.selection_.selectedNodes.insert(nodeId);
            }
        }
    } else if (w_.pickMode_ == PickMode::Part) {
        std::unordered_set<int> hitParts;
        if (useAll) {
            for (const auto& [elemId, nodes] : w_.elemToNodes_) {
                if (!w_.isElementVisible(elemId) || !centerInside(nodes)) continue;
                auto pit = w_.elemToPart_.find(elemId);
                if (pit != w_.elemToPart_.end()) hitParts.insert(pit->second);
            }
        } else {
            int triCount = static_cast<int>(w_.triToElem_.size());
            for (int t = 0; t < triCount; ++t) {
                if (!w_.isTriangleVisible(t)) continue;
                bool anyInside = false;
                for (int v = 0; v < 3; ++v) {
                    unsigned int vi = w_.mesh_.indices[t * 3 + v];
                    glm::vec3 p(w_.mesh_.vertices[vi * 6], w_.mesh_.vertices[vi * 6 + 1], w_.mesh_.vertices[vi * 6 + 2]);
                    if (inside(p)) { anyInside = true; break; }
                }
                if (anyInside && t < static_cast<int>(w_.triToPart_.size()))
                    hitParts.insert(w_.triToPart_[t]);
            }
        }
        for (int p : hitParts) selectPart(p);
    } else {
        if (useAll) {
            for (const auto& [elemId, nodes] : w_.elemToNodes_) {
                if (w_.selection_.isElementSelected(elemId)) continue;
                if (!w_.isElementVisible(elemId) || !centerInside(nodes)) continue;
                w_.selection_.selectedElements.insert(elemId);
            }
        } else {
            int triCount = static_cast<int>(w_.triToElem_.size());
            for (int t = 0; t < triCount; ++t) {
                if (!w_.isTriangleVisible(t)) continue;
                int elemId = w_.triToElem_[t];
                if (w_.selection_.isElementSelected(elemId)) continue;
                bool anyInside = false;
                for (int v = 0; v < 3; ++v) {
                    unsigned int vi = w_.mesh_.indices[t * 3 + v];
                    glm::vec3 p(w_.mesh_.vertices[vi * 6], w_.mesh_.vertices[vi * 6 + 1], w_.mesh_.vertices[vi * 6 + 2]);
                    if (inside(p)) { anyInside = true; break; }
                }
                if (anyInside) w_.selection_.selectedElements.insert(elemId);
            }
        }
    }

    emitSelectionSignals();
}

// ============================================================
// 点选取消
// ============================================================

void PickRenderer::deselectAtPoint(const QPoint& pos) {
    if (!pickFbo_ || w_.triToElem_.empty()) return;

    glm::mat4 mvp = buildPickMvp();
    renderPickBuffer(mvp);

    unsigned char pixel[4] = {0};
    {
        ScopedFramebufferBind fbo(&w_, pickFbo_->handle(), [&]() { w_.bindWidgetFramebuffer(); });
        int dpr = w_.devicePixelRatio();
        int px = pos.x() * dpr;
        int py = (w_.height() - pos.y()) * dpr;
        w_.glReadPixels(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    }

    int elemId = colorToId(pixel[0], pixel[1], pixel[2]);

    if (w_.pickMode_ == PickMode::Node) {
        int closestNode = -1;
        if (elemId >= 0) {
            float ndcX = (2.0f * pos.x() / w_.width()) - 1.0f;
            float ndcY = 1.0f - (2.0f * pos.y() / w_.height());
            float minDist2 = 1e30f;
            int triCount = static_cast<int>(w_.triToElem_.size());
            for (int t = 0; t < triCount; ++t) {
                if (w_.triToElem_[t] != elemId) continue;
                for (int v = 0; v < 3; ++v) {
                    unsigned int vi = w_.mesh_.indices[t * 3 + v];
                    glm::vec4 wp(w_.mesh_.vertices[vi * 6], w_.mesh_.vertices[vi * 6 + 1],
                                 w_.mesh_.vertices[vi * 6 + 2], 1.0f);
                    glm::vec4 clip = mvp * wp;
                    if (clip.w <= 0) continue;
                    float sx = clip.x / clip.w;
                    float sy = clip.y / clip.w;
                    float d2 = (sx - ndcX) * (sx - ndcX) + (sy - ndcY) * (sy - ndcY);
                    if (d2 < minDist2) { minDist2 = d2; closestNode = (vi < w_.vertexToNode_.size()) ? w_.vertexToNode_[vi] : static_cast<int>(vi); }
                }
            }
        }
        if (closestNode >= 0) w_.selection_.selectedNodes.erase(closestNode);

    } else if (w_.pickMode_ == PickMode::Part) {
        if (elemId >= 0 && !w_.elemToPart_.empty()) {
            auto it = w_.elemToPart_.find(elemId);
            if (it != w_.elemToPart_.end()) deselectPart(it->second);
        }

    } else {
        if (elemId >= 0) w_.selection_.selectedElements.erase(elemId);
    }

    emitSelectionSignals();
}

// ============================================================
// 框选取消
// ============================================================

void PickRenderer::deselectInRect(const QRect& rect) {
    if (w_.triToElem_.empty()) return;

    glm::mat4 mvp = buildPickMvp();

    float ndcL = (2.0f * rect.left() / w_.width()) - 1.0f;
    float ndcR = (2.0f * rect.right() / w_.width()) - 1.0f;
    float ndcT = 1.0f - (2.0f * rect.top() / w_.height());
    float ndcB = 1.0f - (2.0f * rect.bottom() / w_.height());
    if (ndcL > ndcR) std::swap(ndcL, ndcR);
    if (ndcB > ndcT) std::swap(ndcB, ndcT);

    int vertCount = static_cast<int>(w_.mesh_.vertices.size() / 6);

    auto inside = [&](const glm::vec3& p) {
        glm::vec4 c = mvp * glm::vec4(p, 1.0f);
        if (c.w <= 0) return false;
        float sx = c.x / c.w, sy = c.y / c.w;
        return sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT;
    };
    const bool useAll = w_.hasSurfaceCache_ && !w_.surfaceCache_.coords.empty()
                        && !w_.elemToNodes_.empty();
    // 单元形心落在框内即选中（through-depth）：比"所有节点入框"更直觉，
    // 不会因表层/边缘单元未被完整框住而漏选，框一块即可整块选中。
    auto centerInside = [&](const auto& nodes) {
        glm::vec3 c(0.0f); int cnt = 0;
        for (int nid : nodes) {
            auto cit = w_.surfaceCache_.coords.find(nid);
            if (cit != w_.surfaceCache_.coords.end()) { c += cit->second; ++cnt; }
        }
        return cnt > 0 && inside(c / static_cast<float>(cnt));
    };

    if (w_.pickMode_ == PickMode::Node) {
        if (useAll) {
            for (const auto& [nodeId, pos] : w_.surfaceCache_.coords) {
                if (nodeId < 0) continue;
                if (inside(pos)) w_.selection_.selectedNodes.erase(nodeId);
            }
        } else {
            std::unordered_set<int> removedNodes;
            for (int vi = 0; vi < vertCount; ++vi) {
                int nodeId = (vi < static_cast<int>(w_.vertexToNode_.size())) ? w_.vertexToNode_[vi] : vi;
                if (nodeId < 0 || !w_.isNodeVisible(nodeId)) continue;
                glm::vec3 p(w_.mesh_.vertices[vi * 6], w_.mesh_.vertices[vi * 6 + 1], w_.mesh_.vertices[vi * 6 + 2]);
                if (inside(p) && removedNodes.insert(nodeId).second)
                    w_.selection_.selectedNodes.erase(nodeId);
            }
        }
    } else if (w_.pickMode_ == PickMode::Part) {
        std::unordered_set<int> hitParts;
        if (useAll) {
            for (const auto& [elemId, nodes] : w_.elemToNodes_) {
                if (!centerInside(nodes)) continue;
                auto pit = w_.elemToPart_.find(elemId);
                if (pit != w_.elemToPart_.end()) hitParts.insert(pit->second);
            }
        } else {
            int triCount = static_cast<int>(w_.triToElem_.size());
            for (int t = 0; t < triCount; ++t) {
                if (!w_.isTriangleVisible(t)) continue;
                bool anyInside = false;
                for (int v = 0; v < 3; ++v) {
                    unsigned int vi = w_.mesh_.indices[t * 3 + v];
                    glm::vec3 p(w_.mesh_.vertices[vi * 6], w_.mesh_.vertices[vi * 6 + 1], w_.mesh_.vertices[vi * 6 + 2]);
                    if (inside(p)) { anyInside = true; break; }
                }
                if (anyInside && t < static_cast<int>(w_.triToPart_.size()))
                    hitParts.insert(w_.triToPart_[t]);
            }
        }
        for (int p : hitParts) deselectPart(p);
    } else {
        if (useAll) {
            for (const auto& [elemId, nodes] : w_.elemToNodes_) {
                if (!w_.selection_.isElementSelected(elemId)) continue;
                if (centerInside(nodes)) w_.selection_.selectedElements.erase(elemId);
            }
        } else {
            int triCount = static_cast<int>(w_.triToElem_.size());
            for (int t = 0; t < triCount; ++t) {
                if (!w_.isTriangleVisible(t)) continue;
                int elemId = w_.triToElem_[t];
                if (!w_.selection_.isElementSelected(elemId)) continue;
                bool anyInside = false;
                for (int v = 0; v < 3; ++v) {
                    unsigned int vi = w_.mesh_.indices[t * 3 + v];
                    glm::vec3 p(w_.mesh_.vertices[vi * 6], w_.mesh_.vertices[vi * 6 + 1], w_.mesh_.vertices[vi * 6 + 2]);
                    if (inside(p)) { anyInside = true; break; }
                }
                if (anyInside) w_.selection_.selectedElements.erase(elemId);
            }
        }
    }

    emitSelectionSignals();
}
