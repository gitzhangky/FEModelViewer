/**
 * @file FEPostFilter.cpp
 * @brief 后处理空间过滤器实现
 */

#include "FEPostFilter.h"
#include <unordered_map>
#include <unordered_set>
#include <cmath>

static constexpr float kPlaneEps = 1e-7f;

static bool samePoint(const glm::vec3& a, const glm::vec3& b) {
    glm::vec3 d = a - b;
    return glm::dot(d, d) <= kPlaneEps * kPlaneEps;
}

static void appendUniquePoint(std::vector<glm::vec3>& points, const glm::vec3& p) {
    for (const glm::vec3& existing : points) {
        if (samePoint(existing, p)) return;
    }
    points.push_back(p);
}

static void appendSliceLine(FESliceResult& result, const glm::vec3& a, const glm::vec3& b) {
    if (samePoint(a, b)) return;
    result.lineVertices.push_back(a.x);
    result.lineVertices.push_back(a.y);
    result.lineVertices.push_back(a.z);
    result.lineVertices.push_back(b.x);
    result.lineVertices.push_back(b.y);
    result.lineVertices.push_back(b.z);
    result.lineCount++;
}

// 过滤边线数据：保留中点在平面正/负侧的边
static void filterEdgesByPlane(const FERenderData& input, FERenderData& result,
                               const FEPlane& plane, bool keepPositiveSide) {
    int edgeCount = static_cast<int>(input.mesh.edgeIndices.size() / 2);
    std::unordered_map<unsigned int, unsigned int> edgeVertRemap;

    for (int ei = 0; ei < edgeCount; ++ei) {
        unsigned int oldV0 = input.mesh.edgeIndices[ei * 2];
        unsigned int oldV1 = input.mesh.edgeIndices[ei * 2 + 1];
        glm::vec3 p0(input.mesh.edgeVertices[oldV0 * 3],
                     input.mesh.edgeVertices[oldV0 * 3 + 1],
                     input.mesh.edgeVertices[oldV0 * 3 + 2]);
        glm::vec3 p1(input.mesh.edgeVertices[oldV1 * 3],
                     input.mesh.edgeVertices[oldV1 * 3 + 1],
                     input.mesh.edgeVertices[oldV1 * 3 + 2]);
        glm::vec3 mid = (p0 + p1) * 0.5f;

        float dist = glm::dot(mid - plane.origin, plane.normal);
        if ((dist >= 0.0f) != keepPositiveSide) continue;

        for (int k = 0; k < 2; ++k) {
            unsigned int oldV = input.mesh.edgeIndices[ei * 2 + k];
            auto it = edgeVertRemap.find(oldV);
            if (it != edgeVertRemap.end()) {
                result.mesh.edgeIndices.push_back(it->second);
            } else {
                unsigned int newV = static_cast<unsigned int>(result.mesh.edgeVertices.size() / 3);
                int base = oldV * 3;
                for (int j = 0; j < 3; ++j)
                    result.mesh.edgeVertices.push_back(input.mesh.edgeVertices[base + j]);
                edgeVertRemap[oldV] = newV;
                result.mesh.edgeIndices.push_back(newV);
            }
        }
        if (ei < static_cast<int>(input.edgeToPart.size()))
            result.edgeToPart.push_back(input.edgeToPart[ei]);
        else
            result.edgeToPart.push_back(-1);
    }
}

// 过滤边线数据：保留端点均在保留顶点集合中的边
static void filterEdgesByKeptVertices(const FERenderData& input, FERenderData& result,
                                      const std::unordered_map<unsigned int, unsigned int>& triVertRemap) {
    // 收集所有保留的三角形顶点的 3D 位置（精确匹配，因为来自相同 FENode）
    struct Vec3Hash {
        size_t operator()(const glm::vec3& v) const {
            size_t h = 0;
            h ^= std::hash<float>{}(v.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>{}(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<float>{}(v.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_set<glm::vec3, Vec3Hash> keptPositions;
    for (const auto& [oldIdx, newIdx] : triVertRemap) {
        int base = oldIdx * 6;
        keptPositions.insert(glm::vec3(input.mesh.vertices[base],
                                       input.mesh.vertices[base + 1],
                                       input.mesh.vertices[base + 2]));
    }

    int edgeCount = static_cast<int>(input.mesh.edgeIndices.size() / 2);
    std::unordered_map<unsigned int, unsigned int> edgeVertRemap;

    for (int ei = 0; ei < edgeCount; ++ei) {
        unsigned int oldV0 = input.mesh.edgeIndices[ei * 2];
        unsigned int oldV1 = input.mesh.edgeIndices[ei * 2 + 1];
        glm::vec3 p0(input.mesh.edgeVertices[oldV0 * 3],
                     input.mesh.edgeVertices[oldV0 * 3 + 1],
                     input.mesh.edgeVertices[oldV0 * 3 + 2]);
        glm::vec3 p1(input.mesh.edgeVertices[oldV1 * 3],
                     input.mesh.edgeVertices[oldV1 * 3 + 1],
                     input.mesh.edgeVertices[oldV1 * 3 + 2]);
        if (keptPositions.find(p0) == keptPositions.end() ||
            keptPositions.find(p1) == keptPositions.end())
            continue;

        for (int k = 0; k < 2; ++k) {
            unsigned int oldV = input.mesh.edgeIndices[ei * 2 + k];
            auto it = edgeVertRemap.find(oldV);
            if (it != edgeVertRemap.end()) {
                result.mesh.edgeIndices.push_back(it->second);
            } else {
                unsigned int newV = static_cast<unsigned int>(result.mesh.edgeVertices.size() / 3);
                int base = oldV * 3;
                for (int j = 0; j < 3; ++j)
                    result.mesh.edgeVertices.push_back(input.mesh.edgeVertices[base + j]);
                edgeVertRemap[oldV] = newV;
                result.mesh.edgeIndices.push_back(newV);
            }
        }
        if (ei < static_cast<int>(input.edgeToPart.size()))
            result.edgeToPart.push_back(input.edgeToPart[ei]);
        else
            result.edgeToPart.push_back(-1);
    }
}

// 过滤单元完整边线（用于选中高亮）：只保留属于保留单元的边
static void filterElemEdges(const FERenderData& input, FERenderData& result) {
    std::unordered_set<int> keptElements(result.triangleToElement.begin(),
                                          result.triangleToElement.end());
    int elemEdgeCount = static_cast<int>(input.mesh.elemEdgeToElement.size());
    for (int ei = 0; ei < elemEdgeCount; ++ei) {
        int elemId = input.mesh.elemEdgeToElement[ei];
        if (keptElements.find(elemId) == keptElements.end()) continue;
        int base = ei * 6;
        for (int j = 0; j < 6; ++j)
            result.mesh.elemEdgeVertices.push_back(input.mesh.elemEdgeVertices[base + j]);
        result.mesh.elemEdgeToElement.push_back(elemId);
        if (ei < static_cast<int>(input.mesh.elemEdgeNodeIds.size()))
            result.mesh.elemEdgeNodeIds.push_back(input.mesh.elemEdgeNodeIds[ei]);
    }
}

FERenderData FEPostFilter::thresholdByElementValue(const FERenderData& input,
                                                    const FEScalarField& field,
                                                    float minValue,
                                                    float maxValue) {
    FERenderData result;
    int triCount = input.triangleCount();
    if (triCount == 0) return result;

    // 旧渲染顶点索引 → 新渲染顶点索引
    std::unordered_map<unsigned int, unsigned int> vertexRemap;
    int vertexStride = 6;  // 位置(3) + 法线(3)

    for (int ti = 0; ti < triCount; ++ti) {
        int elemId = input.elementAtTriangle(ti);
        auto it = field.values.find(elemId);
        if (it == field.values.end()) continue;

        float val = it->second;
        if (val < minValue || val > maxValue) continue;

        // 保留此三角形
        unsigned int oldIdx[3];
        for (int k = 0; k < 3; ++k)
            oldIdx[k] = input.mesh.indices[ti * 3 + k];

        unsigned int newIdx[3];
        for (int k = 0; k < 3; ++k) {
            auto vit = vertexRemap.find(oldIdx[k]);
            if (vit != vertexRemap.end()) {
                newIdx[k] = vit->second;
            } else {
                unsigned int newVi = static_cast<unsigned int>(result.mesh.vertices.size() / vertexStride);
                int base = oldIdx[k] * vertexStride;
                for (int j = 0; j < vertexStride; ++j)
                    result.mesh.vertices.push_back(input.mesh.vertices[base + j]);

                if (oldIdx[k] < input.vertexToNode.size())
                    result.vertexToNode.push_back(input.vertexToNode[oldIdx[k]]);
                else
                    result.vertexToNode.push_back(-1);

                vertexRemap[oldIdx[k]] = newVi;
                newIdx[k] = newVi;
            }
        }

        result.mesh.indices.push_back(newIdx[0]);
        result.mesh.indices.push_back(newIdx[1]);
        result.mesh.indices.push_back(newIdx[2]);
        result.triangleToElement.push_back(elemId);

        if (ti < static_cast<int>(input.triangleToFace.size()))
            result.triangleToFace.push_back(input.triangleToFace[ti]);
        else
            result.triangleToFace.push_back(0);

        if (ti < static_cast<int>(input.triangleToPart.size()))
            result.triangleToPart.push_back(input.triangleToPart[ti]);
        else
            result.triangleToPart.push_back(-1);
    }

    filterEdgesByKeptVertices(input, result, vertexRemap);
    filterElemEdges(input, result);

    return result;
}

FERenderData FEPostFilter::clipByPlane(const FERenderData& input,
                                        const FEPlane& plane,
                                        bool keepPositiveSide) {
    FERenderData result;
    int triCount = input.triangleCount();
    if (triCount == 0) return result;

    std::unordered_map<unsigned int, unsigned int> vertexRemap;
    int vertexStride = 6;

    for (int ti = 0; ti < triCount; ++ti) {
        // 计算三角形质心
        glm::vec3 centroid{0.0f};
        unsigned int oldIdx[3];
        for (int k = 0; k < 3; ++k) {
            oldIdx[k] = input.mesh.indices[ti * 3 + k];
            int base = oldIdx[k] * vertexStride;
            centroid.x += input.mesh.vertices[base + 0];
            centroid.y += input.mesh.vertices[base + 1];
            centroid.z += input.mesh.vertices[base + 2];
        }
        centroid /= 3.0f;

        float dist = glm::dot(centroid - plane.origin, plane.normal);
        bool onPositiveSide = (dist >= 0.0f);
        if (onPositiveSide != keepPositiveSide) continue;

        // 保留此三角形
        unsigned int newIdx[3];
        for (int k = 0; k < 3; ++k) {
            auto vit = vertexRemap.find(oldIdx[k]);
            if (vit != vertexRemap.end()) {
                newIdx[k] = vit->second;
            } else {
                unsigned int newVi = static_cast<unsigned int>(result.mesh.vertices.size() / vertexStride);
                int base = oldIdx[k] * vertexStride;
                for (int j = 0; j < vertexStride; ++j)
                    result.mesh.vertices.push_back(input.mesh.vertices[base + j]);

                if (oldIdx[k] < input.vertexToNode.size())
                    result.vertexToNode.push_back(input.vertexToNode[oldIdx[k]]);
                else
                    result.vertexToNode.push_back(-1);

                vertexRemap[oldIdx[k]] = newVi;
                newIdx[k] = newVi;
            }
        }

        result.mesh.indices.push_back(newIdx[0]);
        result.mesh.indices.push_back(newIdx[1]);
        result.mesh.indices.push_back(newIdx[2]);

        if (ti < static_cast<int>(input.triangleToElement.size()))
            result.triangleToElement.push_back(input.triangleToElement[ti]);
        else
            result.triangleToElement.push_back(-1);

        if (ti < static_cast<int>(input.triangleToFace.size()))
            result.triangleToFace.push_back(input.triangleToFace[ti]);
        else
            result.triangleToFace.push_back(0);

        if (ti < static_cast<int>(input.triangleToPart.size()))
            result.triangleToPart.push_back(input.triangleToPart[ti]);
        else
            result.triangleToPart.push_back(-1);
    }

    filterEdgesByPlane(input, result, plane, keepPositiveSide);
    filterElemEdges(input, result);

    return result;
}

FESliceResult FEPostFilter::sliceByPlane(const FERenderData& input,
                                          const FEPlane& plane) {
    FESliceResult result;
    int triCount = input.triangleCount();
    if (triCount == 0) return result;

    int vertexStride = 6;

    for (int ti = 0; ti < triCount; ++ti) {
        glm::vec3 v[3];
        for (int k = 0; k < 3; ++k) {
            unsigned int vi = input.mesh.indices[ti * 3 + k];
            int base = vi * vertexStride;
            v[k] = glm::vec3(input.mesh.vertices[base],
                             input.mesh.vertices[base + 1],
                             input.mesh.vertices[base + 2]);
        }

        float d[3];
        for (int k = 0; k < 3; ++k)
            d[k] = glm::dot(v[k] - plane.origin, plane.normal);

        int onPlaneCount = 0;
        for (int k = 0; k < 3; ++k) {
            if (std::abs(d[k]) <= kPlaneEps) ++onPlaneCount;
        }

        // 三角形整体落在切片平面上时，绘制轮廓线。
        if (onPlaneCount == 3) {
            appendSliceLine(result, v[0], v[1]);
            appendSliceLine(result, v[1], v[2]);
            appendSliceLine(result, v[2], v[0]);
            continue;
        }

        // 找平面与三角形的交点，并去重；单点相切不会形成线段。
        std::vector<glm::vec3> intersections;
        intersections.reserve(3);

        for (int k = 0; k < 3; ++k) {
            int next = (k + 1) % 3;
            bool currentOnPlane = std::abs(d[k]) <= kPlaneEps;
            bool nextOnPlane = std::abs(d[next]) <= kPlaneEps;

            if (currentOnPlane && nextOnPlane) {
                appendUniquePoint(intersections, v[k]);
                appendUniquePoint(intersections, v[next]);
            } else if (currentOnPlane) {
                appendUniquePoint(intersections, v[k]);
            } else if (nextOnPlane) {
                appendUniquePoint(intersections, v[next]);
            } else if ((d[k] > 0.0f) != (d[next] > 0.0f)) {
                float t = d[k] / (d[k] - d[next]);
                appendUniquePoint(intersections, v[k] + t * (v[next] - v[k]));
            }
        }

        if (intersections.size() == 2) {
            appendSliceLine(result, intersections[0], intersections[1]);
        }
    }

    return result;
}
