/**
 * @file DisplayPanel.cpp
 * @brief 显示控制面板实现
 */

#include "DisplayPanel.h"
#include "Theme.h"
#include "StyleHelpers.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QPushButton>
#include <QColorDialog>

namespace {
const glm::vec3 kDefaultObjColor(0.55f, 0.75f, 0.73f);
const glm::vec3 kDefaultEdgeColor(0.20f, 0.20f, 0.22f);

QColor toQColor(const glm::vec3& c) {
    return QColor::fromRgbF(c.x, c.y, c.z);
}
glm::vec3 toVec3(const QColor& c) {
    return glm::vec3(c.redF(), c.greenF(), c.blueF());
}
void setSwatch(QPushButton* btn, const QColor& c) {
    btn->setProperty("swatch", c);
    btn->setStyleSheet(QString(
        "QPushButton { background: %1; border: 1px solid #888; border-radius: 4px; }")
        .arg(c.name()));
}
}

DisplayPanel::DisplayPanel(QWidget* parent) : QWidget(parent) {
    // 横向布局：底部区域宽而矮，两组并排比单列纵向堆叠更省高度
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto makeLabel = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setFixedWidth(64);
        l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        return l;
    };
    auto makeGrid = [](QGroupBox* box) {
        auto* g = new QGridLayout(box);
        g->setContentsMargins(12, 16, 12, 12);
        g->setHorizontalSpacing(10);
        g->setVerticalSpacing(8);
        g->setColumnStretch(2, 1);
        return g;
    };

    // ── 组1：几何 ──
    auto* geoBox = new QGroupBox("几何");
    auto* g1 = makeGrid(geoBox);

    modeCombo_ = new QComboBox;
    modeCombo_->addItem("实体", 0);
    modeCombo_->addItem("线框", 1);
    modeCombo_->addItem("实体 + 线框", 2);
    modeCombo_->setCurrentIndex(2);  // 默认实体+线框，与渲染默认一致
    modeCombo_->setFixedWidth(140);
    g1->addWidget(makeLabel("显示模式"), 0, 0);
    g1->addWidget(modeCombo_,            0, 1);

    projCombo_ = new QComboBox;
    projCombo_->addItem("透视", 0);
    projCombo_->addItem("正交", 1);
    projCombo_->setFixedWidth(140);
    g1->addWidget(makeLabel("投影"), 1, 0);
    g1->addWidget(projCombo_,        1, 1);

    objColorBtn_ = makeColorButton(toQColor(kDefaultObjColor),
        [this](const glm::vec3& c){ emit objectColorChanged(c); });
    g1->addWidget(makeLabel("物体颜色"), 2, 0);
    g1->addWidget(objColorBtn_,          2, 1, Qt::AlignLeft);

    edgeColorBtn_ = makeColorButton(toQColor(kDefaultEdgeColor),
        [this](const glm::vec3& c){ emit edgeColorChanged(c); });
    g1->addWidget(makeLabel("边线颜色"), 3, 0);
    g1->addWidget(edgeColorBtn_,         3, 1, Qt::AlignLeft);

    // ── 组2：线条与透明 ──
    auto* lineBox = new QGroupBox("线条与透明");
    auto* g2 = makeGrid(lineBox);

    edgeWidthSpin_ = new QSpinBox;
    edgeWidthSpin_->setRange(1, 10);
    edgeWidthSpin_->setValue(1);
    edgeWidthSpin_->setSuffix(" px");
    edgeWidthSpin_->setFixedWidth(140);
    g2->addWidget(makeLabel("边线宽度"), 0, 0);
    g2->addWidget(edgeWidthSpin_,        0, 1);

    // 透明度：滑块 + 百分比标签
    auto makeAlphaRow = [this, &makeLabel](QGridLayout* grid, const QString& text, int row,
                                           QSlider*& slider, QLabel*& valueLbl) {
        auto* container = new QWidget;
        auto* h = new QHBoxLayout(container);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 100);
        slider->setValue(100);
        slider->setMinimumWidth(120);
        valueLbl = new QLabel("100%");
        valueLbl->setFixedWidth(40);
        valueLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        h->addWidget(slider, 1);
        h->addWidget(valueLbl);
        grid->addWidget(makeLabel(text), row, 0);
        grid->addWidget(container,       row, 1, 1, 2);
    };

    makeAlphaRow(g2, "边线透明", 1, edgeAlphaSlider_, edgeAlphaValue_);
    makeAlphaRow(g2, "实体透明", 2, surfaceAlphaSlider_, surfaceAlphaValue_);

    resetBtn_ = new QPushButton("重置默认");
    g2->addWidget(resetBtn_, 3, 1, Qt::AlignLeft);

    root->addWidget(geoBox);
    root->addWidget(lineBox, 1);
    root->addStretch();

    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i){ emit displayModeChanged(modeCombo_->itemData(i).toInt()); });
    connect(projCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i){ emit projectionModeChanged(projCombo_->itemData(i).toInt()); });
    connect(edgeWidthSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v){ emit edgeWidthChanged(v); });
    connect(edgeAlphaSlider_, &QSlider::valueChanged, this, [this](int v){
        edgeAlphaValue_->setText(QString("%1%").arg(v));
        emit edgeAlphaChanged(v);
    });
    connect(surfaceAlphaSlider_, &QSlider::valueChanged, this, [this](int v){
        surfaceAlphaValue_->setText(QString("%1%").arg(v));
        emit surfaceAlphaChanged(v);
    });
    connect(resetBtn_, &QPushButton::clicked, this, [this]{ resetToDefaults(); });
}

void DisplayPanel::resetToDefaults() {
    modeCombo_->setCurrentIndex(2);  // 实体+线框
    projCombo_->setCurrentIndex(0);  // 透视
    edgeWidthSpin_->setValue(1);
    edgeAlphaSlider_->setValue(100);
    surfaceAlphaSlider_->setValue(100);
    // 颜色按钮不会自动发信号，手动复位色块并发射
    setSwatch(objColorBtn_,  toQColor(kDefaultObjColor));
    setSwatch(edgeColorBtn_, toQColor(kDefaultEdgeColor));
    emit objectColorChanged(kDefaultObjColor);
    emit edgeColorChanged(kDefaultEdgeColor);
}

QPushButton* DisplayPanel::makeColorButton(const QColor& initial,
                                           std::function<void(const glm::vec3&)> onPicked) {
    auto* btn = new QPushButton;
    btn->setFixedSize(48, 22);
    setSwatch(btn, initial);
    connect(btn, &QPushButton::clicked, this, [this, btn, onPicked]() {
        QColor cur = btn->property("swatch").value<QColor>();
        QColor picked = QColorDialog::getColor(cur, this, "选择颜色");
        if (picked.isValid()) {
            setSwatch(btn, picked);
            onPicked(toVec3(picked));
        }
    });
    return btn;
}

void DisplayPanel::applyTheme(const Theme& t) {
    setStyleSheet(QString(
        "QWidget { background: %1; color: %2; }"
        "QGroupBox {"
        "  background: %3; border: 1px solid %4;"
        "  border-radius: 8px; margin-top: 14px; padding: 14px 10px 10px 10px;"
        "  font-weight: bold; font-size: 12px; color: %5; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 12px; padding: 0 6px; color: %6; }"
        "QSpinBox, QComboBox {"
        "  background: %3; border: 1px solid %4; border-radius: 4px;"
        "  padding: 3px 8px; color: %2; min-height: 22px; }"
        "QSpinBox:focus, QComboBox:focus { border-color: %6; }"
        "QComboBox QAbstractItemView {"
        "  background: %3; border: 1px solid %4; border-radius: 4px;"
        "  padding: 3px; selection-background-color: %4;"
        "  selection-color: %2; color: %2; outline: none; }"
        "QPushButton {"
        "  background: %4; border: 1px solid %4; border-radius: 4px;"
        "  padding: 4px 12px; color: %2; min-height: 18px; }"
        "QPushButton:hover { background: %7; }"
        "QLabel { color: %2; }"
        "QSlider::groove:horizontal {"
        "  height: 4px; background: %4; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: %6; border-radius: 2px; }"
        "QSlider::handle:horizontal {"
        "  width: 12px; margin: -5px 0; border-radius: 6px; background: %6; }"
    ).arg(t.base, t.text, t.mantle, t.surface0, t.subtext0, t.teal, t.surface1));

    setStyleSheet(styleSheet()
                  + StyleHelpers::comboArrowStyle(t.teal, t.surface0)
                  + StyleHelpers::spinArrowStyle(t.teal, t.mantle, t.surface1, t.surface0));

    // 色块按钮的背景由各自 swatch 样式控制，重设以免被上面的全局 QPushButton 样式覆盖
    if (objColorBtn_)  setSwatch(objColorBtn_,  objColorBtn_->property("swatch").value<QColor>());
    if (edgeColorBtn_) setSwatch(edgeColorBtn_, edgeColorBtn_->property("swatch").value<QColor>());
}
