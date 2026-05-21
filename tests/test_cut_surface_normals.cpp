#include "FEModel.h"
#include "FEMeshConverter.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

static FEModel makeTwoHexModel()
{
    FEModel model;

    // 两个 HEX8 沿 X 方向相邻，共享 x=1 的内部面。
    model.addNode(1,  {0.0f, 0.0f, 0.0f});
    model.addNode(2,  {1.0f, 0.0f, 0.0f});
    model.addNode(3,  {1.0f, 1.0f, 0.0f});
    model.addNode(4,  {0.0f, 1.0f, 0.0f});
    model.addNode(5,  {0.0f, 0.0f, 1.0f});
    model.addNode(6,  {1.0f, 0.0f, 1.0f});
    model.addNode(7,  {1.0f, 1.0f, 1.0f});
    model.addNode(8,  {0.0f, 1.0f, 1.0f});
    model.addNode(9,  {2.0f, 0.0f, 0.0f});
    model.addNode(10, {2.0f, 1.0f, 0.0f});
    model.addNode(11, {2.0f, 0.0f, 1.0f});
    model.addNode(12, {2.0f, 1.0f, 1.0f});

    model.addElement(100, ElementType::HEX8, {1, 2, 3, 4, 5, 6, 7, 8});
    model.addElement(200, ElementType::HEX8, {2, 9, 10, 3, 6, 11, 12, 7});
    return model;
}

static void exposedCutFaceKeepsFlatNormal()
{
    FEModel model = makeTwoHexModel();
    std::vector<int> allElems = {100, 200};
    FESurfaceCache cache = FEMeshConverter::buildSurfaceCache(model, allElems);

    FERenderData rd = FEMeshConverter::buildRenderData(
        cache, [](int elemId) { return elemId == 100; });

    int checkedVertices = 0;
    for (int tri = 0; tri < static_cast<int>(rd.triangleToElement.size()); ++tri) {
        // HEX8 右侧面在 extractFaces() 中是 faceIndex 5；隐藏右侧单元后这是 x=1 切口面。
        if (rd.triangleToElement[tri] != 100 || rd.triangleToFace[tri] != 5)
            continue;

        for (int k = 0; k < 3; ++k) {
            unsigned int vi = rd.mesh.indices[tri * 3 + k];
            float nx = rd.mesh.vertices[vi * 6 + 3];
            float ny = rd.mesh.vertices[vi * 6 + 4];
            float nz = rd.mesh.vertices[vi * 6 + 5];
            assert(std::fabs(nx - 1.0f) < 1e-5f);
            assert(std::fabs(ny) < 1e-5f);
            assert(std::fabs(nz) < 1e-5f);
            ++checkedVertices;
        }
    }

    assert(checkedVertices == 6);
    std::printf("PASS: exposed cut face keeps flat +X normals\n");
}

int main()
{
    exposedCutFaceKeepsFlatNormal();
    return 0;
}
