/**
 * @file test_threshold_row_data.cpp
 * @brief 端到端测试：examples/data/hex_row.bdf + hex_row.unv → 阈值过滤
 *
 * 4 个 HEX8 沿 X 排成一行，节点温度沿 X 线性 0→4。
 * 单元平均温度：0.5、1.5、2.5、3.5。
 * 阈值 [1.0, 3.0] 应保留中间两个单元（1002, 1003）。
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
#include "FEMeshConverter.h"
#include "FEPostFilter.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    const char* dataDir = std::getenv("FERENDER_TEST_DATA_DIR");
    QString base = dataDir ? QString::fromLocal8Bit(dataDir)
                           : QString("examples/data");

    QString bdfPath = base + "/hex_row.bdf";
    QString unvPath = base + "/hex_row.unv";

    FEModel model;
    bool bdfOk = FEParser::parseNastranBdf(bdfPath, model);
    if (!bdfOk) {
        std::fprintf(stderr, "FAIL: cannot parse %s\n", qPrintable(bdfPath));
        return 1;
    }
    std::printf("  bdf: nodes=%d elements=%d\n",
                static_cast<int>(model.nodes.size()),
                static_cast<int>(model.elements.size()));
    assert(model.nodes.size() == 20);
    assert(model.elements.size() == 4);

    FEResultData results;
    bool unvOk = FEParser::parseUnvResults(unvPath, results);
    if (!unvOk) {
        std::fprintf(stderr, "FAIL: cannot parse %s\n", qPrintable(unvPath));
        return 1;
    }
    assert(!results.subcases.empty());
    const FEScalarField& nodeField =
        results.subcases.front().resultTypes.front().components.front().field;
    assert(nodeField.location == FieldLocation::Node);
    assert(nodeField.values.size() == 20);

    // 节点场转单元场（同 MainWindow::applyThreshold 的处理）
    FEScalarField elemField;
    elemField.location = FieldLocation::Element;
    for (const auto& [eid, elem] : model.elements) {
        float sum = 0.0f;
        int n = 0;
        for (int nid : elem.nodeIds) {
            auto it = nodeField.values.find(nid);
            if (it != nodeField.values.end()) { sum += it->second; ++n; }
        }
        if (n > 0) elemField.values[eid] = sum / n;
    }
    std::printf("  element avg temperatures:");
    for (const auto& [eid, val] : elemField.values)
        std::printf(" %d=%.2f", eid, static_cast<double>(val));
    std::printf("\n");

    FERenderData rd = FEMeshConverter::toRenderData(model);
    assert(rd.triangleCount() > 0);

    // 阈值 [1.0, 3.0]：单元 1002 (avg=1.5) 和 1003 (avg=2.5) 保留
    auto filtered = FEPostFilter::thresholdByElementValue(rd, elemField, 1.0f, 3.0f);
    int keptTris = filtered.triangleCount();
    std::printf("  threshold [1.0, 3.0]: kept %d triangles\n", keptTris);
    assert(keptTris > 0);

    bool has1002 = false, has1003 = false, has1001 = false, has1004 = false;
    for (int eid : filtered.triangleToElement) {
        if (eid == 1001) has1001 = true;
        if (eid == 1002) has1002 = true;
        if (eid == 1003) has1003 = true;
        if (eid == 1004) has1004 = true;
    }
    assert(has1002 && has1003);
    assert(!has1001 && !has1004);

    std::printf("PASS: hex_row data files load and threshold keeps middle elements\n");
    return 0;
}
