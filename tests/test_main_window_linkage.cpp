#include "MainWindow.h"
#include "GLWidget.h"
#include "PartsPanel.h"

#include <QAction>
#include <QApplication>
#include <QToolBar>
#include <QTreeWidget>

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
    set.nodeIds = {10, 20};
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

static QAction* findPickAction(MainWindow& window, PickMode mode)
{
    for (auto* toolbar : window.findChildren<QToolBar*>()) {
        for (auto* action : toolbar->actions()) {
            if (!action->data().isValid()) continue;
            if (action->data().toInt() == static_cast<int>(mode))
                return action;
        }
    }
    return nullptr;
}

static void prepareModelTree(MainWindow& window)
{
    auto* partsPanel = window.findChild<PartsPanel*>();
    require(partsPanel != nullptr, "parts panel exists");
    partsPanel->setParts("model", makeParts(), makeNodeSets(), makeElementSets(),
                         {glm::vec3(1.0f, 0.0f, 0.0f),
                          glm::vec3(0.0f, 1.0f, 0.0f)});
}

static void selectTreeItem(MainWindow& window, const QString& itemText)
{
    auto* tree = window.findChild<QTreeWidget*>();
    require(tree != nullptr, "model tree exists");
    auto* root = tree->topLevelItem(0);
    require(root != nullptr, "model tree root exists");

    auto* item = findItemByText(root, itemText);
    require(item != nullptr, "requested model tree item exists");

    tree->clearSelection();
    tree->setCurrentItem(item);
    item->setSelected(true);
    QApplication::processEvents();
}

static void testPartTreeSelectionSyncsToolbar()
{
    MainWindow window;
    prepareModelTree(window);

    auto* gl = window.findChild<GLWidget*>();
    require(gl != nullptr, "GLWidget exists");

    auto* partAction = findPickAction(window, PickMode::Part);
    require(partAction != nullptr, "part toolbar action exists");

    selectTreeItem(window, "Part 2");

    require(gl->pickMode() == PickMode::Part, "part tree selection switches GL pick mode to part");
    require(partAction->isChecked(), "part tree selection checks the part toolbar action");

    std::printf("  PASS: part tree selection syncs GL pick mode and toolbar\n");
}

static void testPartGroupSelectionSyncsToolbar()
{
    MainWindow window;
    prepareModelTree(window);

    auto* gl = window.findChild<GLWidget*>();
    require(gl != nullptr, "GLWidget exists");

    auto* partAction = findPickAction(window, PickMode::Part);
    require(partAction != nullptr, "part toolbar action exists");

    selectTreeItem(window, "部件");

    require(gl->pickMode() == PickMode::Part, "part group selection switches GL pick mode to part");
    require(partAction->isChecked(), "part group selection checks the part toolbar action");

    std::printf("  PASS: part group selection syncs GL pick mode and toolbar\n");
}

static void testSetTreeSelectionSyncsToolbarEvenWhenGLModeAlreadyChanged()
{
    MainWindow window;
    prepareModelTree(window);

    auto* gl = window.findChild<GLWidget*>();
    require(gl != nullptr, "GLWidget exists");

    auto* nodeAction = findPickAction(window, PickMode::Node);
    auto* elementAction = findPickAction(window, PickMode::Element);
    require(nodeAction != nullptr, "node toolbar action exists");
    require(elementAction != nullptr, "element toolbar action exists");

    nodeAction->setChecked(true);
    gl->selectByIds(PickMode::Element, {});
    require(gl->pickMode() == PickMode::Element, "test setup changed GL pick mode to element");
    require(nodeAction->isChecked(), "test setup leaves toolbar on node");

    selectTreeItem(window, "Output");

    require(gl->pickMode() == PickMode::Element, "element set selection keeps GL pick mode on element");
    require(elementAction->isChecked(), "element set selection checks the element toolbar action");

    std::printf("  PASS: set tree selection repairs stale toolbar state\n");
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("=== MainWindow Linkage Tests ===\n");
    testPartTreeSelectionSyncsToolbar();
    testPartGroupSelectionSyncsToolbar();
    testSetTreeSelectionSyncsToolbarEvenWhenGLModeAlreadyChanged();
    std::printf("All MainWindow linkage tests passed!\n");
    return 0;
}
