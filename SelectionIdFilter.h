#pragma once

#include <unordered_set>
#include <utility>
#include <vector>

namespace SelectionIdFilter {

inline void addValidId(std::unordered_set<int>& ids, int id)
{
    if (id >= 0) ids.insert(id);
}

inline std::unordered_set<int> buildSelectableElements(
    const std::vector<int>& triangleToElement,
    const std::vector<int>& renderedEdgeToElement)
{
    std::unordered_set<int> ids;
    ids.reserve(triangleToElement.size() + renderedEdgeToElement.size());
    for (int id : triangleToElement)
        addValidId(ids, id);
    for (int id : renderedEdgeToElement)
        addValidId(ids, id);
    return ids;
}

inline std::unordered_set<int> buildSelectableNodes(
    const std::vector<int>& vertexToNode,
    const std::vector<std::pair<int, int>>& renderedEdgeNodeIds)
{
    std::unordered_set<int> ids;
    ids.reserve(vertexToNode.size() + renderedEdgeNodeIds.size() * 2);
    for (int id : vertexToNode)
        addValidId(ids, id);
    for (const auto& edge : renderedEdgeNodeIds) {
        addValidId(ids, edge.first);
        addValidId(ids, edge.second);
    }
    return ids;
}

}
