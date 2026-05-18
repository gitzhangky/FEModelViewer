#pragma once

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VisibilityFilter {

struct TriangleFilterResult {
    std::vector<unsigned int> indices;
    std::vector<float> triParts;
};

inline bool isPartVisible(int partIndex,
                          const std::unordered_map<int, bool>& partVisibility)
{
    auto it = partVisibility.find(partIndex);
    return it == partVisibility.end() || it->second;
}

inline bool isElementHiddenByNode(
    int elemId,
    const std::unordered_set<int>& hiddenNodes,
    const std::unordered_map<int, std::unordered_set<int>>& elemToNodes)
{
    if (hiddenNodes.empty()) return false;
    auto it = elemToNodes.find(elemId);
    if (it == elemToNodes.end()) return false;
    for (int nid : it->second)
        if (hiddenNodes.count(nid)) return true;
    return false;
}

inline bool isElementVisible(
    int elemId,
    const std::unordered_set<int>& hiddenElements,
    const std::unordered_set<int>& hiddenNodes,
    const std::unordered_map<int, std::unordered_set<int>>& elemToNodes)
{
    if (hiddenElements.count(elemId)) return false;
    return !isElementHiddenByNode(elemId, hiddenNodes, elemToNodes);
}

inline TriangleFilterResult filterTriangles(
    const std::vector<unsigned int>& allTriIndices,
    const std::vector<int>& triToElem,
    const std::vector<int>& triToPart,
    const std::vector<int>& vertexToNode,
    const std::unordered_map<int, bool>& partVisibility,
    const std::unordered_set<int>& hiddenElements,
    const std::unordered_set<int>& hiddenNodes,
    const std::unordered_map<int, std::unordered_set<int>>& elemToNodes)
{
    TriangleFilterResult result;
    result.indices.reserve(allTriIndices.size());
    result.triParts.reserve(allTriIndices.size() / 3);

    int triCount = static_cast<int>(allTriIndices.size() / 3);
    for (int t = 0; t < triCount; ++t) {
        int part = (t < static_cast<int>(triToPart.size())) ? triToPart[t] : -1;
        if (part >= 0 && !isPartVisible(part, partVisibility))
            continue;

        int elemId = (t < static_cast<int>(triToElem.size())) ? triToElem[t] : -1;
        if (elemId >= 0 && !isElementVisible(elemId, hiddenElements, hiddenNodes, elemToNodes))
            continue;

        // 无单元映射时退回到三角形顶点节点过滤。
        if (elemId < 0 && !hiddenNodes.empty()) {
            bool hiddenByVertex = false;
            for (int k = 0; k < 3; ++k) {
                unsigned int vi = allTriIndices[t * 3 + k];
                if (vi < vertexToNode.size() && hiddenNodes.count(vertexToNode[vi])) {
                    hiddenByVertex = true;
                    break;
                }
            }
            if (hiddenByVertex) continue;
        }

        result.indices.push_back(allTriIndices[t * 3]);
        result.indices.push_back(allTriIndices[t * 3 + 1]);
        result.indices.push_back(allTriIndices[t * 3 + 2]);
        result.triParts.push_back(static_cast<float>(part));
    }

    return result;
}

inline std::vector<unsigned int> filterEdges(
    const std::vector<unsigned int>& allEdgeIndices,
    const std::vector<int>& edgeToPart,
    const std::vector<std::vector<int>>& edgeToElements,
    const std::vector<std::pair<int, int>>& edgeNodeIds,
    const std::unordered_map<int, bool>& partVisibility,
    const std::unordered_set<int>& hiddenElements,
    const std::unordered_set<int>& hiddenNodes,
    const std::unordered_map<int, std::unordered_set<int>>& elemToNodes)
{
    std::vector<unsigned int> filtered;
    filtered.reserve(allEdgeIndices.size());

    int edgeCount = static_cast<int>(allEdgeIndices.size() / 2);
    for (int e = 0; e < edgeCount; ++e) {
        int part = (e < static_cast<int>(edgeToPart.size())) ? edgeToPart[e] : -1;
        if (part >= 0 && !isPartVisible(part, partVisibility))
            continue;

        if (e < static_cast<int>(edgeToElements.size()) && !edgeToElements[e].empty()) {
            bool anyVisible = false;
            for (int elemId : edgeToElements[e]) {
                if (isElementVisible(elemId, hiddenElements, hiddenNodes, elemToNodes)) {
                    anyVisible = true;
                    break;
                }
            }
            if (!anyVisible) continue;
        }

        if (!hiddenNodes.empty() && e < static_cast<int>(edgeNodeIds.size())) {
            auto [a, b] = edgeNodeIds[e];
            if (hiddenNodes.count(a) || hiddenNodes.count(b))
                continue;
        }

        filtered.push_back(allEdgeIndices[e * 2]);
        filtered.push_back(allEdgeIndices[e * 2 + 1]);
    }

    return filtered;
}

inline std::vector<unsigned int> filterEdges(
    const std::vector<unsigned int>& allEdgeIndices,
    const std::vector<int>& edgeToPart,
    const std::vector<int>& edgeToElement,
    const std::vector<std::pair<int, int>>& edgeNodeIds,
    const std::unordered_map<int, bool>& partVisibility,
    const std::unordered_set<int>& hiddenElements,
    const std::unordered_set<int>& hiddenNodes,
    const std::unordered_map<int, std::unordered_set<int>>& elemToNodes)
{
    std::vector<unsigned int> filtered;
    filtered.reserve(allEdgeIndices.size());

    int edgeCount = static_cast<int>(allEdgeIndices.size() / 2);
    for (int e = 0; e < edgeCount; ++e) {
        int part = (e < static_cast<int>(edgeToPart.size())) ? edgeToPart[e] : -1;
        if (part >= 0 && !isPartVisible(part, partVisibility))
            continue;

        int elemId = (e < static_cast<int>(edgeToElement.size())) ? edgeToElement[e] : -1;
        if (elemId >= 0 && !isElementVisible(elemId, hiddenElements, hiddenNodes, elemToNodes))
            continue;

        if (!hiddenNodes.empty() && e < static_cast<int>(edgeNodeIds.size())) {
            auto [a, b] = edgeNodeIds[e];
            if (hiddenNodes.count(a) || hiddenNodes.count(b))
                continue;
        }

        filtered.push_back(allEdgeIndices[e * 2]);
        filtered.push_back(allEdgeIndices[e * 2 + 1]);
    }

    return filtered;
}

} // namespace VisibilityFilter
