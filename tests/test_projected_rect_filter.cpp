#include "ProjectedRectFilter.h"

#include <QRect>
#include <glm/glm.hpp>
#include <cassert>
#include <unordered_map>
#include <unordered_set>

static void visibleNodesInsideRectAreCollected()
{
    QRect rect(40, 40, 20, 20);
    glm::mat4 mvp(1.0f);
    std::unordered_map<int, glm::vec3> coords = {
        {1, { 0.0f,  0.0f, 0.0f}},
        {2, { 0.1f,  0.0f, 0.0f}},
        {3, { 0.8f,  0.0f, 0.0f}},
        {4, { 0.0f,  0.0f, 0.0f}},
    };
    auto visible = [](int nodeId) { return nodeId != 4; };

    std::unordered_set<int> out;
    ProjectedRectFilter::collectNodes(rect, 100, 100, mvp, coords, visible, out);

    assert(out.count(1) == 1);
    assert(out.count(2) == 1);
    assert(out.count(3) == 0);
    assert(out.count(4) == 0);
}

static void visibleElementCentersInsideRectAreCollected()
{
    QRect rect(40, 40, 20, 20);
    glm::mat4 mvp(1.0f);
    std::unordered_map<int, glm::vec3> coords = {
        {1, {-0.1f,  0.0f, 0.0f}},
        {2, { 0.1f,  0.0f, 0.0f}},
        {3, { 0.0f,  0.1f, 0.0f}},
        {4, { 0.8f,  0.0f, 0.0f}},
        {5, { 0.9f,  0.0f, 0.0f}},
        {6, { 0.8f,  0.1f, 0.0f}},
        {7, {-0.1f, -0.1f, 0.0f}},
        {8, { 0.1f, -0.1f, 0.0f}},
        {9, { 0.0f, -0.2f, 0.0f}},
    };
    std::unordered_map<int, std::unordered_set<int>> elemToNodes = {
        {10, {1, 2, 3}},   // 可见且中心在框内
        {20, {4, 5, 6}},   // 可见但中心在框外
        {30, {7, 8, 9}},   // 中心在框内但隐藏
    };
    auto visible = [](int elemId) { return elemId != 30; };

    std::unordered_set<int> out;
    ProjectedRectFilter::collectElementsByCenter(rect, 100, 100, mvp,
                                                  elemToNodes, coords, visible, out);

    assert(out.count(10) == 1);
    assert(out.count(20) == 0);
    assert(out.count(30) == 0);
}

int main()
{
    visibleNodesInsideRectAreCollected();
    visibleElementCentersInsideRectAreCollected();
    return 0;
}
