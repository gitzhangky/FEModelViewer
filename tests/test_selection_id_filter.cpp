#include "SelectionIdFilter.h"

#include <cassert>
#include <cstdio>
#include <vector>

static void selectableElementsUseRenderedTrianglesAndEdgesOnly()
{
    std::vector<int> triangleToElement = {1, 2, 2};
    std::vector<int> renderedEdgeToElement = {7};

    auto selectable = SelectionIdFilter::buildSelectableElements(triangleToElement,
                                                                 renderedEdgeToElement);

    assert(selectable.count(1) == 1);
    assert(selectable.count(2) == 1);
    assert(selectable.count(7) == 1);
    assert(selectable.count(3) == 0);
    printf("  PASS: selectable elements only come from rendered triangles and edges\n");
}

static void selectableNodesIncludeRenderedTriangleAndLineNodes()
{
    std::vector<int> vertexToNode = {10, 20, 20, -1};
    std::vector<std::pair<int, int>> renderedEdgeNodeIds = {{30, 40}, {-1, 50}};

    auto selectable = SelectionIdFilter::buildSelectableNodes(vertexToNode,
                                                              renderedEdgeNodeIds);

    assert(selectable.count(10) == 1);
    assert(selectable.count(20) == 1);
    assert(selectable.count(30) == 1);
    assert(selectable.count(40) == 1);
    assert(selectable.count(50) == 1);
    assert(selectable.count(-1) == 0);
    printf("  PASS: selectable nodes include rendered triangle and line nodes\n");
}

int main()
{
    printf("=== SelectionIdFilter Tests ===\n");
    selectableElementsUseRenderedTrianglesAndEdgesOnly();
    selectableNodesIncludeRenderedTriangleAndLineNodes();
    printf("All SelectionIdFilter tests passed!\n");
    return 0;
}
