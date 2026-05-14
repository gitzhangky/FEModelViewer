/**
 * @file FEPostFilter.cpp
 * @brief 后处理空间过滤器实现
 */

#include "FEPostFilter.h"
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <algorithm>

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

struct ClipVertex {
    float data[6] = {};
    int nodeId = -1;
};

static glm::vec3 clipVertexPos(const ClipVertex& v) {
    return glm::vec3(v.data[0], v.data[1], v.data[2]);
}

static bool sameClipVertexPosition(const ClipVertex& a, const ClipVertex& b) {
    return samePoint(clipVertexPos(a), clipVertexPos(b));
}

static bool isDegenerateTriangle(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c) {
    glm::vec3 ab = clipVertexPos(b) - clipVertexPos(a);
    glm::vec3 ac = clipVertexPos(c) - clipVertexPos(a);
    return glm::length(glm::cross(ab, ac)) <= 1.0e-8f;
}

static ClipVertex interpolateClipVertex(const ClipVertex& a, const ClipVertex& b, float t) {
    ClipVertex out;
    for (int i = 0; i < 6; ++i)
        out.data[i] = a.data[i] + (b.data[i] - a.data[i]) * t;

    glm::vec3 n(out.data[3], out.data[4], out.data[5]);
    float len = glm::length(n);
    if (len > 1.0e-8f) {
        n /= len;
        out.data[3] = n.x;
        out.data[4] = n.y;
        out.data[5] = n.z;
    }
    out.nodeId = -1;
    return out;
}

static float signedDistanceForClip(const ClipVertex& v, const FEPlane& plane) {
    return glm::dot(clipVertexPos(v) - plane.origin, plane.normal);
}

static bool keepDistance(float distance, bool keepPositiveSide) {
    return keepPositiveSide ? (distance >= -kPlaneEps) : (distance <= kPlaneEps);
}

static std::vector<ClipVertex> clipPolygonByPlane(const ClipVertex tri[3],
                                                   const FEPlane& plane,
                                                   bool keepPositiveSide) {
    std::vector<ClipVertex> input{tri[0], tri[1], tri[2]};
    std::vector<ClipVertex> output;
    output.reserve(4);

    for (int i = 0; i < 3; ++i) {
        const ClipVertex& current = input[i];
        const ClipVertex& next = input[(i + 1) % 3];
        float currentDist = signedDistanceForClip(current, plane);
        float nextDist = signedDistanceForClip(next, plane);
        bool currentInside = keepDistance(currentDist, keepPositiveSide);
        bool nextInside = keepDistance(nextDist, keepPositiveSide);

        if (currentInside && nextInside) {
            output.push_back(next);
        } else if (currentInside && !nextInside) {
            float t = currentDist / (currentDist - nextDist);
            output.push_back(interpolateClipVertex(current, next, t));
        } else if (!currentInside && nextInside) {
            float t = currentDist / (currentDist - nextDist);
            output.push_back(interpolateClipVertex(current, next, t));
            output.push_back(next);
        }
    }

    std::vector<ClipVertex> compact;
    compact.reserve(output.size());
    for (const ClipVertex& v : output) {
        if (compact.empty() || !sameClipVertexPosition(compact.back(), v))
            compact.push_back(v);
    }
    if (compact.size() > 1 && sameClipVertexPosition(compact.front(), compact.back()))
        compact.pop_back();

    return compact;
}

static unsigned int appendClipVertex(FERenderData& result, const ClipVertex& v) {
    unsigned int newIndex = static_cast<unsigned int>(result.mesh.vertices.size() / 6);
    for (float value : v.data)
        result.mesh.vertices.push_back(value);
    result.vertexToNode.push_back(v.nodeId);
    return newIndex;
}

static void rebuildTriangleEdges(FERenderData& result) {
    result.mesh.edgeVertices.clear();
    result.mesh.edgeIndices.clear();
    result.edgeToPart.clear();

    struct EdgeKey {
        unsigned int a;
        unsigned int b;
        bool operator==(const EdgeKey& other) const {
            return a == other.a && b == other.b;
        }
    };
    struct EdgeHash {
        size_t operator()(const EdgeKey& edge) const {
            size_t h = std::hash<unsigned int>{}(edge.a);
            h ^= std::hash<unsigned int>{}(edge.b) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    std::unordered_set<EdgeKey, EdgeHash> seen;
    int triCount = result.triangleCount();
    for (int ti = 0; ti < triCount; ++ti) {
        unsigned int tri[3] = {
            result.mesh.indices[ti * 3],
            result.mesh.indices[ti * 3 + 1],
            result.mesh.indices[ti * 3 + 2],
        };
        for (int e = 0; e < 3; ++e) {
            unsigned int va = tri[e];
            unsigned int vb = tri[(e + 1) % 3];
            EdgeKey key{std::min(va, vb), std::max(va, vb)};
            if (seen.find(key) != seen.end()) continue;
            seen.insert(key);

            unsigned int edgeIndex = static_cast<unsigned int>(result.mesh.edgeVertices.size() / 3);
            for (unsigned int v : {va, vb}) {
                int base = static_cast<int>(v) * 6;
                result.mesh.edgeVertices.push_back(result.mesh.vertices[base]);
                result.mesh.edgeVertices.push_back(result.mesh.vertices[base + 1]);
                result.mesh.edgeVertices.push_back(result.mesh.vertices[base + 2]);
            }
            result.mesh.edgeIndices.push_back(edgeIndex);
            result.mesh.edgeIndices.push_back(edgeIndex + 1);

            if (ti < static_cast<int>(result.triangleToPart.size()))
                result.edgeToPart.push_back(result.triangleToPart[ti]);
            else
                result.edgeToPart.push_back(-1);
        }
    }
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

    int vertexStride = 6;

    for (int ti = 0; ti < triCount; ++ti) {
        ClipVertex tri[3];
        for (int k = 0; k < 3; ++k) {
            unsigned int oldIdx = input.mesh.indices[ti * 3 + k];
            int base = static_cast<int>(oldIdx) * vertexStride;
            for (int j = 0; j < vertexStride; ++j)
                tri[k].data[j] = input.mesh.vertices[base + j];
            if (oldIdx < input.vertexToNode.size())
                tri[k].nodeId = input.vertexToNode[oldIdx];
        }

        std::vector<ClipVertex> clipped = clipPolygonByPlane(tri, plane, keepPositiveSide);
        if (clipped.size() < 3) continue;

        int elemId = (ti < static_cast<int>(input.triangleToElement.size()))
            ? input.triangleToElement[ti]
            : -1;
        int faceId = (ti < static_cast<int>(input.triangleToFace.size()))
            ? input.triangleToFace[ti]
            : 0;
        int partId = (ti < static_cast<int>(input.triangleToPart.size()))
            ? input.triangleToPart[ti]
            : -1;

        unsigned int root = 0;
        bool rootCreated = false;
        for (std::size_t i = 1; i + 1 < clipped.size(); ++i) {
            if (isDegenerateTriangle(clipped[0], clipped[i], clipped[i + 1]))
                continue;

            if (!rootCreated) {
                root = appendClipVertex(result, clipped[0]);
                rootCreated = true;
            }
            unsigned int b = appendClipVertex(result, clipped[i]);
            unsigned int c = appendClipVertex(result, clipped[i + 1]);

            result.mesh.indices.push_back(root);
            result.mesh.indices.push_back(b);
            result.mesh.indices.push_back(c);
            result.triangleToElement.push_back(elemId);
            result.triangleToFace.push_back(faceId);
            result.triangleToPart.push_back(partId);
        }
    }

    rebuildTriangleEdges(result);
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
