/**
 * @file PickPanel.cpp
 * @brief 右侧拾取控制面板实现
 *
 * 单个 QGroupBox 卡片，内含拾取模式、显示控制、ID标签三个区域。
 * 显隐和标签的操作对象由当前拾取模式决定。
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
    visCheck_ = new QCheckBox("显示");
    visCheck_->setChecked(true);
    cardLayout->addWidget(visCheck_);

    // ── 分隔线 ──
    auto* sep2 = new QFrame;
    sep2->setFrameShape(QFrame::HLine);
    sep2->setFrameShadow(QFrame::Plain);
    sep2->setFixedHeight(1);
    cardLayout->addWidget(sep2);

    // ── ID标签 ──
    labelCheck_ = new QCheckBox("标签");
    labelCheck_->setChecked(false);
    cardLayout->addWidget(labelCheck_);

    layout->addWidget(pickGroup_);

    // ── 信号连接 ──
    // Qt 5.15 引入 idClicked，低版本使用 buttonClicked(int)
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(modeGroup_, &QButtonGroup::idClicked,
            this, &PickPanel::pickModeChanged);
#else
    connect(modeGroup_, QOverload<int>::of(&QButtonGroup::buttonClicked),
            this, &PickPanel::pickModeChanged);
#endif

    connect(visCheck_,   &QCheckBox::toggled, this, &PickPanel::visibilityChanged);
    connect(labelCheck_, &QCheckBox::toggled, this, &PickPanel::labelChanged);
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
