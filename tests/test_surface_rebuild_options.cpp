#include "FEModel.h"
#include "FEMeshConverter.h"

#include <cassert>
#include <vector>

static FEModel makeHexModel()
{
    FEModel model;
    model.addNode(1, {0.0f, 0.0f, 0.0f});
    model.addNode(2, {1.0f, 0.0f, 0.0f});
    model.addNode(3, {1.0f, 1.0f, 0.0f});
    model.addNode(4, {0.0f, 1.0f, 0.0f});
    model.addNode(5, {0.0f, 0.0f, 1.0f});
    model.addNode(6, {1.0f, 0.0f, 1.0f});
    model.addNode(7, {1.0f, 1.0f, 1.0f});
    model.addNode(8, {0.0f, 1.0f, 1.0f});
    model.addElement(10, ElementType::HEX8, {1, 2, 3, 4, 5, 6, 7, 8});
    return model;
}

int main()
{
    FEModel model = makeHexModel();
    std::vector<int> elems = {10};
    FESurfaceCache cache = FEMeshConverter::buildSurfaceCache(model, elems);

    FERenderData full = FEMeshConverter::buildRenderData(cache, nullptr);
    assert(!full.mesh.indices.empty());
    assert(full.mesh.edgeNodeIds.size() == full.mesh.edgeIndices.size() / 2);
    assert(!full.mesh.elemEdgeVertices.empty());
    assert(!full.mesh.elemEdgeToElement.empty());
    assert(!full.mesh.elemEdgeNodeIds.empty());

    FERenderData light = FEMeshConverter::buildRenderData(cache, nullptr, nullptr, false);
    assert(!light.mesh.indices.empty());
    assert(light.mesh.edgeNodeIds.size() == light.mesh.edgeIndices.size() / 2);
    assert(light.mesh.elemEdgeVertices.empty());
    assert(light.mesh.elemEdgeToElement.empty());
    assert(light.mesh.elemEdgeNodeIds.empty());
    return 0;
}
