/**
 * @file MainWindow.cpp
 * @brief 主窗口实现
 *
 * 布局结构：
 *   ┌──────────────────────────────────────────────────────┐
 *   │  工具栏: [打开] [清空] | [节点][单元][部件] 拾取模式 │
 *   ├────────────┬─────────────────────────────────────────┤
 *   │ ┌────────┐ │                                         │
 *   │ │模型树  │ │                                         │
 *   │ │(Parts) │ │          GLWidget (OpenGL 视口)         │
 *   │ ├────────┤ │                                         │
 *   │ │选中信息│ │                                         │
 *   │ ├────────┤ │                                         │
 *   │ │监控    │ │                                         │
 *   │ └────────┘ │                                         │
 *   ├────────────┴─────────────────────────────────────────┤
 *   │  状态栏: 节点数 | 单元数 | 三角面数                  │
 *   └──────────────────────────────────────────────────────┘
 */

#include "MainWindow.h"
#include "GLWidget.h"
#include "MonitorPanel.h"
#include "FEModelPanel.h"
#include "PartsPanel.h"
#include "FEGroup.h"
#include "FEPickResult.h"

#include <glm/glm.hpp>
#include <vector>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QStatusBar>
#include <QLabel>
#include <algorithm>

MainWindow::MainWindow() {
    setWindowTitle("FEModelViewer");
    resize(1100, 750);

    // ── 工具栏 ──
    setupToolBar();

    // ── 左侧边栏 ──
    auto* sidebar = new QWidget;
    sidebar->setMinimumWidth(180);
    sidebar->setMaximumWidth(400);
    sidebar->setStyleSheet("QWidget { background: #1e1e2e; }");

    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    // 模型树放在最上方
    partsPanel_ = new PartsPanel;
    partsPanel_->setMinimumHeight(120);

    // 选中信息 + 模型统计
    feModelPanel_ = new FEModelPanel;

    // 监控面板
    monitorPanel_ = new MonitorPanel;

    sidebarLayout->addWidget(partsPanel_, 3);    // 模型树占较大比例
    sidebarLayout->addWidget(feModelPanel_, 2);  // 信息面板
    sidebarLayout->addWidget(monitorPanel_, 0);  // 监控面板固定高度

    // ── GL 视口 ──
    glWidget_ = new GLWidget;

    // ── QSplitter 左右布局 ──
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(sidebar);
    splitter->addWidget(glWidget_);
    // 初始比例：左240 / 右自适应
    splitter->setSizes({240, 860});
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    splitter->setStyleSheet(
        "QSplitter::handle {"
        "  background: #313244; width: 3px; }"
        "QSplitter::handle:hover {"
        "  background: #89b4fa; }"
    );

    setCentralWidget(splitter);

    // ── 状态栏 ──
    setupStatusBar();

    // ── 信号/槽连接 ──

    // FEM 模型生成 → 更新网格 + 自适应缩放
    connect(feModelPanel_, &FEModelPanel::meshGenerated,
            this, [this](const Mesh& mesh, const glm::vec3& center, float size,
                         const std::vector<int>& triToElem,
                         const std::vector<int>& vertexToNode){
        glWidget_->setMesh(mesh);
        glWidget_->setTriangleToElementMap(triToElem);
        glWidget_->setVertexToNodeMap(vertexToNode);
        glWidget_->setObjectColor(glm::vec3(0.55f, 0.75f, 0.73f));
        if (size > 0) {
            glWidget_->fitToModel(center, size);
        }
        // 更新状态栏
        statusLabel_->setText(
            QString("  节点: %1  |  单元: %2  |  三角面: %3")
            .arg(mesh.vertices.size() / 6)
            .arg(triToElem.empty() ? 0 : static_cast<int>(*std::max_element(triToElem.begin(), triToElem.end())))
            .arg(mesh.indices.size() / 3));
    });

    // 选中状态变化 → 通知面板更新
    connect(glWidget_, &GLWidget::selectionChanged,
            feModelPanel_, &FEModelPanel::updateSelectionInfo);

    // 部件列表更新 → 填充 PartsPanel，并传递 triToPart/edgeToPart 给 GLWidget
    connect(feModelPanel_, &FEModelPanel::partsChanged,
            this, [this](const QString& modelName, const std::vector<FEPart>& parts,
                         const std::vector<int>& triToPart, const std::vector<int>& edgeToPart) {
        glWidget_->setTriangleToPartMap(triToPart);
        glWidget_->setEdgeToPartMap(edgeToPart);
        partsPanel_->setParts(modelName, parts, glWidget_->partColors());
    });

    // PartsPanel 部件可见性变化 → GLWidget 更新渲染
    connect(partsPanel_, &PartsPanel::partVisibilityChanged,
            glWidget_, &GLWidget::setPartVisibility);

    // 监控面板绑定
    monitorPanel_->bindToWidget(glWidget_);
}

// ════════════════════════════════════════════════════════════
// 工具栏
// ════════════════════════════════════════════════════════════

void MainWindow::setupToolBar() {
    auto* toolbar = addToolBar("主工具栏");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(18, 18));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    toolbar->setStyleSheet(
        "QToolBar {"
        "  background: #181825; border-bottom: 1px solid #313244;"
        "  padding: 2px 4px; spacing: 2px; }"
        "QToolButton {"
        "  background: transparent; color: #cdd6f4;"
        "  border: 1px solid transparent; border-radius: 4px;"
        "  padding: 4px 10px; font-size: 12px; }"
        "QToolButton:hover {"
        "  background: #313244; border-color: #45475a; }"
        "QToolButton:pressed {"
        "  background: #45475a; }"
        "QToolButton:checked {"
        "  background: #45475a; border-color: #89b4fa; color: #89b4fa; }"
        "QToolBar::separator {"
        "  width: 1px; background: #313244; margin: 4px 6px; }"
    );

    // ── 文件操作 ──
    auto* openAction = toolbar->addAction("打开模型");
    openAction->setToolTip("打开 FEM 模型文件 (INP/BDF/FEM)");

    auto* clearAction = toolbar->addAction("清空");
    clearAction->setToolTip("清空当前模型");

    toolbar->addSeparator();

    // ── 拾取模式 ──
    pickGroup_ = new QActionGroup(this);
    pickGroup_->setExclusive(true);

    auto* nodeAction = toolbar->addAction("节点");
    nodeAction->setCheckable(true);
    nodeAction->setChecked(true);
    nodeAction->setToolTip("节点拾取模式");
    nodeAction->setData(static_cast<int>(PickMode::Node));
    pickGroup_->addAction(nodeAction);

    auto* elemAction = toolbar->addAction("单元");
    elemAction->setCheckable(true);
    elemAction->setToolTip("单元拾取模式");
    elemAction->setData(static_cast<int>(PickMode::Element));
    pickGroup_->addAction(elemAction);

    auto* partAction = toolbar->addAction("部件");
    partAction->setCheckable(true);
    partAction->setToolTip("部件拾取模式");
    partAction->setData(static_cast<int>(PickMode::Part));
    pickGroup_->addAction(partAction);

    // ── 连接 ──
    connect(openAction, &QAction::triggered, this, [this]() {
        feModelPanel_->loadModelFromFile();
    });

    connect(clearAction, &QAction::triggered, this, [this]() {
        feModelPanel_->clearModel();
        statusLabel_->setText("  就绪");
    });

    connect(pickGroup_, &QActionGroup::triggered, this, [this](QAction* action) {
        PickMode mode = static_cast<PickMode>(action->data().toInt());
        glWidget_->setPickMode(mode);
    });
}

// ════════════════════════════════════════════════════════════
// 状态栏
// ════════════════════════════════════════════════════════════

void MainWindow::setupStatusBar() {
    auto* sb = statusBar();
    sb->setStyleSheet(
        "QStatusBar {"
        "  background: #181825; border-top: 1px solid #313244;"
        "  color: #9399b2; font-size: 11px; font-family: monospace; }"
        "QStatusBar::item { border: none; }"
    );

    statusLabel_ = new QLabel("  就绪");
    sb->addWidget(statusLabel_);
}
