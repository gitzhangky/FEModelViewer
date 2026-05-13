/**
 * @file ResultPanel.cpp
 * @brief 右侧结果面板实现
 *
 * 级联选择：工况 → 类型 → 分量 → 色谱
 */

#include "ResultPanel.h"
#include "Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>

ResultPanel::ResultPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void ResultPanel::setupUI()
{
    setMinimumWidth(140);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);

    // ── 卡片：结果显示 ──
    resultGroup_ = new QGroupBox("结果显示");
    auto* cardLayout = new QVBoxLayout(resultGroup_);
    cardLayout->setContentsMargins(8, 8, 8, 8);
    cardLayout->setSpacing(6);

    // 左右结构行
    auto addRow = [&](const QString& text, QComboBox*& combo, bool enabled = false) {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        auto* label = new QLabel(text);
        rowLabels_.push_back(label);
        label->setFixedWidth(50);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(label);
        combo = new QComboBox;
        combo->setEnabled(enabled);
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row->addWidget(combo);
        cardLayout->addLayout(row);
    };

    addRow("帧", subcaseCombo_);
    addRow("类型", typeCombo_);
    addRow("分量", componentCombo_);
    // 色谱（暂时隐藏）
    colormapCombo_ = new QComboBox;
    colormapCombo_->addItem("Jet");
    colormapCombo_->setCurrentIndex(0);
    colormapCombo_->hide();

    // ── 按钮行 ──
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(6);

    applyBtn_ = new QPushButton("应用");
    applyBtn_->setEnabled(false);
    btnRow->addWidget(applyBtn_);

    clearBtn_ = new QPushButton("清除");
    clearBtn_->setEnabled(false);
    btnRow->addWidget(clearBtn_);

    cardLayout->addLayout(btnRow);

    // ── 信息标签 ──
    infoLabel_ = new QLabel;
    infoLabel_->setWordWrap(true);
    cardLayout->addWidget(infoLabel_);

    layout->addWidget(resultGroup_);
    layout->addStretch(1);

    // 默认主题在 MainWindow 中统一调用 applyTheme() 设置

    // ── 信号连接 ──
    connect(subcaseCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ResultPanel::onFrameChanged);
    connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ResultPanel::onTypeChanged);
    connect(componentCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ResultPanel::onComponentChanged);
    connect(applyBtn_, &QPushButton::clicked, this, &ResultPanel::onApplyClicked);
    connect(clearBtn_, &QPushButton::clicked, this, &ResultPanel::onClearClicked);
}

void ResultPanel::setResults(const FEResultData& results)
{
    repo_ = FEResultRepository::fromResultData(results);
    populateFrameCombo();
}

void ResultPanel::setRepository(const FEResultRepository& repo)
{
    repo_ = repo;
    populateFrameCombo();
}

void ResultPanel::populateFrameCombo()
{
    subcaseCombo_->blockSignals(true);
    subcaseCombo_->clear();
    for (int i = 0; i < repo_.frameCount(); ++i) {
        const FEResultFrame* f = repo_.frame(i);
        subcaseCombo_->addItem(QString::fromStdString(f->info.valueLabel));
    }
    subcaseCombo_->blockSignals(false);

    bool hasData = !repo_.empty();
    subcaseCombo_->setEnabled(hasData);

    if (hasData) {
        subcaseCombo_->setCurrentIndex(0);
        onFrameChanged(0);
    }

    infoLabel_->setText(hasData
        ? QString("%1 个帧已加载").arg(repo_.frameCount())
        : "无结果数据");
}

void ResultPanel::clearResults()
{
    repo_.clear();
    subcaseCombo_->clear();
    typeCombo_->clear();
    componentCombo_->clear();
    subcaseCombo_->setEnabled(false);
    typeCombo_->setEnabled(false);
    componentCombo_->setEnabled(false);
    applyBtn_->setEnabled(false);
    clearBtn_->setEnabled(false);
    infoLabel_->setText("");
}

void ResultPanel::onFrameChanged(int index)
{
    (void)index;
    refreshTypes();
}

void ResultPanel::onTypeChanged(int index)
{
    (void)index;
    refreshComponents();
}

void ResultPanel::onComponentChanged(int index)
{
    applyBtn_->setEnabled(index >= 0);
}

void ResultPanel::refreshTypes()
{
    typeCombo_->blockSignals(true);
    typeCombo_->clear();

    int frameIdx = subcaseCombo_->currentIndex();
    const FEResultFrame* f = repo_.frame(frameIdx);
    if (f) {
        for (const auto& rt : f->resultTypes) {
            typeCombo_->addItem(QString::fromStdString(rt.name));
        }
    }

    typeCombo_->blockSignals(false);
    typeCombo_->setEnabled(typeCombo_->count() > 0);

    if (typeCombo_->count() > 0) {
        typeCombo_->setCurrentIndex(0);
        onTypeChanged(0);
    } else {
        refreshComponents();
    }
}

void ResultPanel::refreshComponents()
{
    componentCombo_->blockSignals(true);
    componentCombo_->clear();

    int frameIdx = subcaseCombo_->currentIndex();
    int rtIdx = typeCombo_->currentIndex();
    const FEResultFrame* f = repo_.frame(frameIdx);
    if (f && rtIdx >= 0 && rtIdx < static_cast<int>(f->resultTypes.size())) {
        const auto& rt = f->resultTypes[rtIdx];
        for (const auto& comp : rt.components) {
            componentCombo_->addItem(QString::fromStdString(comp.name));
        }
    }

    componentCombo_->blockSignals(false);
    componentCombo_->setEnabled(componentCombo_->count() > 0);
    applyBtn_->setEnabled(componentCombo_->count() > 0);

    if (componentCombo_->count() > 0)
        componentCombo_->setCurrentIndex(0);
}

void ResultPanel::onApplyClicked()
{
    int frameIdx = subcaseCombo_->currentIndex();
    int rtIdx = typeCombo_->currentIndex();
    int cpIdx = componentCombo_->currentIndex();

    if (frameIdx < 0 || rtIdx < 0 || cpIdx < 0) return;

    const FEResultFrame* f = repo_.frame(frameIdx);
    if (!f) return;
    if (rtIdx >= static_cast<int>(f->resultTypes.size())) return;

    const auto& rt = f->resultTypes[rtIdx];
    if (cpIdx >= static_cast<int>(rt.components.size())) return;

    const auto& comp = rt.components[cpIdx];

    QString title = QString("%1 - %2 - %3")
        .arg(QString::fromStdString(f->info.valueLabel))
        .arg(QString::fromStdString(rt.name))
        .arg(QString::fromStdString(comp.name));

    clearBtn_->setEnabled(true);
    emit applyResult(comp.field, title);
}

void ResultPanel::onClearClicked()
{
    clearBtn_->setEnabled(false);
    emit clearResult();
}

void ResultPanel::applyTheme(const Theme& t) {
    // 按钮单独设样式
    applyBtn_->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: %2; border: none;"
        "  border-radius: 5px; padding: 7px 16px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }"
        "QPushButton:disabled { background: %5; color: %6; }"
    ).arg(t.blue, t.btnText, t.blueHover, t.bluePressed, t.surface1, t.overlay0));

    clearBtn_->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: %2; border: 1px solid %3;"
        "  border-radius: 5px; padding: 7px 16px; font-size: 12px; }"
        "QPushButton:hover { background: %3; border-color: %4; color: %4; }"
        "QPushButton:pressed { background: %5; }"
        "QPushButton:disabled { background: %6; color: %3; border-color: %1; }"
    ).arg(t.surface0, t.text, t.surface1, t.blue, t.surface2, t.base));

    // 卡片 + ComboBox 统一主题
    setStyleSheet(QString(
        "QWidget { background: %1; color: %2; }"
        "QGroupBox {"
        "  background: %3; border: 1px solid %4;"
        "  border-radius: 8px; margin-top: 14px; padding: 14px 10px 10px 10px;"
        "  font-weight: bold; font-size: 12px; color: %5; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 12px; padding: 0 6px;"
        "  color: %6; }"
        "QLabel { font-size: 11px; color: %7; padding: 1px 0; }"
        "QComboBox {"
        "  background: %4; border: 1px solid %8; border-radius: 5px;"
        "  padding: 5px 10px; font-size: 12px; color: %2;"
        "  min-height: 24px; }"
        "QComboBox:hover { border-color: %6; }"
        "QComboBox:disabled { background: %1; color: %8; border-color: %4; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox::down-arrow {"
        "  image: none; border-left: 5px solid transparent;"
        "  border-right: 5px solid transparent;"
        "  border-top: 6px solid %6; margin-right: 8px; }"
        "QComboBox QAbstractItemView {"
        "  background: %3; border: 1px solid %8; border-radius: 4px;"
        "  padding: 4px; selection-background-color: %8; color: %2;"
        "  outline: none; }"
    ).arg(t.base, t.text, t.mantle, t.surface0, t.subtext0, t.blue, t.subtext1, t.surface1));
}
