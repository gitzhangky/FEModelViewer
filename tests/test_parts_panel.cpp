#include "PartsPanel.h"

#include <QApplication>
#include <QTreeWidget>
#include <cassert>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static std::vector<FEPart> makeParts()
{
    FEPart p0;
    p0.name = "Part 1";
    p0.elementIds = {1, 2};

    FEPart p1;
    p1.name = "Part 2";
    p1.elementIds = {3, 4};

    return {p0, p1};
}

static std::vector<FENodeSet> makeNodeSets()
{
    FENodeSet set;
    set.name = "Fixed";
    set.nodeIds = {1, 3, 5};
    return {set};
}

static std::vector<FEElementSet> makeElementSets()
{
    FEElementSet set;
    set.name = "Output";
    set.elementIds = {100, 200};
    return {set};
}

static QTreeWidgetItem* findItemByText(QTreeWidgetItem* root, const QString& text)
{
    if (!root) return nullptr;
    if (root->text(0).startsWith(text)) return root;
    for (int i = 0; i < root->childCount(); ++i) {
        if (auto* found = findItemByText(root->child(i), text))
            return found;
    }
    return nullptr;
}

static void testProgrammaticPartSyncDoesNotEmitTreeSelection()
{
    PartsPanel panel;
    panel.setParts("model", makeParts(),
                   {}, {},
                   {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)});

    auto* tree = panel.findChild<QTreeWidget*>();
    assert(tree);

    int treeSelectionSignalCount = 0;
    QObject::connect(tree, &QTreeWidget::itemSelectionChanged,
                     [&]() { ++treeSelectionSignalCount; });

    panel.selectParts({0});
    panel.selectParts({1});
    QApplication::processEvents();

    assert(treeSelectionSignalCount == 0);
    assert(tree->selectedItems().size() == 1);
    assert(tree->selectedItems().front()->data(0, Qt::UserRole).toInt() == 1);
    printf("  PASS: programmatic part sync blocks tree selection signals\n");
}

static void testSetSelectionAndVisibilityRequests()
{
    PartsPanel panel;
    panel.setParts("model", makeParts(), makeNodeSets(), makeElementSets(),
                   {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)});

    auto* tree = panel.findChild<QTreeWidget*>();
    require(tree != nullptr, "tree exists");
    auto* root = tree->topLevelItem(0);
    require(root != nullptr, "root item exists");

    std::vector<int> selectedIds;
    PickMode selectedMode = PickMode::Element;
    int selectionCount = 0;
    QObject::connect(&panel, &PartsPanel::setSelectionRequested,
                     [&](PickMode mode, const std::vector<int>& ids) {
        selectedMode = mode;
        selectedIds = ids;
        ++selectionCount;
    });

    bool visibleFlag = true;
    std::vector<int> visibilityIds;
    PickMode visibilityMode = PickMode::Element;
    int visibilityCount = 0;
    QObject::connect(&panel, &PartsPanel::setVisibilityRequested,
                     [&](PickMode mode, const std::vector<int>& ids, bool visible) {
        visibilityMode = mode;
        visibilityIds = ids;
        visibleFlag = visible;
        ++visibilityCount;
    });

    auto* fixed = findItemByText(root, "Fixed");
    require(fixed != nullptr, "node set item exists");
    tree->setCurrentItem(fixed);
    fixed->setSelected(true);
    QApplication::processEvents();

    require(selectionCount == 1, "node set selection signal emitted");
    require(selectedMode == PickMode::Node, "node set selection uses node mode");
    require(selectedIds == std::vector<int>({1, 3, 5}), "node set selection ids emitted");

    fixed->setCheckState(0, Qt::Unchecked);
    QApplication::processEvents();

    require(visibilityCount == 1, "node set visibility signal emitted");
    require(visibilityMode == PickMode::Node, "node set visibility uses node mode");
    require(visibilityIds == std::vector<int>({1, 3, 5}), "node set visibility ids emitted");
    require(!visibleFlag, "node set visibility can hide");

    auto* output = findItemByText(root, "Output");
    require(output != nullptr, "element set item exists");
    tree->clearSelection();
    tree->setCurrentItem(output);
    output->setSelected(true);
    QApplication::processEvents();

    require(selectionCount == 2, "element set selection signal emitted");
    require(selectedMode == PickMode::Element, "element set selection uses element mode");
    require(selectedIds == std::vector<int>({100, 200}), "element set selection ids emitted");

    std::printf("  PASS: set selection and visibility requests are emitted\n");
}

static void testSetGroupSelectionRequestsAllChildSetIds()
{
    PartsPanel panel;
    panel.setParts("model", makeParts(), makeNodeSets(), makeElementSets(),
                   {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)});

    auto* tree = panel.findChild<QTreeWidget*>();
    require(tree != nullptr, "tree exists");
    auto* root = tree->topLevelItem(0);
    require(root != nullptr, "root item exists");

    int partSelectionCount = 0;
    QObject::connect(&panel, &PartsPanel::partSelectionChanged,
                     [&](const std::vector<int>&) { ++partSelectionCount; });

    PickMode selectedMode = PickMode::Element;
    std::vector<int> selectedIds;
    int setSelectionCount = 0;
    QObject::connect(&panel, &PartsPanel::setSelectionRequested,
                     [&](PickMode mode, const std::vector<int>& ids) {
        selectedMode = mode;
        selectedIds = ids;
        ++setSelectionCount;
    });

    auto* nodeSetGroup = findItemByText(root, "节点集");
    require(nodeSetGroup != nullptr, "node set group exists");
    tree->setCurrentItem(nodeSetGroup);
    nodeSetGroup->setSelected(true);
    QApplication::processEvents();

    require(setSelectionCount == 1, "node set group emits set selection");
    require(selectedMode == PickMode::Node, "node set group uses node mode");
    require(selectedIds == std::vector<int>({1, 3, 5}), "node set group emits child ids");
    require(partSelectionCount == 0, "node set group does not emit empty part selection");

    std::printf("  PASS: set group selection requests all child set ids\n");
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    printf("=== PartsPanel Tests ===\n");
    testProgrammaticPartSyncDoesNotEmitTreeSelection();
    testSetSelectionAndVisibilityRequests();
    testSetGroupSelectionRequestsAllChildSetIds();
    printf("All PartsPanel tests passed!\n");
    return 0;
}
