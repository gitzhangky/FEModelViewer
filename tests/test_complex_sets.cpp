#include "FEParser.h"

#include <QFile>
#include <QTextStream>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static const FENodeSet* findNodeSet(const FEModel& model, const std::string& name)
{
    for (const auto& set : model.nodeSets)
        if (set.name == name) return &set;
    return nullptr;
}

static const FEElementSet* findElementSet(const FEModel& model, const std::string& name)
{
    for (const auto& set : model.elementSets)
        if (set.name == name) return &set;
    return nullptr;
}

static QString writeInpFile(const QString& text)
{
    QTemporaryDir dir;
    dir.setAutoRemove(false);
    QString path = dir.filePath("test.inp");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Text), "create temporary inp");
    QTextStream out(&file);
    out << text;
    file.close();
    return path;
}

// 测试1: NSET/ELSET 可以通过名称引用其他已定义的集合
static void testSetNameReference()
{
    QString path = writeInpFile(
        "*Node\n"
        "1,0,0,0\n"
        "2,1,0,0\n"
        "3,0,1,0\n"
        "4,0,0,1\n"
        "5,1,1,0\n"
        "6,1,0,1\n"
        "*Element, type=C3D4, elset=Part1\n"
        "10, 1, 2, 3, 4\n"
        "*Element, type=C3D4, elset=Part2\n"
        "20, 3, 4, 5, 6\n"
        "*Nset, nset=SetA\n"
        "1, 2\n"
        "*Nset, nset=SetB\n"
        "3, 4\n"
        "*Nset, nset=AllNodes\n"
        "SetA, SetB\n"
        "*Elset, elset=AllElems\n"
        "Part1, Part2\n");

    FEModel model;
    bool ok = FEParser::parseAbaqusInp(path, model);

    require(ok, "parse succeeds");

    const FENodeSet* allNodes = findNodeSet(model, "AllNodes");
    require(allNodes != nullptr, "AllNodes set exists");
    std::printf("    AllNodes has %zu IDs\n", allNodes->nodeIds.size());
    // 应该包含 1,2,3,4（通过引用 SetA 和 SetB）
    require(allNodes->nodeIds.size() == 4, "AllNodes should have 4 IDs from SetA+SetB");

    const FEElementSet* allElems = findElementSet(model, "AllElems");
    require(allElems != nullptr, "AllElems set exists");
    std::printf("    AllElems has %zu IDs\n", allElems->elementIds.size());
    // 应该包含 10,20（通过引用 Part1 和 Part2）
    require(allElems->elementIds.size() == 2, "AllElems should have 2 IDs from Part1+Part2");

    QFile::remove(path);
    QDir().rmdir(QFileInfo(path).absolutePath());
    std::printf("  PASS: set name reference\n");
}

// 测试2: *PART / *END PART 结构
static void testPartStructure()
{
    QString path = writeInpFile(
        "*PART, NAME=Block\n"
        "*Node\n"
        "1,0,0,0\n"
        "2,1,0,0\n"
        "3,0,1,0\n"
        "4,0,0,1\n"
        "*Element, type=C3D4, elset=AllTet\n"
        "10, 1, 2, 3, 4\n"
        "*Nset, nset=Bottom\n"
        "1, 2\n"
        "*END PART\n"
        "*ASSEMBLY, NAME=Assembly\n"
        "*INSTANCE, NAME=Block-1, PART=Block\n"
        "*END INSTANCE\n"
        "*NSET, NSET=Fixed, INSTANCE=Block-1\n"
        "Bottom\n"
        "*END ASSEMBLY\n");

    FEModel model;
    bool ok = FEParser::parseAbaqusInp(path, model);

    require(ok, "parse succeeds");
    std::printf("    nodes=%d, elements=%d\n", model.nodeCount(), model.elementCount());
    std::printf("    nodeSets=%zu, elementSets=%zu\n",
                model.nodeSets.size(), model.elementSets.size());

    for (const auto& ns : model.nodeSets) {
        std::printf("    nodeSet '%s': %zu IDs\n", ns.name.c_str(), ns.nodeIds.size());
    }

    // Bottom 应该有 2 个节点
    const FENodeSet* bottom = findNodeSet(model, "Bottom");
    require(bottom != nullptr, "Bottom set exists");
    require(bottom->nodeIds.size() == 2, "Bottom has 2 IDs");

    // Fixed 应该通过引用 Bottom 获得 2 个节点
    const FENodeSet* fixed = findNodeSet(model, "Fixed");
    require(fixed != nullptr, "Fixed set exists");
    require(fixed->nodeIds.size() == 2, "Fixed should have 2 IDs via Bottom reference");

    QFile::remove(path);
    QDir().rmdir(QFileInfo(path).absolutePath());
    std::printf("  PASS: part structure with assembly set reference\n");
}

// 测试3: GENERATE 多行（多个范围）
static void testGenerateMultiRange()
{
    QString path = writeInpFile(
        "*Node\n"
        "1,0,0,0\n"
        "2,1,0,0\n"
        "3,2,0,0\n"
        "4,0,1,0\n"
        "5,1,1,0\n"
        "6,2,1,0\n"
        "*Element, type=S4\n"
        "1, 1, 2, 5, 4\n"
        "2, 2, 3, 6, 5\n"
        "*Elset, elset=MySet, generate\n"
        "1, 2, 1\n");

    FEModel model;
    bool ok = FEParser::parseAbaqusInp(path, model);

    require(ok, "parse succeeds");

    const FEElementSet* mySet = findElementSet(model, "MySet");
    require(mySet != nullptr, "MySet exists");
    require(mySet->elementIds.size() == 2, "MySet has 2 elements from generate");
    require(mySet->elementIds[0] == 1 && mySet->elementIds[1] == 2,
            "MySet has correct elements");

    QFile::remove(path);
    QDir().rmdir(QFileInfo(path).absolutePath());
    std::printf("  PASS: generate multi-range\n");
}

// 测试4: 加载移动硬盘上的复杂模型
static void testComplexModel()
{
    QString path = "/Volumes/Lexar/models/complex_test_model_fixed.inp";
    if (!QFile::exists(path)) {
        std::printf("  SKIP: complex model not available at %s\n", qPrintable(path));
        return;
    }

    FEModel model;
    bool ok = FEParser::parseAbaqusInp(path, model);
    require(ok, "parse complex model");

    std::printf("    nodes=%d, elements=%d\n", model.nodeCount(), model.elementCount());
    std::printf("    parts=%zu, nodeSets=%zu, elementSets=%zu\n",
                model.parts.size(), model.nodeSets.size(), model.elementSets.size());

    for (const auto& ns : model.nodeSets) {
        std::printf("    nodeSet '%s': %zu IDs\n", ns.name.c_str(), ns.nodeIds.size());
    }
    for (const auto& es : model.elementSets) {
        std::printf("    elementSet '%s': %zu IDs\n", es.name.c_str(), es.elementIds.size());
    }

    // BottomFace 应该有节点
    const FENodeSet* bottom = findNodeSet(model, "BottomFace");
    require(bottom != nullptr, "BottomFace set exists");
    require(!bottom->nodeIds.empty(), "BottomFace has nodes");

    // TopFace 应该有节点
    const FENodeSet* top = findNodeSet(model, "TopFace");
    require(top != nullptr, "TopFace set exists");
    require(!top->nodeIds.empty(), "TopFace has nodes");

    // HexLower (GENERATE) 应该有单元
    const FEElementSet* hexLower = findElementSet(model, "HexLower");
    require(hexLower != nullptr, "HexLower set exists");
    std::printf("    HexLower: %zu elements\n", hexLower->elementIds.size());
    require(hexLower->elementIds.size() == 18750, "HexLower has 18750 elements from generate 1-18750");

    // HexUpper (GENERATE) 应该有单元
    const FEElementSet* hexUpper = findElementSet(model, "HexUpper");
    require(hexUpper != nullptr, "HexUpper set exists");
    std::printf("    HexUpper: %zu elements\n", hexUpper->elementIds.size());
    require(hexUpper->elementIds.size() == 18750, "HexUpper has 18750 elements from generate 18751-37500");

    // AllFixed 应该通过名称引用获得节点
    const FENodeSet* allFixed = findNodeSet(model, "AllFixed");
    if (allFixed) {
        std::printf("    AllFixed: %zu IDs\n", allFixed->nodeIds.size());
        require(!allFixed->nodeIds.empty(),
                "AllFixed should have nodes via set name references");
    } else {
        std::printf("    WARNING: AllFixed set not found\n");
    }

    std::printf("  PASS: complex model sets parsed\n");
}

int main()
{
    std::printf("=== Complex Set Tests ===\n");
    testSetNameReference();
    testPartStructure();
    testGenerateMultiRange();
    testComplexModel();
    std::printf("All complex set tests passed!\n");
    return 0;
}
