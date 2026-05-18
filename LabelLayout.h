#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LabelLayout {

inline long long binKey(double x, double y, int binPx)
{
    int safeBin = std::max(1, binPx);
    int bx = static_cast<int>(std::floor(x / static_cast<double>(safeBin)));
    int by = static_cast<int>(std::floor(y / static_cast<double>(safeBin)));
    return (static_cast<long long>(bx) << 32) ^ static_cast<unsigned int>(by);
}

inline int nextStackOffset(double x,
                           double y,
                           int binPx,
                           int lineHeight,
                           std::unordered_map<long long, int>& binCounts)
{
    long long key = binKey(x, y, binPx);
    int stackIndex = binCounts[key]++;
    return stackIndex * (std::max(1, lineHeight) + 2);
}

inline std::vector<int> stablePriorityOrder(const std::vector<int>& currentIds,
                                            const std::vector<int>& previousIds)
{
    std::unordered_set<int> currentSet(currentIds.begin(), currentIds.end());
    std::unordered_set<int> emitted;
    std::vector<int> ordered;
    ordered.reserve(currentIds.size());

    for (int id : previousIds) {
        if (currentSet.count(id) && emitted.insert(id).second)
            ordered.push_back(id);
    }

    std::vector<int> remaining;
    remaining.reserve(currentIds.size());
    for (int id : currentIds) {
        if (emitted.count(id) == 0)
            remaining.push_back(id);
    }
    std::sort(remaining.begin(), remaining.end());
    remaining.erase(std::unique(remaining.begin(), remaining.end()), remaining.end());
    ordered.insert(ordered.end(), remaining.begin(), remaining.end());
    return ordered;
}

} // namespace LabelLayout
