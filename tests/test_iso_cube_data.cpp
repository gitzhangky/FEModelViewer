/**
 * @file test_iso_cube_data.cpp
 * @brief 端到端测试：examples/data/hex_cube.bdf + hex_cube.unv → 等值面
 *
 * 验证：
 *   1. BDF 解析能拿到 1 个 HEX8 单元 + 8 个节点
 *   2. UNV 2414 解析能拿到节点温度场（底面 0，顶面 1）
 *   3. FEIsoSurface 对 iso=0.5 能切出非空三角网格
 */

#include <QCoreApplication>
#include <QString>
#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "FEParser.h"
#include "FEModel.h"
#include "FEField.h"
#include "FEResultData.h"
#include "FEIsoSurface.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const char* dataDir = std::getenv("FERENDER_TEST_DATA_DIR");
    QString base = dataDir ? QString::fromLocal8Bit(dataDir)
                           : QString("examples/data");

    QString bdfPath = base + "/hex_cube.bdf";
    QString unvPath = base + "/hex_cube.unv";

    FEModel model;
    bool bdfOk = FEParser::parseNastranBdf(bdfPath, model);
    if (!bdfOk) {
        std::fprintf(stderr, "FAIL: cannot parse %s\n", qPrintable(bdfPath));
        return 1;
    }
    std::printf("  bdf: nodes=%d elements=%d\n",
                static_cast<int>(model.nodes.size()),
                static_cast<int>(model.elements.size()));
    assert(model.nodes.size() == 8);
    assert(model.elements.size() == 1);
    assert(model.elements.begin()->second.type == ElementType::HEX8);

    FEResultData results;
    bool unvOk = FEParser::parseUnvResults(unvPath, results);
    if (!unvOk) {
        std::fprintf(stderr, "FAIL: cannot parse %s\n", qPrintable(unvPath));
        return 1;
    }
    std::printf("  unv: subcases=%d\n",
                static_cast<int>(results.subcases.size()));
    assert(!results.subcases.empty());

    const FESubcase& sc = results.subcases.front();
    assert(!sc.resultTypes.empty());
    const FEResultType& rt = sc.resultTypes.front();
    assert(!rt.components.empty());
    const FEScalarField& field = rt.components.front().field;
    assert(field.location == FieldLocation::Node);
    assert(field.values.size() == 8);

    Mesh iso = FEIsoSurface::extract(model, field, 0.5f);
    int triCount = static_cast<int>(iso.indices.size() / 3);
    std::printf("  iso T=0.5: %d triangles\n", triCount);
    assert(triCount > 0);

    std::printf("PASS: hex_cube data files load and iso-surface extracts\n");
    return 0;
}
