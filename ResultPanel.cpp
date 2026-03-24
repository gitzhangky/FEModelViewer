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
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // ── 标题 ──
    titleLabel_ = new QLabel("结果显示");
    layout->addWidget(titleLabel_);

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
    btnRow->addWidget(applyBtn_);

    clearBtn_ = new QPushButton("清除");
    clearBtn_->setEnabled(false);
    btnRow->addWidget(clearBtn_);

    layout->addLayout(btnRow);

    // ── 信息标签 ──
    infoLabel_ = new QLabel;
    infoLabel_->setWordWrap(true);
    layout->addWidget(infoLabel_);

    layout->addStretch(1);

    // 默认主题在 MainWindow 中统一调用 applyTheme() 设置

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

void ResultPanel::applyTheme(const Theme& t) {
    titleLabel_->setStyleSheet(
        QString("font-size: 13px; font-weight: bold; color: %1; padding: 4px 0;").arg(t.blue));
    for (auto* lbl : rowLabels_)
        lbl->setStyleSheet(QString("font-size: 11px; color: %1;").arg(t.subtext0));
    infoLabel_->setStyleSheet(
        QString("font-size: 10px; color: %1; padding-top: 4px;").arg(t.overlay0));

    applyBtn_->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: %2; border: none;"
        "  border-radius: 4px; padding: 6px 12px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }"
        "QPushButton:disabled { background: %5; color: %6; }"
    ).arg(t.blue, t.btnText, t.blueHover, t.bluePressed, t.surface1, t.overlay0));

    clearBtn_->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: %2; border: 1px solid %3;"
        "  border-radius: 4px; padding: 6px 12px; font-size: 12px; }"
        "QPushButton:hover { background: %3; border-color: %4; }"
        "QPushButton:pressed { background: %5; }"
        "QPushButton:disabled { background: %6; color: %3; border-color: %1; }"
    ).arg(t.surface0, t.text, t.surface1, t.blue, t.surface2, t.base));

    setStyleSheet(QString(
        "QWidget { background: %1; color: %2; }"
        "QComboBox {"
        "  background: %3; border: 1px solid %4; border-radius: 4px;"
        "  padding: 4px 8px; font-size: 12px; color: %2;"
        "  min-height: 22px; }"
        "QComboBox:hover { border-color: %5; }"
        "QComboBox:disabled { background: %1; color: %4; border-color: %3; }"
        "QComboBox::drop-down {"
        "  border: none; width: 20px; }"
        "QComboBox::down-arrow {"
        "  image: none; border-left: 4px solid transparent;"
        "  border-right: 4px solid transparent;"
        "  border-top: 5px solid %5; margin-right: 6px; }"
        "QComboBox QAbstractItemView {"
        "  background: %3; border: 1px solid %4;"
        "  selection-background-color: %4; color: %2; }"
    ).arg(t.base, t.text, t.surface0, t.surface1, t.blue));
}
