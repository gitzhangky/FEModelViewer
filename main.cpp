/**
 * @file main.cpp
 * @brief 程序入口
 *
 * 职责：
 *   1. 配置 OpenGL 上下文格式（版本、Profile、深度缓冲、多重采样）
 *   2. 创建 QApplication 和主窗口
 *   3. 进入 Qt 事件循环
 */

#include <QApplication>
#include <QSurfaceFormat>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
    // ── 配置 OpenGL 上下文 ──
    QSurfaceFormat fmt;
    fmt.setVersion(4, 1);                      // OpenGL 4.1（macOS 支持的最高核心版本）
    fmt.setProfile(QSurfaceFormat::CoreProfile); // Core Profile（不包含已废弃的固定管线功能）
    fmt.setDepthBufferSize(32);                // 32 位深度缓冲（提高薄壳模型深度精度）
    fmt.setSamples(8);                         // 8x MSAA 多重采样抗锯齿
    QSurfaceFormat::setDefaultFormat(fmt);     // 设为全局默认格式

    // ── 启动应用 ──
    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    // 进入 Qt 事件循环（阻塞直到窗口关闭）
    return app.exec();
}
