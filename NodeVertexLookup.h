#pragma once

#include <unordered_map>
#include <vector>

namespace NodeVertexLookup {

inline std::unordered_map<int, int> buildFirstVertexByNode(const std::vector<int>& vertexToNode)
{
    std::unordered_map<int, int> lookup;
    lookup.reserve(vertexToNode.size());
    for (int i = 0; i < static_cast<int>(vertexToNode.size()); ++i) {
        int nodeId = vertexToNode[i];
        if (nodeId >= 0 && lookup.find(nodeId) == lookup.end())
            lookup[nodeId] = i;
    }
    return lookup;
}

} // namespace NodeVertexLookup
