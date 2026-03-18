/**
 * @file MainWindow.cpp
 * @brief 主窗口实现
 *
 * 布局结构：
 *   ┌──────────────┬──────────────────────┐
 *   │ ┌──────────┐ │                      │
 *   │ │FEModel   │ │                      │
 *   │ │Panel     │ │      GLWidget        │
 *   │ │          │ │   (OpenGL 视口)      │
 *   │ │          │ │                      │
 *   │ ├──────────┤ │                      │
 *   │ │ Monitor  │ │                      │
 *   │ │ (FPS/GPU)│ │                      │
 *   │ └──────────┘ │                      │
 *   └──────────────┴──────────────────────┘
 *    ←   220px   →  ←    自适应拉伸     →
 */

#include "MainWindow.h"
#include "GLWidget.h"
#include "MonitorPanel.h"
#include "FEModelPanel.h"

#include <glm/glm.hpp>
#include <vector>
#include <QHBoxLayout>
#include <QVBoxLayout>

MainWindow::MainWindow() {
    setWindowTitle("FEModelViewer");
    resize(1000, 700);

    auto* central = new QWidget;
    setCentralWidget(central);

    auto* mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // ── 左侧边栏 ──
    auto* sidebar = new QWidget;
    sidebar->setFixedWidth(220);
    sidebar->setStyleSheet("QWidget { background: #1e1e2e; }");

    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    feModelPanel_ = new FEModelPanel;
    monitorPanel_ = new MonitorPanel;

    sidebarLayout->addWidget(feModelPanel_, 1);
    sidebarLayout->addWidget(monitorPanel_);

    mainLayout->addWidget(sidebar);

    // ── GL 视口 ──
    glWidget_ = new GLWidget;
    mainLayout->addWidget(glWidget_, 1);

    // ── 信号/槽连接 ──

    // FEM 模型生成 → 更新网格 + 自适应缩放
    connect(feModelPanel_, &FEModelPanel::meshGenerated,
            this, [this](const Mesh& mesh, const glm::vec3& center, float size,
                         const std::vector<int>& triToElem,
                         const std::vector<int>& triToFace){
        glWidget_->setMesh(mesh);
        glWidget_->setTriangleToElementMap(triToElem);
        glWidget_->setTriangleToFaceMap(triToFace);
        glWidget_->setObjectColor(glm::vec3(0.55f, 0.75f, 0.73f));
        if (size > 0) {
            glWidget_->fitToModel(center, size);
        }
    });

    // 选中状态变化 → 通知面板更新
    connect(glWidget_, &GLWidget::selectionChanged,
            feModelPanel_, [this](int count){
        // 可扩展：在面板中显示选中信息
        Q_UNUSED(count);
    });

    // 拾取模式变化 → 同步到 GLWidget
    connect(feModelPanel_, &FEModelPanel::pickModeChanged,
            glWidget_, &GLWidget::setPickMode);

    // 监控面板绑定
    monitorPanel_->bindToWidget(glWidget_);

    // ── 启动时自动加载悬臂梁模型 ──
    feModelPanel_->loadDefaultModel();
}
