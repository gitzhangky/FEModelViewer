#include "SelectionRenderer.h"

#include "GLStateGuards.h"
#include "GLWidget.h"

#include <QOpenGLShaderProgram>
#include <QVector3D>
#include <algorithm>
#include <unordered_set>

SelectionRenderer::SelectionRenderer(GLWidget& w) : w_(w) {}

void SelectionRenderer::initGL() {
    selEdgeVao_.create();
    selEdgeVbo_.create();
}

void SelectionRenderer::resetForMesh() {
    selEdgeVertCount_ = 0;
    selectionDirty_ = true;
    silhouetteDirty_ = false;
    partEdgeCacheValid_ = false;
    edgeAdjDirty_ = true;
    cachedStaticEdgeVerts_.clear();
    cachedSilhouettes_.clear();
    edgeAdjMap_.clear();
}

void SelectionRenderer::markSelectionDirty() {
    selectionDirty_ = true;
    partEdgeCacheValid_ = false;
}

void SelectionRenderer::markSilhouetteDirty() {
    silhouetteDirty_ = true;
}

void SelectionRenderer::markEdgeAdjacencyDirty() {
    edgeAdjDirty_ = true;
    partEdgeCacheValid_ = false;
}

void SelectionRenderer::invalidatePartEdgeCache() {
    partEdgeCacheValid_ = false;
}

void SelectionRenderer::clearHighlight() {
    selEdgeVertCount_ = 0;
    selectionDirty_ = true;
    silhouetteDirty_ = false;
    partEdgeCacheValid_ = false;
}

void SelectionRenderer::update() {
    if (selectionDirty_) {
        int hlMode = 0;

        if (!w_.selection_.selectedElements.empty() && !w_.triToElem_.empty()) {
            partEdgeCacheValid_ = false;
            rebuildSelectionEdges();
            hlMode = 0;
        } else if (!w_.selection_.selectedNodes.empty()) {
            std::vector<float> hlVerts;
            for (int nid : w_.selection_.selectedNodes) {
                if (!w_.isNodeVisible(nid)) continue;
                int vi = -1;
                if (!w_.nodeToFirstVertex_.empty()) {
                    auto it = w_.nodeToFirstVertex_.find(nid);
                    if (it != w_.nodeToFirstVertex_.end()) vi = it->second;
                } else {
                    vi = nid;
                }
                if (vi >= 0 && vi * 6 + 2 < static_cast<int>(w_.mesh_.vertices.size())) {
                    hlVerts.push_back(w_.mesh_.vertices[vi * 6]);
                    hlVerts.push_back(w_.mesh_.vertices[vi * 6 + 1]);
                    hlVerts.push_back(w_.mesh_.vertices[vi * 6 + 2]);
                } else if (w_.hasSurfaceCache_) {
                    // 内部节点不在渲染网格里，退回到表面缓存坐标，保证高亮点可见
                    auto cit = w_.surfaceCache_.coords.find(nid);
                    if (cit != w_.surfaceCache_.coords.end()) {
                        hlVerts.push_back(cit->second.x);
                        hlVerts.push_back(cit->second.y);
                        hlVerts.push_back(cit->second.z);
                    }
                }
            }
            uploadHighlightVertices(hlVerts);
            hlMode = 1;
        } else {
            selEdgeVertCount_ = 0;
        }

        selectionDirty_ = false;
        silhouetteDirty_ = false;
        selHlMode_ = hlMode;
    } else if (silhouetteDirty_ && partEdgeCacheValid_ &&
               w_.pickMode_ == PickMode::Part && w_.selection_.hasSelection()) {
        updateSilhouetteFromCache();
        silhouetteDirty_ = false;
    }
}

void SelectionRenderer::render(QOpenGLShaderProgram& shader) {
    if (selEdgeVertCount_ <= 0 || !w_.selection_.hasSelection()) return;

    shader.setUniformValue("uWireframe", true);
    shader.setUniformValue("uPointHighlight", selHlMode_ == 1);
    shader.setUniformValue("uWireAlpha", 1.0f);
    shader.setUniformValue("uUseVertexColor", false);
    shader.setUniformValue("uColor", QVector3D(1.0f, 0.78f, 0.0f));

    ScopedDepthTest depth(&w_, false);
    selEdgeVao_.bind();
    if (selHlMode_ == 1) {
        ScopedBlend blend(&w_, true);
        w_.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glPointSize(10.0f);
        w_.glDrawArrays(GL_POINTS, 0, selEdgeVertCount_);
        glPointSize(1.0f);
    } else {
        w_.setLineWidthClamped(2.5f);
        w_.glDrawArrays(GL_LINES, 0, selEdgeVertCount_);
        glLineWidth(1.0f);
    }
    selEdgeVao_.release();
    shader.setUniformValue("uPointHighlight", false);
}

void SelectionRenderer::rebuildSelectionEdges() {
    const bool useSurfaceEdgeCache = w_.hasSurfaceCache_ && !w_.surfaceElemEdgeToElement_.empty();
    const std::vector<float>& edgeVertices = useSurfaceEdgeCache
        ? w_.surfaceElemEdgeVertices_
        : w_.mesh_.elemEdgeVertices;
    const std::vector<int>& edgeToElement = useSurfaceEdgeCache
        ? w_.surfaceElemEdgeToElement_
        : w_.mesh_.elemEdgeToElement;

    int edgeCount = static_cast<int>(edgeToElement.size());
    std::vector<float> verts;

    if (w_.pickMode_ == PickMode::Part && !w_.vertexToNode_.empty()) {
        // 部件模式：使用缓存机制，避免每帧重建 edgeMap
        if (!partEdgeCacheValid_) {
            buildPartEdgeCache();
        }
        updateSilhouetteFromCache();
        return;  // VBO 已在 updateSilhouetteFromCache 中上传
    }

    // 单元模式：显示所有选中单元的全部边线
    for (int i = 0; i < edgeCount; ++i) {
        int elemId = edgeToElement[i];
        if (!w_.selection_.isElementSelected(elemId)) continue;
        if (!w_.isElementVisible(elemId)) continue;

        int base = i * 6;
        if (base + 5 >= static_cast<int>(edgeVertices.size())) continue;
        for (int j = 0; j < 6; ++j)
            verts.push_back(edgeVertices[base + j]);
    }

    uploadHighlightVertices(verts);
}

void SelectionRenderer::buildEdgeAdjacency() {
    edgeAdjMap_.clear();
    edgeAdjDirty_ = false;

    int triCount = static_cast<int>(w_.mesh_.indices.size() / 3);
    if (triCount == 0) return;

    // 预分配（每个三角形 3 条边，约 50% 共享 → ~1.5x triCount 条边）
    edgeAdjMap_.reserve(triCount * 2);

    for (int t = 0; t < triCount; ++t) {
        for (int e = 0; e < 3; ++e) {
            unsigned int vi_a = w_.mesh_.indices[t * 3 + e];
            unsigned int vi_b = w_.mesh_.indices[t * 3 + (e + 1) % 3];
            int na = (vi_a < w_.vertexToNode_.size()) ? w_.vertexToNode_[vi_a] : static_cast<int>(vi_a);
            int nb = (vi_b < w_.vertexToNode_.size()) ? w_.vertexToNode_[vi_b] : static_cast<int>(vi_b);
            int64_t key = (static_cast<int64_t>(std::min(na, nb)) << 32) |
                          static_cast<uint32_t>(std::max(na, nb));

            auto& pe = edgeAdjMap_[key];
            if (pe.adjTris.empty()) {
                pe.va = vi_a;
                pe.vb = vi_b;
            }
            pe.adjTris.push_back(t);
        }
    }
}

void SelectionRenderer::buildPartEdgeCache() {
    // 确保边邻接表已构建
    if (edgeAdjDirty_) buildEdgeAdjacency();

    cachedStaticEdgeVerts_.clear();
    cachedSilhouettes_.clear();

    // ── 1. 收集选中且可见的部件索引 ──
    std::unordered_set<int> selectedParts;
    int numParts = static_cast<int>(w_.partElementIds_.size());
    for (int p = 0; p < numParts; ++p) {
        auto vit = w_.partVisibility_.find(p);
        if (vit != w_.partVisibility_.end() && !vit->second) continue;
        for (int eid : w_.partElementIds_[p]) {
            if (w_.selection_.isElementSelected(eid) && w_.isElementVisible(eid)) {
                selectedParts.insert(p);
                break;
            }
        }
    }

    if (selectedParts.empty()) {
        partEdgeCacheValid_ = true;
        return;
    }

    // ── 2. 只遍历选中部件的三角形，收集边并分类 ──
    const float featureAngleThreshold = 0.5f;  // cos(60°)

    auto triNormal = [&](int t) -> glm::vec3 {
        unsigned int i0 = w_.mesh_.indices[t * 3];
        unsigned int i1 = w_.mesh_.indices[t * 3 + 1];
        unsigned int i2 = w_.mesh_.indices[t * 3 + 2];
        glm::vec3 p0(w_.mesh_.vertices[i0 * 6], w_.mesh_.vertices[i0 * 6 + 1], w_.mesh_.vertices[i0 * 6 + 2]);
        glm::vec3 p1(w_.mesh_.vertices[i1 * 6], w_.mesh_.vertices[i1 * 6 + 1], w_.mesh_.vertices[i1 * 6 + 2]);
        glm::vec3 p2(w_.mesh_.vertices[i2 * 6], w_.mesh_.vertices[i2 * 6 + 1], w_.mesh_.vertices[i2 * 6 + 2]);
        glm::vec3 cr = glm::cross(p1 - p0, p2 - p0);
        float len = glm::length(cr);
        return (len > 1e-12f) ? cr / len : glm::vec3(0.0f);
    };

    auto pushEdgeVerts = [&](unsigned int a, unsigned int b, std::vector<float>& out) {
        out.push_back(w_.mesh_.vertices[a * 6]);
        out.push_back(w_.mesh_.vertices[a * 6 + 1]);
        out.push_back(w_.mesh_.vertices[a * 6 + 2]);
        out.push_back(w_.mesh_.vertices[b * 6]);
        out.push_back(w_.mesh_.vertices[b * 6 + 1]);
        out.push_back(w_.mesh_.vertices[b * 6 + 2]);
    };

    // 用 visited 集合确保每条边只处理一次
    std::unordered_set<int64_t> visitedEdges;

    // 预估容量（减少 rehash）
    int totalSelectedTris = 0;
    for (int p : selectedParts) totalSelectedTris += static_cast<int>(w_.partTriangles_[p].size());
    visitedEdges.reserve(totalSelectedTris * 2);
    cachedStaticEdgeVerts_.reserve(totalSelectedTris * 6);

    for (int p : selectedParts) {
        for (int t : w_.partTriangles_[p]) {
            for (int e = 0; e < 3; ++e) {
                unsigned int vi_a = w_.mesh_.indices[t * 3 + e];
                unsigned int vi_b = w_.mesh_.indices[t * 3 + (e + 1) % 3];
                int na = (vi_a < w_.vertexToNode_.size()) ? w_.vertexToNode_[vi_a] : static_cast<int>(vi_a);
                int nb = (vi_b < w_.vertexToNode_.size()) ? w_.vertexToNode_[vi_b] : static_cast<int>(vi_b);
                int64_t key = (static_cast<int64_t>(std::min(na, nb)) << 32) |
                              static_cast<uint32_t>(std::max(na, nb));

                if (!visitedEdges.insert(key).second) continue;  // 已处理

                auto it = edgeAdjMap_.find(key);
                if (it == edgeAdjMap_.end()) continue;

                const PreEdge& pe = it->second;

                // 分类邻接三角形
                int selectedTriCount = 0;
                int otherTriCount = 0;
                int selTri0 = -1, selTri1 = -1;

                for (int adjT : pe.adjTris) {
                    int adjPart = (adjT < static_cast<int>(w_.triToPart_.size())) ? w_.triToPart_[adjT] : -1;
                    if (adjPart >= 0 && selectedParts.count(adjPart)) {
                        if (selectedTriCount == 0) selTri0 = adjT;
                        else if (selectedTriCount == 1) selTri1 = adjT;
                        selectedTriCount++;
                    } else {
                        otherTriCount++;
                    }
                }

                // 边界边（与非选中部件共享）
                if (otherTriCount > 0) {
                    pushEdgeVerts(pe.va, pe.vb, cachedStaticEdgeVerts_);
                    continue;
                }
                // 开放边（只有一个三角形）
                if (selectedTriCount == 1) {
                    pushEdgeVerts(pe.va, pe.vb, cachedStaticEdgeVerts_);
                    continue;
                }
                // 特征边 or 轮廓边候选
                if (selectedTriCount >= 2 && selTri0 >= 0 && selTri1 >= 0) {
                    glm::vec3 n0 = triNormal(selTri0);
                    glm::vec3 n1 = triNormal(selTri1);
                    if (glm::dot(n0, n1) < featureAngleThreshold) {
                        pushEdgeVerts(pe.va, pe.vb, cachedStaticEdgeVerts_);
                    } else {
                        SilhouetteCandidate sc;
                        sc.ax = w_.mesh_.vertices[pe.va * 6];
                        sc.ay = w_.mesh_.vertices[pe.va * 6 + 1];
                        sc.az = w_.mesh_.vertices[pe.va * 6 + 2];
                        sc.bx = w_.mesh_.vertices[pe.vb * 6];
                        sc.by = w_.mesh_.vertices[pe.vb * 6 + 1];
                        sc.bz = w_.mesh_.vertices[pe.vb * 6 + 2];
                        sc.n0 = n0;
                        sc.n1 = n1;
                        cachedSilhouettes_.push_back(sc);
                    }
                }
            }
        }
    }

    partEdgeCacheValid_ = true;
}

void SelectionRenderer::updateSilhouetteFromCache() {
    // 预分配：静态边 + 最大可能的轮廓边
    size_t staticSize = cachedStaticEdgeVerts_.size();
    std::vector<float> verts;
    verts.reserve(staticSize + cachedSilhouettes_.size() * 6);

    // 复制静态边（边界/特征/开放）
    verts.insert(verts.end(), cachedStaticEdgeVerts_.begin(), cachedStaticEdgeVerts_.end());

    // 添加视角依赖的轮廓边
    glm::vec3 eyePos = w_.cam_.eye();
    for (const auto& sc : cachedSilhouettes_) {
        glm::vec3 edgeMid((sc.ax + sc.bx) * 0.5f,
                          (sc.ay + sc.by) * 0.5f,
                          (sc.az + sc.bz) * 0.5f);
        glm::vec3 viewDir = eyePos - edgeMid;
        float d0 = glm::dot(sc.n0, viewDir);
        float d1 = glm::dot(sc.n1, viewDir);
        if (d0 * d1 <= 0.0f) {
            verts.push_back(sc.ax); verts.push_back(sc.ay); verts.push_back(sc.az);
            verts.push_back(sc.bx); verts.push_back(sc.by); verts.push_back(sc.bz);
        }
    }

    uploadHighlightVertices(verts);
}

void SelectionRenderer::uploadHighlightVertices(const std::vector<float>& verts) {
    selEdgeVertCount_ = static_cast<int>(verts.size() / 3);

    selEdgeVao_.bind();
    {
        ScopedBufferBind bind(selEdgeVbo_);
        if (!verts.empty())
            selEdgeVbo_.allocate(verts.data(), static_cast<int>(verts.size() * sizeof(float)));
        w_.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        w_.glEnableVertexAttribArray(0);
        w_.glDisableVertexAttribArray(1);
    }
    selEdgeVao_.release();
}
