#include "ResultPanel.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSlider>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void requireNear(double actual, double expected) {
    assert(std::fabs(actual - expected) < 1.0e-3);
}

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void testPlaneControlsUseModelBounds() {
    ResultPanel panel;
    panel.setPlaneBounds(glm::vec3(-10.0f, 0.0f, 5.0f),
                         glm::vec3(30.0f, 20.0f, 25.0f));

    auto* filterType = panel.findChild<QComboBox*>("filterTypeCombo");
    auto* axis = panel.findChild<QComboBox*>("planeAxisCombo");
    auto* offset = panel.findChild<QDoubleSpinBox*>("planeOffsetSpin");
    auto* slider = panel.findChild<QSlider*>("planeOffsetSlider");
    auto* range = panel.findChild<QLabel*>("planeRangeLabel");

    assert(filterType);
    assert(axis);
    assert(offset);
    assert(slider);
    assert(range);

    assert(filterType->itemText(1) == "裁剪平面（隐藏一侧）");
    assert(filterType->itemText(2) == "切片线（不隐藏）");
    assert(axis->itemText(0) == "X 平面");
    assert(axis->itemText(1) == "Y 平面");
    assert(axis->itemText(2) == "Z 平面");
    assert(axis->minimumWidth() >= 88);

    filterType->setCurrentIndex(1);
    axis->setCurrentIndex(1);
    requireNear(offset->minimum(), 0.0);
    requireNear(offset->maximum(), 20.0);
    requireNear(offset->value(), 10.0);
    assert(range->text().contains("0"));
    assert(range->text().contains("20"));

    slider->setValue(250);
    requireNear(offset->value(), 5.0);

    offset->setValue(15.0);
    assert(std::abs(slider->value() - 750) <= 1);

    int previewCount = 0;
    glm::vec3 lastOrigin(0.0f);
    glm::vec3 lastNormal(0.0f);
    QObject::connect(&panel, &ResultPanel::planePreviewChanged,
                     [&](const glm::vec3& origin, const glm::vec3& normal) {
        ++previewCount;
        lastOrigin = origin;
        lastNormal = normal;
    });

    offset->setValue(4.0);
    assert(previewCount > 0);
    requireNear(lastOrigin.y, 4.0);
    requireNear(lastNormal.x, 0.0);
    requireNear(lastNormal.y, 1.0);
    requireNear(lastNormal.z, 0.0);

    printf("  PASS: plane controls use model bounds\n");
}

static void testClearResultsResetsPlaneFilterPreview() {
    ResultPanel panel;
    auto* filterType = panel.findChild<QComboBox*>("filterTypeCombo");
    require(filterType != nullptr, "filter type combo exists");

    int previewCount = 0;
    QObject::connect(&panel, &ResultPanel::planePreviewChanged,
                     [&](const glm::vec3&, const glm::vec3&) {
        ++previewCount;
    });

    filterType->setCurrentIndex(2);
    panel.setPlaneBounds(glm::vec3(0.0f), glm::vec3(10.0f));
    require(previewCount > 0, "slice mode emits preview before reset");

    previewCount = 0;
    panel.clearResults();
    panel.setPlaneBounds(glm::vec3(0.0f), glm::vec3(20.0f));

    require(previewCount == 0, "clearResults prevents stale plane preview on next model");
    require(filterType->currentIndex() == 0, "clearResults resets filter type");
    printf("  PASS: clear results resets plane filter preview\n");
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    printf("=== ResultPanel Tests ===\n");
    testPlaneControlsUseModelBounds();
    testClearResultsResetsPlaneFilterPreview();
    printf("All ResultPanel tests passed!\n");
    return 0;
}
