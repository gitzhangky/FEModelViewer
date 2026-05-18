#include "VisibilityFilter.h"

#include <cassert>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>

static void hiddenElementFiltersItsTriangles()
{
    std::vector<unsigned int> indices = {
        0, 1, 2,
        2, 3, 0,
    };
    std::vector<int> triToElem = {100, 200};
    std::vector<int> triToPart = {0, 0};
    std::vector<int> vertexToNode = {1, 2, 3, 4};
    std::unordered_map<int, bool> partVisibility;
    std::unordered_set<int> hiddenElements = {200};
    std::unordered_set<int> hiddenNodes;
    std::unordered_map<int, std::unordered_set<int>> elemToNodes = {
        {100, {1, 2, 3}},
        {200, {1, 3, 4}},
    };

    auto result = VisibilityFilter::filterTriangles(indices, triToElem, triToPart,
                                                    vertexToNode, partVisibility,
                                                    hiddenElements, hiddenNodes,
                                                    elemToNodes);

    assert(result.indices.size() == 3);
    assert(result.indices[0] == 0);
    assert(result.indices[1] == 1);
    assert(result.indices[2] == 2);
    assert(result.triParts.size() == 1);
    assert(result.triParts[0] == 0);
    printf("  PASS: hidden element filters its triangles\n");
}

static void hiddenNodeFiltersConnectedElements()
{
    std::vector<unsigned int> indices = {
        0, 1, 2,
        2, 3, 0,
        4, 5, 6,
    };
    std::vector<int> triToElem = {100, 100, 300};
    std::vector<int> triToPart = {0, 0, 1};
    std::vector<int> vertexToNode = {1, 2, 3, 4, 5, 6, 7};
    std::unordered_map<int, bool> partVisibility;
    std::unordered_set<int> hiddenElements;
    std::unordered_set<int> hiddenNodes = {4};
    std::unordered_map<int, std::unordered_set<int>> elemToNodes = {
        {100, {1, 2, 3, 4}},
        {300, {5, 6, 7}},
    };

    auto result = VisibilityFilter::filterTriangles(indices, triToElem, triToPart,
                                                    vertexToNode, partVisibility,
                                                    hiddenElements, hiddenNodes,
                                                    elemToNodes);

    assert(result.indices.size() == 3);
    assert(result.indices[0] == 4);
    assert(result.indices[1] == 5);
    assert(result.indices[2] == 6);
    assert(result.triParts.size() == 1);
    assert(result.triParts[0] == 1);
    printf("  PASS: hidden node filters connected elements\n");
}

static void partElementAndNodeVisibilityCombineForEdges()
{
    std::vector<unsigned int> edgeIndices = {
        0, 1,  // visible
        1, 2,  // hidden by element
        2, 3,  // hidden by node
        4, 5,  // hidden by part
    };
    std::vector<int> edgeToPart = {0, 0, 0, 1};
    std::vector<int> edgeToElem = {10, 20, 30, 40};
    std::vector<std::pair<int, int>> edgeNodeIds = {{1, 2}, {2, 3}, {3, 4}, {5, 6}};
    std::unordered_map<int, bool> partVisibility = {{1, false}};
    std::unordered_set<int> hiddenElements = {20};
    std::unordered_set<int> hiddenNodes = {4};
    std::unordered_map<int, std::unordered_set<int>> elemToNodes = {
        {10, {1, 2}},
        {20, {2, 3}},
        {30, {3, 4}},
        {40, {5, 6}},
    };

    auto result = VisibilityFilter::filterEdges(edgeIndices, edgeToPart, edgeToElem,
                                                edgeNodeIds, partVisibility,
                                                hiddenElements, hiddenNodes,
                                                elemToNodes);

    assert(result.size() == 2);
    assert(result[0] == 0);
    assert(result[1] == 1);
    printf("  PASS: part element and node visibility combine for edges\n");
}

int main()
{
    printf("=== VisibilityFilter Tests ===\n");
    hiddenElementFiltersItsTriangles();
    hiddenNodeFiltersConnectedElements();
    partElementAndNodeVisibilityCombineForEdges();
    printf("All VisibilityFilter tests passed!\n");
    return 0;
}
