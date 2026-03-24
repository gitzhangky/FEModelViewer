/**
 * @file MonitorPanel.cpp
 * @brief 实时监控面板实现
 */

#include "MonitorPanel.h"
#include "GLWidget.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include "Theme.h"

MonitorPanel::MonitorPanel(QWidget* parent) : QGroupBox("监控", parent) {
    // 默认主题在 MainWindow 中统一调用 applyTheme() 设置

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(4);

    // 创建各行信息标签（初始显示 "标签: --"）
    fpsLabel_       = makeRow(layout, "FPS");
    frameTimeLabel_ = makeRow(layout, "帧时间");
    vertexLabel_    = makeRow(layout, "顶点数");
    triangleLabel_  = makeRow(layout, "三角面");
    rendererLabel_  = makeRow(layout, "GPU");
    vendorLabel_    = makeRow(layout, "厂商");
    glVersionLabel_ = makeRow(layout, "OpenGL");
    glslLabel_      = makeRow(layout, "GLSL");
}

void MonitorPanel::bindToWidget(GLWidget* gl) {
    gl_ = gl;

    // GL 初始化完成后，一次性读取硬件信息（这些信息不会变化）
    connect(gl_, &GLWidget::glInitialized, this, [this]{
        rendererLabel_->setText("GPU: "     + gl_->glRenderer());
        vendorLabel_->setText("厂商: "      + gl_->gpuVendor());
        glVersionLabel_->setText("OpenGL: " + gl_->glVersion());
        glslLabel_->setText("GLSL: "        + gl_->glslVersion());
    });

    // 启动定时器，每 200ms 刷新一次动态数据（FPS、网格统计）
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MonitorPanel::refresh);
    timer->start(200);
}

void MonitorPanel::refresh() {
    if (!gl_) return;

    // 更新动态性能数据
    fpsLabel_->setText(QString("FPS: %1").arg(gl_->currentFps(), 0, 'f', 1));
    frameTimeLabel_->setText(QString("帧时间: %1 ms").arg(gl_->frameTimeMs(), 0, 'f', 2));
    vertexLabel_->setText(QString("顶点数: %1").arg(gl_->vertexCount()));
    triangleLabel_->setText(QString("三角面: %1").arg(gl_->triangleCount()));
}

QLabel* MonitorPanel::makeRow(QVBoxLayout* layout, const QString& label) {
    // 创建等宽字体的信息标签
    auto* lbl = new QLabel(label + ": --");
    lbl->setWordWrap(true);  // 允许换行（GPU 型号可能较长）
    lbl->setStyleSheet("font-size: 11px; font-family: monospace;");
    layout->addWidget(lbl);
    return lbl;
}

void MonitorPanel::applyTheme(const Theme& t) {
    setStyleSheet(QString(
        "QGroupBox {"
        "  background: %1; border: 1px solid %2;"
        "  border-radius: 6px; margin-top: 12px; padding: 16px 8px 8px 8px;"
        "  font-weight: bold; font-size: 12px; color: %3; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 10px; padding: 0 4px;"
        "  color: %4; }"
        "QLabel { color: %5; font-size: 11px; font-family: monospace; }"
    ).arg(t.mantle, t.surface0, t.subtext0, t.red, t.overlay2));
}
