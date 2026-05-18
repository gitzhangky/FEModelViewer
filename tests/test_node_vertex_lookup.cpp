#include "NodeVertexLookup.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void testBuildKeepsFirstVertexForEachNode()
{
    std::vector<int> vertexToNode = {10, 20, 10, -1, 30, 20};
    auto lookup = NodeVertexLookup::buildFirstVertexByNode(vertexToNode);

    require(lookup.size() == 3, "lookup contains only valid unique node ids");
    require(lookup.at(10) == 0, "node 10 maps to its first vertex");
    require(lookup.at(20) == 1, "node 20 maps to its first vertex");
    require(lookup.at(30) == 4, "node 30 maps to its first vertex");
    require(lookup.find(-1) == lookup.end(), "negative node ids are ignored");
    printf("  PASS: build keeps first vertex for each node\n");
}

int main()
{
    printf("=== NodeVertexLookup Tests ===\n");
    testBuildKeepsFirstVertexForEachNode();
    printf("All NodeVertexLookup tests passed!\n");
    return 0;
}
