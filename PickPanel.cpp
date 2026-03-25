/**
 * @file PickPanel.cpp
 * @brief 右侧拾取控制面板实现
 *
 * 单个 QGroupBox 卡片，内含拾取模式、显示控制、ID标签三个区域。
 * 纯界面面板，渲染逻辑后续开发。
 */

#include "PickPanel.h"
#include "Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>

PickPanel::PickPanel(QWidget* parent)
    : QWidget(parent)
{
    setMinimumWidth(140);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // ════════════════════════════════════════
    // 单个卡片：拾取
    // ════════════════════════════════════════
    pickGroup_ = new QGroupBox("拾取");
    auto* cardLayout = new QVBoxLayout(pickGroup_);
    cardLayout->setContentsMargins(8, 8, 8, 8);
    cardLayout->setSpacing(6);

    // ── 拾取模式 ──
    modeGroup_ = new QButtonGroup(this);
    modeGroup_->setExclusive(true);

    nodeRadio_ = new QRadioButton("节点");
    elemRadio_ = new QRadioButton("单元");
    partRadio_ = new QRadioButton("部件");
    nodeRadio_->setChecked(true);

    modeGroup_->addButton(nodeRadio_, 0);  // PickMode::Node
    modeGroup_->addButton(elemRadio_, 1);  // PickMode::Element
    modeGroup_->addButton(partRadio_, 2);  // PickMode::Part

    auto* modeRow = new QHBoxLayout;
    modeRow->setSpacing(10);
    modeRow->addWidget(nodeRadio_);
    modeRow->addWidget(elemRadio_);
    modeRow->addWidget(partRadio_);
    modeRow->addStretch();
    cardLayout->addLayout(modeRow);

    // ── 分隔线 ──
    auto* sep1 = new QFrame;
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFrameShadow(QFrame::Plain);
    sep1->setFixedHeight(1);
    cardLayout->addWidget(sep1);

    // ── 显示控制 ──
    visLabel_ = new QLabel("显示");
    cardLayout->addWidget(visLabel_);

    nodeVisCheck_ = new QCheckBox("节点");
    elemVisCheck_ = new QCheckBox("单元");
    partVisCheck_ = new QCheckBox("部件");
    nodeVisCheck_->setChecked(false);
    elemVisCheck_->setChecked(true);
    partVisCheck_->setChecked(true);

    auto* visRow = new QHBoxLayout;
    visRow->setSpacing(10);
    visRow->addWidget(nodeVisCheck_);
    visRow->addWidget(elemVisCheck_);
    visRow->addWidget(partVisCheck_);
    visRow->addStretch();
    cardLayout->addLayout(visRow);

    // ── 分隔线 ──
    auto* sep2 = new QFrame;
    sep2->setFrameShape(QFrame::HLine);
    sep2->setFrameShadow(QFrame::Plain);
    sep2->setFixedHeight(1);
    cardLayout->addWidget(sep2);

    // ── ID标签 ──
    labelLabel_ = new QLabel("标签");
    cardLayout->addWidget(labelLabel_);

    nodeLabelCheck_ = new QCheckBox("节点ID");
    elemLabelCheck_ = new QCheckBox("单元ID");
    partLabelCheck_ = new QCheckBox("部件ID");
    nodeLabelCheck_->setChecked(false);
    elemLabelCheck_->setChecked(false);
    partLabelCheck_->setChecked(false);

    auto* labelRow = new QHBoxLayout;
    labelRow->setSpacing(10);
    labelRow->addWidget(nodeLabelCheck_);
    labelRow->addWidget(elemLabelCheck_);
    labelRow->addWidget(partLabelCheck_);
    labelRow->addStretch();
    cardLayout->addLayout(labelRow);

    layout->addWidget(pickGroup_);

    // ── 信号连接 ──
    connect(modeGroup_, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &PickPanel::pickModeChanged);

    connect(nodeVisCheck_, &QCheckBox::toggled, this, &PickPanel::nodeVisibilityChanged);
    connect(elemVisCheck_, &QCheckBox::toggled, this, &PickPanel::elementVisibilityChanged);
    connect(partVisCheck_, &QCheckBox::toggled, this, &PickPanel::partVisibilityChanged);

    connect(nodeLabelCheck_, &QCheckBox::toggled, this, &PickPanel::nodeLabelChanged);
    connect(elemLabelCheck_, &QCheckBox::toggled, this, &PickPanel::elementLabelChanged);
    connect(partLabelCheck_, &QCheckBox::toggled, this, &PickPanel::partLabelChanged);
}

void PickPanel::applyTheme(const Theme& t)
{
    // 与左侧 FEModelPanel 一致的 QGroupBox 卡片风格
    setStyleSheet(QString(
        "QWidget { background: %1; color: %2; }"
        "QGroupBox {"
        "  background: %3; border: 1px solid %4;"
        "  border-radius: 6px; margin-top: 12px; padding: 16px 8px 8px 8px;"
        "  font-weight: bold; font-size: 12px; color: %5; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 10px; padding: 0 4px;"
        "  color: %6; }"
        "QLabel { font-size: 11px; font-weight: bold; color: %7; }"
        "QRadioButton { font-size: 12px; color: %2; spacing: 4px; }"
        "QRadioButton::indicator {"
        "  width: 14px; height: 14px; border-radius: 7px;"
        "  border: 2px solid %8; background: %4; }"
        "QRadioButton::indicator:checked {"
        "  background: %6; border-color: %6; }"
        "QRadioButton::indicator:hover { border-color: %6; }"
        "QCheckBox { font-size: 12px; color: %2; spacing: 6px; padding: 1px 0; }"
        "QCheckBox::indicator {"
        "  width: 14px; height: 14px; border-radius: 3px;"
        "  border: 2px solid %8; background: %4; }"
        "QCheckBox::indicator:checked {"
        "  background: %6; border-color: %6; }"
        "QCheckBox::indicator:hover { border-color: %6; }"
        "QFrame[frameShape=\"4\"] { background: %4; border: none; }"
    ).arg(t.base, t.text, t.mantle, t.surface0, t.subtext0, t.blue, t.subtext1, t.surface2));
}
