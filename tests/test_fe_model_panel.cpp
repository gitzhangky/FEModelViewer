#include "FEModelPanel.h"

#include <QApplication>
#include <QPushButton>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static QPushButton* findButton(FEModelPanel& panel, const QString& text)
{
    for (auto* btn : panel.findChildren<QPushButton*>()) {
        if (btn->text() == text) return btn;
    }
    return nullptr;
}

static void testVisibilityButtonsUseCurrentSelectionWhenSearchIsEmpty()
{
    FEModelPanel panel;
    PickMode requestedMode = PickMode::Node;
    std::vector<int> requestedIds;
    bool requestedVisible = true;
    int requestCount = 0;

    QObject::connect(&panel, &FEModelPanel::visibilityRequested,
                     [&](PickMode mode, const std::vector<int>& ids, bool visible) {
        requestedMode = mode;
        requestedIds = ids;
        requestedVisible = visible;
        ++requestCount;
    });

    panel.updateSelectionInfo(PickMode::Element, 2, {20, 10});

    auto* hide = findButton(panel, "隐藏");
    require(hide != nullptr, "hide button exists");
    hide->click();

    require(requestCount == 1, "hide emits one visibility request");
    require(requestedMode == PickMode::Element, "request uses selected element mode");
    require(requestedIds.size() == 2, "request contains selected ids");
    require(requestedIds[0] == 10, "request ids are sorted");
    require(requestedIds[1] == 20, "request ids include second selected id");
    require(requestedVisible == false, "request hides selected ids");
    printf("  PASS: visibility buttons use current selection when search is empty\n");
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    printf("=== FEModelPanel Tests ===\n");
    testVisibilityButtonsUseCurrentSelectionWhenSearchIsEmpty();
    printf("All FEModelPanel tests passed!\n");
    return 0;
}
