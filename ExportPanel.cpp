/**
 * @file ExportPanel.cpp
 * @brief 截图 / 录像导出面板实现
 */

#include "ExportPanel.h"
#include "Theme.h"
#include "StyleHelpers.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QStandardPaths>

namespace {
// 自动单位：bytes 太小直接显示 B，否则按 KB / MB / GB 切换
QString formatBytes(qint64 bytes) {
    constexpr qint64 KB = 1024;
    constexpr qint64 MB = 1024 * 1024;
    constexpr qint64 GB = 1024 * 1024 * 1024;
    if (bytes < KB) return QString("%1 B").arg(bytes);
    if (bytes < MB) return QString("%1 KB").arg(bytes / static_cast<double>(KB), 0, 'f', 1);
    if (bytes < GB) return QString("%1 MB").arg(bytes / static_cast<double>(MB), 0, 'f', 2);
    return QString("%1 GB").arg(bytes / static_cast<double>(GB), 0, 'f', 2);
}
}

ExportPanel::ExportPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ── 输出目录 ──
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("输出目录:"));
        outputDirEdit_ = new QLineEdit;
        outputDirEdit_->setText(
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
        row->addWidget(outputDirEdit_, 1);
        browseBtn_ = new QPushButton("浏览…");
        browseBtn_->setFixedWidth(64);
        row->addWidget(browseBtn_);
        root->addLayout(row);
    }

    // ── 参数（帧率 / 时长上限 / 分辨率） ──
    {
        auto* paramBox = new QGroupBox("参数");
        auto* grid = new QGridLayout(paramBox);
        grid->setContentsMargins(12, 16, 12, 12);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(8);
        grid->setColumnStretch(2, 1);  // 右侧空列吸收剩余空间，控件靠左不漂浮

        auto makeLabel = [](const QString& text) {
            auto* l = new QLabel(text);
            l->setFixedWidth(72);
            l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            return l;
        };

        fpsSpin_ = new QSpinBox;
        fpsSpin_->setRange(1, 120);
        fpsSpin_->setValue(30);
        fpsSpin_->setSuffix(" fps");
        fpsSpin_->setFixedWidth(120);
        grid->addWidget(makeLabel("帧率"), 0, 0);
        grid->addWidget(fpsSpin_,          0, 1);

        maxDurSpin_ = new QSpinBox;
        maxDurSpin_->setRange(1, 3600);
        maxDurSpin_->setValue(60);
        maxDurSpin_->setSuffix(" 秒");
        maxDurSpin_->setFixedWidth(120);
        grid->addWidget(makeLabel("最长时长"), 1, 0);
        grid->addWidget(maxDurSpin_,          1, 1);

        resolutionCombo_ = new QComboBox;
        resolutionCombo_->addItem("跟随当前视口", 0);
        resolutionCombo_->addItem("1920 × 1080", 1);
        resolutionCombo_->addItem("自定义", 2);
        resolutionCombo_->setFixedWidth(180);
        grid->addWidget(makeLabel("分辨率"), 2, 0);
        grid->addWidget(resolutionCombo_,    2, 1);

        // 自定义分辨率行
        customResRow_ = new QWidget;
        auto* customRowLayout = new QHBoxLayout(customResRow_);
        customRowLayout->setContentsMargins(0, 0, 0, 0);
        customRowLayout->setSpacing(6);
        customWSpin_ = new QSpinBox;
        customWSpin_->setRange(64, 7680);
        customWSpin_->setValue(1280);
        customWSpin_->setSuffix(" px");
        customWSpin_->setFixedWidth(110);
        customHSpin_ = new QSpinBox;
        customHSpin_->setRange(64, 4320);
        customHSpin_->setValue(720);
        customHSpin_->setSuffix(" px");
        customHSpin_->setFixedWidth(110);
        customRowLayout->addWidget(customWSpin_);
        customRowLayout->addWidget(new QLabel("×"));
        customRowLayout->addWidget(customHSpin_);
        customRowLayout->addStretch();
        customResRow_->setVisible(false);
        grid->addWidget(customResRow_, 3, 1, 1, 2);

        root->addWidget(paramBox);
    }

    // ── 操作按钮 ──
    {
        auto* row = new QHBoxLayout;
        screenshotBtn_ = new QPushButton("截图");
        startBtn_      = new QPushButton("开始录制");
        stopBtn_       = new QPushButton("停止录制");
        stopBtn_->setEnabled(false);
        row->addWidget(screenshotBtn_);
        row->addWidget(startBtn_);
        row->addWidget(stopBtn_);
        row->addStretch();
        root->addLayout(row);
    }

    // ── 状态 ──
    ffmpegStatusLabel_ = new QLabel("ffmpeg 检测中…");
    ffmpegStatusLabel_->setStyleSheet("font-size: 11px;");
    root->addWidget(ffmpegStatusLabel_);

    recStatusLabel_ = new QLabel(" ");
    recStatusLabel_->setStyleSheet("font-size: 11px; font-family: monospace;");
    root->addWidget(recStatusLabel_);

    root->addStretch();

    // ── 信号 ──
    connect(browseBtn_, &QPushButton::clicked, this, &ExportPanel::onBrowseDir);
    connect(resolutionCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportPanel::onResolutionModeChanged);
    connect(outputDirEdit_, &QLineEdit::editingFinished, this, [this]() {
        emit outputDirChanged(outputDirEdit_->text());
    });
    connect(screenshotBtn_, &QPushButton::clicked,
            this, &ExportPanel::screenshotRequested);
    connect(startBtn_, &QPushButton::clicked,
            this, &ExportPanel::recordStartRequested);
    connect(stopBtn_, &QPushButton::clicked,
            this, &ExportPanel::recordStopRequested);

    // 录制按钮默认禁用，等 ffmpeg 检测结果出来再决定
    startBtn_->setEnabled(false);
}

void ExportPanel::onBrowseDir() {
    QString dir = QFileDialog::getExistingDirectory(
        this, "选择输出目录", outputDirEdit_->text());
    if (!dir.isEmpty()) {
        outputDirEdit_->setText(dir);
        emit outputDirChanged(dir);
    }
}

void ExportPanel::onResolutionModeChanged(int index) {
    customResRow_->setVisible(index == 2);
}

void ExportPanel::setFfmpegAvailable(bool available) {
    ffmpegAvailable_ = available;
    if (available) {
        ffmpegStatusLabel_->setText("ffmpeg ✓ 已检测到");
        ffmpegStatusLabel_->setStyleSheet("font-size: 11px; color: #88c070;");
    } else {
        ffmpegStatusLabel_->setText(
            "未检测到 ffmpeg，无法录制视频。macOS: brew install ffmpeg");
        ffmpegStatusLabel_->setStyleSheet("font-size: 11px; color: #d08770;");
    }
    if (!recording_) {
        startBtn_->setEnabled(available);
    }
}

void ExportPanel::setRecording(bool recording) {
    recording_ = recording;
    // 录制中禁掉参数控件，防止中途改帧率/分辨率
    outputDirEdit_->setEnabled(!recording);
    browseBtn_->setEnabled(!recording);
    fpsSpin_->setEnabled(!recording);
    maxDurSpin_->setEnabled(!recording);
    resolutionCombo_->setEnabled(!recording);
    customWSpin_->setEnabled(!recording);
    customHSpin_->setEnabled(!recording);

    screenshotBtn_->setEnabled(!recording);
    startBtn_->setEnabled(!recording && ffmpegAvailable_);
    stopBtn_->setEnabled(recording);

    if (!recording) recStatusLabel_->setText(" ");
}

void ExportPanel::updateRecordingStats(int frames, int droppedFrames) {
    double sec = frames / static_cast<double>(qMax(1, fpsSpin_->value()));
    QString text = QString("录制中  帧数: %1   时长: %2s")
        .arg(frames).arg(sec, 0, 'f', 1);
    if (droppedFrames > 0)
        text += QString("   丢帧: %1").arg(droppedFrames);
    recStatusLabel_->setText(text);
}

void ExportPanel::setRecordingFinished(int frames, qint64 finalBytes, int droppedFrames) {
    double sec = frames / static_cast<double>(qMax(1, fpsSpin_->value()));
    QString text = QString("已完成  帧数: %1   时长: %2s   大小: %3")
        .arg(frames).arg(sec, 0, 'f', 1).arg(formatBytes(finalBytes));
    if (droppedFrames > 0)
        text += QString("   丢帧: %1").arg(droppedFrames);
    recStatusLabel_->setText(text);
}

QString ExportPanel::outputDir() const     { return outputDirEdit_->text(); }
int  ExportPanel::framerate() const        { return fpsSpin_->value(); }
int  ExportPanel::maxDurationSec() const   { return maxDurSpin_->value(); }
int  ExportPanel::resolutionMode() const   { return resolutionCombo_->currentData().toInt(); }
int  ExportPanel::customWidth() const      { return customWSpin_->value(); }
int  ExportPanel::customHeight() const     { return customHSpin_->value(); }

void ExportPanel::applyTheme(const Theme& t) {
    setStyleSheet(QString(
        "QWidget { background: %1; color: %2; }"
        "QGroupBox {"
        "  background: %3; border: 1px solid %4;"
        "  border-radius: 8px; margin-top: 14px; padding: 14px 10px 10px 10px;"
        "  font-weight: bold; font-size: 12px; color: %5; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 12px; padding: 0 6px; color: %6; }"
        "QLineEdit, QSpinBox, QComboBox {"
        "  background: %3; border: 1px solid %4; border-radius: 4px;"
        "  padding: 3px 8px; color: %2; min-height: 22px; }"
        "QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: %6; }"
        "QComboBox QAbstractItemView {"
        "  background: %3; border: 1px solid %4; border-radius: 4px;"
        "  padding: 3px; selection-background-color: %4;"
        "  selection-color: %2; color: %2; outline: none; }"
        "QPushButton {"
        "  background: %4; border: 1px solid %4; border-radius: 4px;"
        "  padding: 4px 12px; color: %2; min-height: 18px; }"
        "QPushButton:hover { background: %7; }"
        "QPushButton:pressed { background: %6; color: %1; }"
        "QPushButton:disabled { color: %8; background: %3; }"
        "QLabel { color: %2; }"
    ).arg(t.base, t.text, t.mantle, t.surface0, t.subtext0, t.teal,
          t.surface1, t.overlay0));

    // 追加箭头样式（SVG）
    setStyleSheet(styleSheet()
                  + StyleHelpers::comboArrowStyle(t.teal, t.surface0)
                  + StyleHelpers::spinArrowStyle(t.teal, t.mantle, t.surface1, t.surface0));
}
