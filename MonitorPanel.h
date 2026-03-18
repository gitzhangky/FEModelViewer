/**
 * @file MonitorPanel.h
 * @brief 实时监控面板声明
 *
 * 显示实时性能数据和硬件信息：
 *   - FPS 帧率、每帧耗时
 *   - 当前网格的顶点数、三角面数
 *   - GPU 型号、厂商、OpenGL 版本、GLSL 版本
 *
 * 通过 bindToWidget() 绑定到 GLWidget 后，使用 QTimer 定时刷新数据。
 */

#pragma once

#include <QGroupBox>

class QLabel;
class QVBoxLayout;
class GLWidget;

class MonitorPanel : public QGroupBox {
    Q_OBJECT

public:
    explicit MonitorPanel(QWidget* parent = nullptr);

    /**
     * @brief 绑定到 GLWidget 并启动定时刷新
     * @param gl 要监控的 OpenGL 渲染组件
     *
     * 连接 GLWidget::glInitialized 信号以获取硬件信息，
     * 并启动 200ms 间隔的定时器刷新 FPS 等动态数据。
     */
    void bindToWidget(GLWidget* gl);

private slots:
    /** @brief 定时刷新性能统计数据 */
    void refresh();

private:
    /**
     * @brief 创建一行信息标签
     * @param layout 父布局
     * @param label 标签前缀文字（如 "FPS"）
     * @return 创建的 QLabel 指针，后续用于更新显示文本
     */
    static QLabel* makeRow(QVBoxLayout* layout, const QString& label);

    GLWidget* gl_ = nullptr;  // 绑定的 GLWidget（用于查询数据）

    // ── 各项数据的显示标签 ──
    QLabel* fpsLabel_ = nullptr;         // FPS 帧率
    QLabel* frameTimeLabel_ = nullptr;   // 每帧耗时
    QLabel* vertexLabel_ = nullptr;      // 顶点数
    QLabel* triangleLabel_ = nullptr;    // 三角面数
    QLabel* rendererLabel_ = nullptr;    // GPU 型号
    QLabel* vendorLabel_ = nullptr;      // GPU 厂商
    QLabel* glVersionLabel_ = nullptr;   // OpenGL 版本
    QLabel* glslLabel_ = nullptr;        // GLSL 版本
};
