/**
 * @file AppearancePanel.cpp
 * @brief 外观控制面板实现
 */

#include "AppearancePanel.h"
#include "Theme.h"
#include "StyleHelpers.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QColorDialog>

namespace {
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

AppearancePanel::AppearancePanel(QWidget* parent) : QWidget(parent) {
    // 横向布局：背景组与色谱组并排，省高度
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto makeLabel = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setFixedWidth(64);
        l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        return l;
    };

    auto bindColorButton = [this](QPushButton* btn) {
        connect(btn, &QPushButton::clicked, this, [this, btn]() {
            QColor cur = btn->property("swatch").value<QColor>();
            QColor picked = QColorDialog::getColor(cur, this, "选择颜色");
            if (picked.isValid()) {
                setSwatch(btn, picked);
                emitBackground();
            }
        });
    };

    // ── 背景分组 ──
    {
        auto* box = new QGroupBox("背景");
        auto* grid = new QGridLayout(box);
        grid->setContentsMargins(12, 16, 12, 12);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(8);
        grid->setColumnStretch(2, 1);

        topColorBtn_ = new QPushButton; topColorBtn_->setFixedSize(48, 22);
        botColorBtn_ = new QPushButton; botColorBtn_->setFixedSize(48, 22);
        setSwatch(topColorBtn_, QColor::fromRgbF(0.38, 0.45, 0.58));
        setSwatch(botColorBtn_, QColor::fromRgbF(0.68, 0.74, 0.82));
        bindColorButton(topColorBtn_);
        bindColorButton(botColorBtn_);
        grid->addWidget(makeLabel("顶部色"), 0, 0);
        grid->addWidget(topColorBtn_,        0, 1, Qt::AlignLeft);
        grid->addWidget(makeLabel("底部色"), 1, 0);
        grid->addWidget(botColorBtn_,        1, 1, Qt::AlignLeft);

        resetBgBtn_ = new QPushButton("恢复主题");
        grid->addWidget(resetBgBtn_, 2, 1, Qt::AlignLeft);

        root->addWidget(box);
    }

    // ── 云图色谱分组 ──
    {
        auto* box = new QGroupBox("云图色谱");
        auto* grid = new QGridLayout(box);
        grid->setContentsMargins(12, 16, 12, 12);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(8);
        grid->setColumnStretch(2, 1);

        colormapCombo_ = new QComboBox;
        colormapCombo_->addItem("Jet（彩虹）", 0);
        colormapCombo_->addItem("灰度", 1);
        colormapCombo_->addItem("冷暖", 2);
        colormapCombo_->setFixedWidth(160);
        grid->addWidget(makeLabel("色谱"), 0, 0);
        grid->addWidget(colormapCombo_,    0, 1);

        bandsSpin_ = new QSpinBox;
        bandsSpin_->setRange(2, 32);
        bandsSpin_->setValue(10);
        bandsSpin_->setFixedWidth(160);
        grid->addWidget(makeLabel("分段数"), 1, 0);
        grid->addWidget(bandsSpin_,          1, 1);

        invertCheck_ = new QCheckBox("反转色谱");
        grid->addWidget(invertCheck_, 2, 1, Qt::AlignLeft);

        resetColormapBtn_ = new QPushButton("恢复默认色谱");
        grid->addWidget(resetColormapBtn_, 3, 1, Qt::AlignLeft);

        root->addWidget(box);
    }

    root->addStretch();

    connect(resetBgBtn_, &QPushButton::clicked, this, [this]() {
        emit backgroundResetRequested();
    });
    connect(colormapCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i){ emit colormapChanged(colormapCombo_->itemData(i).toInt()); });
    connect(bandsSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v){ emit numBandsChanged(v); });
    connect(invertCheck_, &QCheckBox::toggled,
            this, [this](bool on){ emit colormapInvertedChanged(on); });
    connect(resetColormapBtn_, &QPushButton::clicked,
            this, [this]{ resetColormapDefaults(); });
}

void AppearancePanel::resetColormapDefaults() {
    colormapCombo_->setCurrentIndex(0);  // Jet
    bandsSpin_->setValue(10);
    invertCheck_->setChecked(false);
}

void AppearancePanel::emitBackground() {
    emit backgroundColorsChanged(
        toVec3(topColorBtn_->property("swatch").value<QColor>()),
        toVec3(botColorBtn_->property("swatch").value<QColor>()));
}

void AppearancePanel::applyTheme(const Theme& t) {
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
        "QCheckBox { color: %2; spacing: 6px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border-radius: 3px;"
        "  border: 1px solid %4; background: %3; }"
        "QCheckBox::indicator:checked { background: %6; border-color: %6; }"
    ).arg(t.base, t.text, t.mantle, t.surface0, t.subtext0, t.teal, t.surface1));

    setStyleSheet(styleSheet()
                  + StyleHelpers::comboArrowStyle(t.teal, t.surface0)
                  + StyleHelpers::spinArrowStyle(t.teal, t.mantle, t.surface1, t.surface0));

    // 背景色块复位为该主题预设（顶/底），并把"恢复主题"按钮样式与色块区分
    setSwatch(topColorBtn_, QColor::fromRgbF(t.bgTopR, t.bgTopG, t.bgTopB));
    setSwatch(botColorBtn_, QColor::fromRgbF(t.bgBotR, t.bgBotG, t.bgBotB));
    resetBgBtn_->setStyleSheet(QString(
        "QPushButton { background: %1; border: 1px solid %1; border-radius: 4px;"
        " padding: 4px 12px; color: %2; min-height: 18px; }"
        "QPushButton:hover { background: %3; }")
        .arg(t.surface0, t.text, t.surface1));
}
