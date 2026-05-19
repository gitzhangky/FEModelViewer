#include "FEParser.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static QString writeInpFile(const QString& text)
{
    QTemporaryDir dir;
    dir.setAutoRemove(false);
    QString path = dir.filePath("sets.inp");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Text), "create temporary inp");
    QTextStream out(&file);
    out << text;
    file.close();
    return path;
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

static void testInpNsetElsetAndGenerateAreParsed()
{
    QString path = writeInpFile(
        "*Node\n"
        "1,0,0,0\n"
        "2,1,0,0\n"
        "3,1,1,0\n"
        "4,0,1,0\n"
        "5,2,0,0\n"
        "6,2,1,0\n"
        "*Element, type=S4, elset=Shells\n"
        "100,1,2,3,4\n"
        "200,2,5,6,3\n"
        "*Nset, nset=Fixed, generate\n"
        "1,5,2\n"
        "*Nset, nset=Loaded\n"
        "3, 4, 6\n"
        "*Elset, elset=OutputElems\n"
        "100, 200\n");

    FEModel model;
    bool ok = FEParser::parseAbaqusInp(path, model);

    require(ok, "parse succeeds");
    require(model.nodeSets.size() == 2, "two node sets parsed");
    require(model.elementSets.size() == 2, "element keyword elset plus explicit elset parsed");
    require(model.parts.size() == 1, "element elset still creates a part");

    const FENodeSet* fixed = findNodeSet(model, "Fixed");
    require(fixed != nullptr, "Fixed node set exists");
    require(fixed->nodeIds.size() == 3, "Fixed generate expands three ids");
    require(fixed->nodeIds[0] == 1 && fixed->nodeIds[1] == 3 && fixed->nodeIds[2] == 5,
            "Fixed generate expands expected ids");

    const FENodeSet* loaded = findNodeSet(model, "Loaded");
    require(loaded != nullptr, "Loaded node set exists");
    require(loaded->nodeIds.size() == 3, "Loaded node set contains three ids");
    require(loaded->nodeIds[0] == 3 && loaded->nodeIds[1] == 4 && loaded->nodeIds[2] == 6,
            "Loaded node set keeps listed ids");

    const FEElementSet* shells = findElementSet(model, "Shells");
    require(shells != nullptr, "Element keyword ELSET also creates element set");
    require(shells->elementIds.size() == 2, "Shells element set has element ids");
    require(shells->elementIds[0] == 100 && shells->elementIds[1] == 200,
            "Shells element set receives element ids from *Element");

    const FEElementSet* output = findElementSet(model, "OutputElems");
    require(output != nullptr, "explicit element set exists");
    require(output->elementIds.size() == 2, "explicit element set has element ids");
    require(output->elementIds[0] == 100 && output->elementIds[1] == 200,
            "explicit element set keeps listed ids");

    QFile::remove(path);
    QDir().rmdir(QFileInfo(path).absolutePath());
    std::printf("  PASS: inp nset/elset and generate are parsed\n");
}

static void testExplicitElsetCreatesFallbackPartWhenElementKeywordHasNoElset()
{
    QString path = writeInpFile(
        "*Node\n"
        "1,0,0,0\n"
        "2,1,0,0\n"
        "3,0,1,0\n"
        "4,0,0,1\n"
        "*Element, type=C3D4\n"
        "10,1,2,3,4\n"
        "*Elset, elset=Default\n"
        "10\n");

    FEModel model;
    bool ok = FEParser::parseAbaqusInp(path, model);

    require(ok, "parse succeeds");
    require(model.elementSets.size() == 1, "explicit elset parsed");
    require(model.parts.size() == 1, "explicit elset creates fallback part");
    require(model.parts[0].name == "Default", "fallback part keeps elset name");
    require(model.parts[0].elementIds == std::vector<int>({10}),
            "fallback part keeps elset element ids");

    QFile::remove(path);
    QDir().rmdir(QFileInfo(path).absolutePath());
    std::printf("  PASS: explicit elset creates fallback part\n");
}

int main()
{
    std::printf("=== INP Set Tests ===\n");
    testInpNsetElsetAndGenerateAreParsed();
    testExplicitElsetCreatesFallbackPartWhenElementKeywordHasNoElset();
    std::printf("All INP set tests passed!\n");
    return 0;
}
