#pragma once

#include <QRect>
#include <glm/glm.hpp>

#include <algorithm>
#include <unordered_set>

namespace ProjectedRectFilter {

struct RectNdc {
    float left = -1.0f;
    float right = 1.0f;
    float bottom = -1.0f;
    float top = 1.0f;
};

inline RectNdc makeRectNdc(const QRect& rect, int width, int height)
{
    const float safeW = static_cast<float>(std::max(1, width));
    const float safeH = static_cast<float>(std::max(1, height));
    RectNdc out;
    out.left   = (2.0f * rect.left() / safeW) - 1.0f;
    out.right  = (2.0f * rect.right() / safeW) - 1.0f;
    out.top    = 1.0f - (2.0f * rect.top() / safeH);
    out.bottom = 1.0f - (2.0f * rect.bottom() / safeH);
    if (out.left > out.right) std::swap(out.left, out.right);
    if (out.bottom > out.top) std::swap(out.bottom, out.top);
    return out;
}

inline bool containsProjectedPoint(const RectNdc& rect, const glm::mat4& mvp,
                                   const glm::vec3& point)
{
    glm::vec4 clip = mvp * glm::vec4(point, 1.0f);
    if (clip.w <= 0.0f) return false;
    const float x = clip.x / clip.w;
    const float y = clip.y / clip.w;
    return x >= rect.left && x <= rect.right && y >= rect.bottom && y <= rect.top;
}

template <typename CoordMap, typename IsNodeVisible>
void collectNodes(const QRect& rect, int width, int height, const glm::mat4& mvp,
                  const CoordMap& coords, IsNodeVisible isNodeVisible,
                  std::unordered_set<int>& out)
{
    const RectNdc rectNdc = makeRectNdc(rect, width, height);
    for (const auto& item : coords) {
        const int nodeId = item.first;
        if (nodeId < 0 || !isNodeVisible(nodeId)) continue;
        if (containsProjectedPoint(rectNdc, mvp, item.second))
            out.insert(nodeId);
    }
}

template <typename NodeIds, typename CoordMap>
bool elementCenterInside(const RectNdc& rect, const glm::mat4& mvp,
                         const NodeIds& nodeIds, const CoordMap& coords)
{
    glm::vec3 center(0.0f);
    int count = 0;
    for (int nodeId : nodeIds) {
        auto it = coords.find(nodeId);
        if (it == coords.end()) continue;
        center += it->second;
        ++count;
    }
    return count > 0 && containsProjectedPoint(rect, mvp, center / static_cast<float>(count));
}

template <typename ElemToNodes, typename CoordMap, typename IsElementVisible>
void collectElementsByCenter(const QRect& rect, int width, int height, const glm::mat4& mvp,
                             const ElemToNodes& elemToNodes, const CoordMap& coords,
                             IsElementVisible isElementVisible,
                             std::unordered_set<int>& out)
{
    const RectNdc rectNdc = makeRectNdc(rect, width, height);
    for (const auto& item : elemToNodes) {
        const int elemId = item.first;
        if (!isElementVisible(elemId)) continue;
        if (elementCenterInside(rectNdc, mvp, item.second, coords))
            out.insert(elemId);
    }
}

} // namespace ProjectedRectFilter
