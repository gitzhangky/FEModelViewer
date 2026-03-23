/**
 * @file ResultPanel.cpp
 * @brief 右侧结果面板实现
 *
 * 级联选择：工况 → 类型 → 分量 → 色谱
 */

#include "ResultPanel.h"

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
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ── 标题 ──
    auto* titleLabel = new QLabel("结果显示");
    titleLabel->setStyleSheet(
        "font-size: 13px; font-weight: bold; color: #89b4fa; padding: 4px 0;");
    layout->addWidget(titleLabel);

    // 左右结构行
    auto addRow = [&](const QString& text, QComboBox*& combo, bool enabled = false) {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        auto* label = new QLabel(text);
        label->setStyleSheet("font-size: 11px; color: #a6adc8;");
        label->setFixedWidth(50);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(label);
        combo = new QComboBox;
        combo->setEnabled(enabled);
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row->addWidget(combo);
        layout->addLayout(row);
    };

    addRow("工况", subcaseCombo_);
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
    applyBtn_->setStyleSheet(
        "QPushButton {"
        "  background: #89b4fa; color: #1e1e2e; border: none;"
        "  border-radius: 4px; padding: 6px 12px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: #b4d0fb; }"
        "QPushButton:pressed { background: #74a8f7; }"
        "QPushButton:disabled { background: #45475a; color: #6c7086; }");
    btnRow->addWidget(applyBtn_);

    clearBtn_ = new QPushButton("清除");
    clearBtn_->setEnabled(false);
    clearBtn_->setStyleSheet(
        "QPushButton {"
        "  background: #313244; color: #cdd6f4; border: 1px solid #45475a;"
        "  border-radius: 4px; padding: 6px 12px; font-size: 12px; }"
        "QPushButton:hover { background: #45475a; border-color: #89b4fa; }"
        "QPushButton:pressed { background: #585b70; }"
        "QPushButton:disabled { background: #1e1e2e; color: #45475a; border-color: #313244; }");
    btnRow->addWidget(clearBtn_);

    layout->addLayout(btnRow);

    // ── 信息标签 ──
    infoLabel_ = new QLabel;
    infoLabel_->setStyleSheet("font-size: 10px; color: #6c7086; padding-top: 4px;");
    infoLabel_->setWordWrap(true);
    layout->addWidget(infoLabel_);

    layout->addStretch(1);

    // ── 面板样式（Catppuccin Mocha） ──
    setStyleSheet(
        "QWidget { background: #1e1e2e; color: #cdd6f4; }"
        "QComboBox {"
        "  background: #313244; border: 1px solid #45475a; border-radius: 4px;"
        "  padding: 4px 8px; font-size: 12px; color: #cdd6f4;"
        "  min-height: 22px; }"
        "QComboBox:hover { border-color: #89b4fa; }"
        "QComboBox:disabled { background: #1e1e2e; color: #45475a; border-color: #313244; }"
        "QComboBox::drop-down {"
        "  border: none; width: 20px; }"
        "QComboBox::down-arrow {"
        "  image: none; border-left: 4px solid transparent;"
        "  border-right: 4px solid transparent;"
        "  border-top: 5px solid #89b4fa; margin-right: 6px; }"
        "QComboBox QAbstractItemView {"
        "  background: #313244; border: 1px solid #45475a;"
        "  selection-background-color: #45475a; color: #cdd6f4; }");

    // ── 信号连接 ──
    connect(subcaseCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ResultPanel::onSubcaseChanged);
    connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ResultPanel::onTypeChanged);
    connect(componentCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ResultPanel::onComponentChanged);
    connect(applyBtn_, &QPushButton::clicked, this, &ResultPanel::onApplyClicked);
    connect(clearBtn_, &QPushButton::clicked, this, &ResultPanel::onClearClicked);
}

void ResultPanel::setResults(const FEResultData& results)
{
    results_ = results;

    subcaseCombo_->blockSignals(true);
    subcaseCombo_->clear();
    for (const auto& sc : results_.subcases) {
        subcaseCombo_->addItem(QString::fromStdString(sc.name));
    }
    subcaseCombo_->blockSignals(false);

    bool hasData = !results_.subcases.empty();
    subcaseCombo_->setEnabled(hasData);

    if (hasData) {
        subcaseCombo_->setCurrentIndex(0);
        onSubcaseChanged(0);
    }

    infoLabel_->setText(hasData
        ? QString("%1 个工况已加载").arg(results_.subcases.size())
        : "无结果数据");
}

void ResultPanel::clearResults()
{
    results_.clear();
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

void ResultPanel::onSubcaseChanged(int index)
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

    int scIdx = subcaseCombo_->currentIndex();
    if (scIdx >= 0 && scIdx < static_cast<int>(results_.subcases.size())) {
        const auto& sc = results_.subcases[scIdx];
        for (const auto& rt : sc.resultTypes) {
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

    int scIdx = subcaseCombo_->currentIndex();
    int rtIdx = typeCombo_->currentIndex();
    if (scIdx >= 0 && scIdx < static_cast<int>(results_.subcases.size()) &&
        rtIdx >= 0 && rtIdx < static_cast<int>(results_.subcases[scIdx].resultTypes.size())) {
        const auto& rt = results_.subcases[scIdx].resultTypes[rtIdx];
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
    int scIdx = subcaseCombo_->currentIndex();
    int rtIdx = typeCombo_->currentIndex();
    int cpIdx = componentCombo_->currentIndex();

    if (scIdx < 0 || rtIdx < 0 || cpIdx < 0) return;
    if (scIdx >= static_cast<int>(results_.subcases.size())) return;

    const auto& sc = results_.subcases[scIdx];
    if (rtIdx >= static_cast<int>(sc.resultTypes.size())) return;

    const auto& rt = sc.resultTypes[rtIdx];
    if (cpIdx >= static_cast<int>(rt.components.size())) return;

    const auto& comp = rt.components[cpIdx];

    QString title = QString("%1 - %2 - %3")
        .arg(QString::fromStdString(sc.name))
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
