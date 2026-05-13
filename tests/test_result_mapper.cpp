#include "FEModel.h"
#include "FEMeshConverter.h"
#include "FEResultMapper.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

void requireNear(float actual, float expected) {
    assert(std::fabs(actual - expected) < 1.0e-5f);
}

FEModel makeQuadModel() {
    FEModel model;
    model.addNode(10, {0.0f, 0.0f, 0.0f});
    model.addNode(20, {1.0f, 0.0f, 0.0f});
    model.addNode(30, {1.0f, 1.0f, 0.0f});
    model.addNode(40, {0.0f, 1.0f, 0.0f});
    model.addElement(100, ElementType::QUAD4, {10, 20, 30, 40});
    return model;
}

void mapsNodeFieldToRenderVertices() {
    FEModel model = makeQuadModel();
    FERenderData rd = FEMeshConverter::toRenderData(model);

    FEScalarField field;
    field.name = "Temperature";
    field.location = FieldLocation::Node;
    field.values = {
        {10, 1.0f},
        {20, 2.0f},
        {30, 3.0f},
        {40, 4.0f},
    };

    FEMappedScalars mapped = FEResultMapper::mapScalarToVertices(field, rd, model);

    assert(mapped.scalars.size() == rd.vertexToNode.size());
    assert(mapped.location == FieldLocation::Node);
    requireNear(mapped.minValue, 1.0f);
    requireNear(mapped.maxValue, 4.0f);
    assert(mapped.minId == 10);
    assert(mapped.maxId == 40);

    for (std::size_t i = 0; i < rd.vertexToNode.size(); ++i) {
        int nodeId = rd.vertexToNode[i];
        requireNear(mapped.scalars[i], field.values.at(nodeId));
    }
}

void mapsElementFieldToRenderVertices() {
    FEModel model = makeQuadModel();
    FERenderData rd = FEMeshConverter::toRenderData(model);

    FEScalarField field;
    field.name = "Element Energy";
    field.location = FieldLocation::Element;
    field.values = {{100, 42.0f}};

    FEMappedScalars mapped = FEResultMapper::mapScalarToVertices(field, rd, model);

    assert(mapped.scalars.size() == rd.vertexCount());
    assert(mapped.location == FieldLocation::Element);
    requireNear(mapped.minValue, 42.0f);
    requireNear(mapped.maxValue, 42.0f);
    assert(mapped.minId == 100);
    assert(mapped.maxId == 100);

    for (float value : mapped.scalars) {
        requireNear(value, 42.0f);
    }
}

}  // namespace

int main() {
    mapsNodeFieldToRenderVertices();
    mapsElementFieldToRenderVertices();
    std::cout << "test_result_mapper passed\n";
    return 0;
}
