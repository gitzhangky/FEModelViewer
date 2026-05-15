#include "PartsPanel.h"

#include <QApplication>
#include <QTreeWidget>
#include <cassert>
#include <cstdio>

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

static void testProgrammaticPartSyncDoesNotEmitTreeSelection()
{
    PartsPanel panel;
    panel.setParts("model", makeParts(),
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

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    printf("=== PartsPanel Tests ===\n");
    testProgrammaticPartSyncDoesNotEmitTreeSelection();
    printf("All PartsPanel tests passed!\n");
    return 0;
}
