/**
 * @file MainWindow.cpp
 * @brief 主窗口实现
 *
 * 布局结构：
 *   ┌──────────────────────────────────────────────────────┐
 *   │  工具栏: [清空] | [节点][单元][部件] 拾取模式        │
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
 *   │  文件导入: 模型文件 [...] [路径] | 结果文件 [...] [路径] [应用] │
 *   ├──────────────────────────────────────────────────────┤
 *   │  状态栏                                              │
 *   └──────────────────────────────────────────────────────┘
 */

#include "MainWindow.h"
#include "GLWidget.h"
#include "MonitorPanel.h"
#include "FEModelPanel.h"
#include "PartsPanel.h"
#include "ResultPanel.h"
#include "FEGroup.h"
#include "FEPickResult.h"
#include "FEMeshConverter.h"
#include "FEResultData.h"

#include <glm/glm.hpp>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QStatusBar>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QSettings>
#include <QDir>
#include <QCloseEvent>

MainWindow::MainWindow() {
    setWindowTitle("FEModelViewer");
    resize(1100, 800);

    // ── 工具栏 ──
    setupToolBar();

    // ── 左侧边栏 ──
    auto* sidebar = new QWidget;
    sidebar->setMinimumWidth(200);
    sidebar->setStyleSheet("QWidget { background: #1e1e2e; }");

    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    partsPanel_ = new PartsPanel;
    partsPanel_->setMinimumHeight(120);

    feModelPanel_ = new FEModelPanel;
    monitorPanel_ = new MonitorPanel;

    sidebarLayout->addWidget(partsPanel_, 3);
    sidebarLayout->addWidget(feModelPanel_, 2);
    sidebarLayout->addWidget(monitorPanel_, 0);

    // ── GL 视口 ──
    glWidget_ = new GLWidget;

    // ── 右侧结果面板 ──
    resultPanel_ = new ResultPanel;

    auto* rightSidebar = new QWidget;
    rightSidebar->setMinimumWidth(180);
    rightSidebar->setMaximumWidth(280);
    rightSidebar->setStyleSheet("QWidget { background: #1e1e2e; }");
    auto* rightLayout = new QVBoxLayout(rightSidebar);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(resultPanel_);

    // ── 水平 Splitter: 左侧边栏 | GL视口 | 右侧边栏 ──
    auto* hSplitter = new QSplitter(Qt::Horizontal);
    hSplitter->setChildrenCollapsible(false);
    hSplitter->addWidget(sidebar);
    hSplitter->addWidget(glWidget_);
    hSplitter->addWidget(rightSidebar);
    hSplitter->setSizes({240, 640, 220});
    hSplitter->setStretchFactor(0, 0);
    hSplitter->setStretchFactor(1, 1);
    hSplitter->setStretchFactor(2, 0);

    hSplitter->setStyleSheet(
        "QSplitter::handle {"
        "  background: #313244; width: 3px; }"
        "QSplitter::handle:hover {"
        "  background: #89b4fa; }"
    );

    // ── 底部文件导入面板 ──
    auto* filePanel = createFilePanel();

    // ── 垂直 Splitter: 上方主区域 + 下方文件面板 ──
    auto* vSplitter = new QSplitter(Qt::Vertical);
    vSplitter->setChildrenCollapsible(false);
    vSplitter->addWidget(hSplitter);
    vSplitter->addWidget(filePanel);
    // 文件面板固定高度，主区域自适应
    vSplitter->setStretchFactor(0, 1);
    vSplitter->setStretchFactor(1, 0);

    vSplitter->setStyleSheet(
        "QSplitter::handle {"
        "  background: #313244; height: 3px; }"
        "QSplitter::handle:hover {"
        "  background: #89b4fa; }"
    );

    setCentralWidget(vSplitter);

    // ── 状态栏 ──
    setupStatusBar();

    // ── 信号/槽连接 ──

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
    });

    connect(glWidget_, &GLWidget::selectionChanged,
            feModelPanel_, &FEModelPanel::updateSelectionInfo);

    connect(feModelPanel_, &FEModelPanel::partsChanged,
            this, [this](const QString& modelName, const std::vector<FEPart>& parts,
                         const std::vector<int>& triToPart, const std::vector<int>& edgeToPart) {
        glWidget_->setTriangleToPartMap(triToPart);
        glWidget_->setEdgeToPartMap(edgeToPart);
        partsPanel_->setParts(modelName, parts, glWidget_->partColors());
    });

    // 加载进度 → 状态栏进度条
    connect(feModelPanel_, &FEModelPanel::loadProgress,
            this, [this](int percent, const QString& text) {
        if (percent > 0 && !text.isEmpty()) {
            statusProgress_->setVisible(true);
            statusProgress_->setValue(percent);
            progressText_->setVisible(true);
            progressText_->setText(text);
            statusLabel_->setVisible(false);
        } else {
            statusProgress_->setVisible(false);
            progressText_->setVisible(false);
            statusLabel_->setVisible(true);
        }
    });

    // 加载完成 → 更新状态栏文字
    connect(feModelPanel_, &FEModelPanel::loadFinished,
            this, [this](bool success, const QString& message) {
        statusProgress_->setVisible(false);
        progressText_->setVisible(false);
        statusLabel_->setVisible(true);
        statusLabel_->setText("  " + message);
        statusLabel_->setStyleSheet(success
            ? "color: #a6e3a1; font-weight: bold;"
            : "color: #f38ba8; font-weight: bold;");
    });

    connect(partsPanel_, &PartsPanel::partVisibilityChanged,
            glWidget_, &GLWidget::setPartVisibility);

    connect(partsPanel_, &PartsPanel::partSelectionChanged,
            glWidget_, &GLWidget::highlightParts);

    // 部件拾取 → 同步模型树选中状态
    connect(glWidget_, &GLWidget::partsPicked,
            partsPanel_, &PartsPanel::selectParts);

    monitorPanel_->bindToWidget(glWidget_);

    // ── 结果面板连接 ──

    // resultsLoaded → 填充右侧面板
    connect(feModelPanel_, &FEModelPanel::resultsLoaded,
            resultPanel_, &ResultPanel::setResults);

    // 应用云图
    connect(resultPanel_, &ResultPanel::applyResult,
            this, [this](const FEScalarField& field, const QString& title) {
        const FEModel& model = feModelPanel_->currentModel();
        if (model.nodes.empty()) return;

        const FERenderData& rd = feModelPanel_->currentRenderData();
        int vertCount = static_cast<int>(rd.mesh.vertices.size() / 6);
        if (vertCount == 0) return;

        // 获取色谱范围
        float minVal = 0, maxVal = 1;
        field.computeRange(minVal, maxVal);

        const int numBands = 9;

        // 构建 per-vertex 标量值数组（传给 GPU，由片段着色器做量化 + 颜色映射）
        std::vector<float> scalars(vertCount, 0.0f);

        // ── 建立节点值查找表（处理 NID 不匹配的情况） ──
        int directHits = 0;
        for (const auto& [nid, node] : model.nodes) {
            if (field.values.count(nid) > 0) directHits++;
        }
        bool useDirect = (directHits > static_cast<int>(model.nodes.size()) / 2);

        std::unordered_map<int, float> nodeValueMap;
        if (useDirect) {
            for (const auto& [nid, val] : field.values)
                nodeValueMap[nid] = val;
        } else {
            std::vector<int> modelNids, resultNids;
            modelNids.reserve(model.nodes.size());
            for (const auto& [nid, node] : model.nodes) modelNids.push_back(nid);
            std::sort(modelNids.begin(), modelNids.end());
            resultNids.reserve(field.values.size());
            for (const auto& [nid, val] : field.values) resultNids.push_back(nid);
            std::sort(resultNids.begin(), resultNids.end());
            int mc = std::min(static_cast<int>(modelNids.size()), static_cast<int>(resultNids.size()));
            for (int k = 0; k < mc; ++k) {
                auto it = field.values.find(resultNids[k]);
                if (it != field.values.end())
                    nodeValueMap[modelNids[k]] = it->second;
            }
        }

        if (field.location == FieldLocation::Element) {
            // 单元场：遍历三角形，把每个三角形对应的单元值写入其三个顶点
            int triCount = static_cast<int>(rd.triangleToElement.size());
            for (int t = 0; t < triCount; ++t) {
                int elemId = rd.triangleToElement[t];
                auto it = field.values.find(elemId);
                if (it == field.values.end()) continue;
                for (int k = 0; k < 3; ++k) {
                    unsigned int vi = rd.mesh.indices[t * 3 + k];
                    if (static_cast<int>(vi) < vertCount) {
                        scalars[vi] = it->second;
                    }
                }
            }
        } else {
            // 节点场：per-vertex 标量值
            for (int i = 0; i < vertCount; ++i) {
                int nodeId = (i < static_cast<int>(rd.vertexToNode.size())) ? rd.vertexToNode[i] : -1;
                if (nodeId < 0) continue;
                auto it = nodeValueMap.find(nodeId);
                if (it == nodeValueMap.end()) continue;
                scalars[i] = it->second;
            }
        }

        // 上传标量值到 GPU，由片段着色器做离散量化 + Jet colormap 映射
        glWidget_->setVertexScalars(scalars, minVal, maxVal, numBands);

        // 更新色标
        glWidget_->setColorBarVisible(true);
        glWidget_->setColorBarRange(minVal, maxVal);
        glWidget_->setColorBarTitle(title);
    });

    // 清除云图 → 恢复部件颜色
    connect(resultPanel_, &ResultPanel::clearResult,
            this, [this]() {
        glWidget_->setUseVertexColor(false);
        glWidget_->setColorBarVisible(false);
        glWidget_->update();
    });
}

// ════════════════════════════════════════════════════════════
// 底部文件导入面板
// ════════════════════════════════════════════════════════════

QWidget* MainWindow::createFilePanel() {
    auto* panel = new QWidget;
    panel->setMinimumHeight(70);
    panel->setMaximumHeight(120);

    auto* mainLayout = new QVBoxLayout(panel);
    mainLayout->setContentsMargins(12, 8, 12, 8);
    mainLayout->setSpacing(6);

    // ── 模型文件行 ──
    auto* modelRow = new QHBoxLayout;
    modelRow->setSpacing(6);

    auto* modelLabel = new QLabel("模型文件");
    modelLabel->setFixedWidth(60);
    modelRow->addWidget(modelLabel);

    modelPathEdit_ = new QLineEdit;
    modelPathEdit_->setPlaceholderText("选择 INP / BDF / FEM / OP2 文件...");
    modelPathEdit_->setReadOnly(true);
    modelRow->addWidget(modelPathEdit_);

    auto* modelBrowseBtn = new QPushButton("浏览...");
    modelBrowseBtn->setFixedWidth(70);
    modelRow->addWidget(modelBrowseBtn);

    mainLayout->addLayout(modelRow);

    // ── 结果文件行 ──
    auto* resultRow = new QHBoxLayout;
    resultRow->setSpacing(6);

    auto* resultLabel = new QLabel("结果文件");
    resultLabel->setFixedWidth(60);
    resultRow->addWidget(resultLabel);

    resultPathEdit_ = new QLineEdit;
    resultPathEdit_->setPlaceholderText("选择 ODB / OP2 / H3D 结果文件...");
    resultPathEdit_->setReadOnly(true);
    resultRow->addWidget(resultPathEdit_);

    auto* resultBrowseBtn = new QPushButton("浏览...");
    resultBrowseBtn->setFixedWidth(70);
    resultRow->addWidget(resultBrowseBtn);

    // 应用按钮放在结果行右侧
    auto* applyBtn = new QPushButton("应用");
    applyBtn->setFixedWidth(70);
    resultRow->addWidget(applyBtn);

    mainLayout->addLayout(resultRow);

    // ── 样式 ──
    panel->setStyleSheet(
        "QWidget { background: #11111b; color: #cdd6f4; }"

        "QLabel {"
        "  font-size: 12px; color: #89b4fa; font-weight: bold; }"

        "QLineEdit {"
        "  background: #1e1e2e; border: 1px solid #45475a; border-radius: 4px;"
        "  padding: 4px 8px; font-size: 12px; color: #cdd6f4;"
        "  selection-background-color: #45475a; }"
        "QLineEdit:focus {"
        "  border-color: #89b4fa; }"
        "QLineEdit[readOnly=\"true\"] {"
        "  color: #9399b2; }"

        "QPushButton {"
        "  background: #313244; color: #cdd6f4; border: 1px solid #45475a;"
        "  border-radius: 4px; padding: 4px 8px; font-size: 12px; }"
        "QPushButton:hover {"
        "  background: #45475a; border-color: #89b4fa; }"
        "QPushButton:pressed {"
        "  background: #585b70; }"
    );

    // 给应用按钮加个醒目样式
    applyBtn->setStyleSheet(
        "QPushButton {"
        "  background: #89b4fa; color: #1e1e2e; border: none;"
        "  border-radius: 4px; padding: 4px 8px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover {"
        "  background: #b4d0fb; }"
        "QPushButton:pressed {"
        "  background: #74a8f7; }"
    );

    // ── 连接 ──
    connect(modelBrowseBtn, &QPushButton::clicked, this, &MainWindow::browseModelFile);
    connect(resultBrowseBtn, &QPushButton::clicked, this, &MainWindow::browseResultFile);
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::applyFiles);

    // 恢复上次的文件路径
    {
        QSettings settings("FEModelViewer", "FEModelViewer");
        modelPathEdit_->setText(settings.value("lastModelPath", QString()).toString());
        resultPathEdit_->setText(settings.value("lastResultPath", QString()).toString());
    }

    return panel;
}

void MainWindow::browseModelFile() {
    QSettings settings("FEModelViewer", "FEModelViewer");
    QString lastDir = settings.value("lastOpenDir", QString()).toString();
    if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
        lastDir = QDir::homePath() + "/Desktop";
        if (!QDir(lastDir).exists()) lastDir = QDir::homePath();
    }

    QFileDialog dialog(this, "选择模型文件", lastDir,
        "所有支持格式 (*.inp *.bdf *.fem *.op2);;"
        "ABAQUS Input (*.inp);;"
        "Nastran BDF (*.bdf *.fem);;"
        "Nastran OP2 (*.op2);;"
        "所有文件 (*)");
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    if (dialog.exec() != QDialog::Accepted) return;
    QString path = dialog.selectedFiles().first();

    if (!path.isEmpty()) {
        modelPathEdit_->setText(path);
        settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());
    }
}

void MainWindow::browseResultFile() {
    QSettings settings("FEModelViewer", "FEModelViewer");
    QString lastDir = settings.value("lastOpenDir", QString()).toString();
    if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
        lastDir = QDir::homePath() + "/Desktop";
        if (!QDir(lastDir).exists()) lastDir = QDir::homePath();
    }

    QFileDialog dialog(this, "选择结果文件", lastDir,
        "结果文件 (*.odb *.op2 *.h3d *.rst *.xdb);;"
        "ABAQUS ODB (*.odb);;"
        "Nastran OP2 (*.op2);;"
        "HyperWorks H3D (*.h3d);;"
        "所有文件 (*)");
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    if (dialog.exec() != QDialog::Accepted) return;
    QString path = dialog.selectedFiles().first();

    if (!path.isEmpty()) {
        resultPathEdit_->setText(path);
        settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());
    }
}

void MainWindow::applyFiles() {
    QString modelPath = modelPathEdit_->text().trimmed();
    QString resultPath = resultPathEdit_->text().trimmed();

    if (modelPath.isEmpty() && resultPath.isEmpty()) {
        QMessageBox::information(this, "提示", "请先选择模型文件或结果文件。");
        return;
    }

    // 加载模型文件
    if (!modelPath.isEmpty()) {
        feModelPanel_->loadModelFromPath(modelPath);
    }

    // 加载结果文件
    if (!resultPath.isEmpty()) {
        QString suffix = QFileInfo(resultPath).suffix().toLower();
        if (suffix == "op2") {
            FEResultData results;
            bool ok = feModelPanel_->parseNastranOp2Results(resultPath, results);
            if (ok) {
                emit feModelPanel_->resultsLoaded(results);
                statusLabel_->setText("  结果加载完成");
                statusLabel_->setStyleSheet("color: #a6e3a1; font-weight: bold;");
            } else {
                QMessageBox::warning(this, "结果文件",
                    QString("未能从 OP2 文件中解析到结果数据。\n\n文件: %1").arg(resultPath));
            }
        } else {
            QMessageBox::information(this, "结果文件",
                QString("暂不支持该格式的结果文件。\n\n文件: %1").arg(resultPath));
        }
    }
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
    connect(clearAction, &QAction::triggered, this, [this]() {
        feModelPanel_->clearModel();
        statusProgress_->setVisible(false);
        progressText_->setVisible(false);
        statusLabel_->setVisible(true);
        statusLabel_->setText("  就绪");
        statusLabel_->setStyleSheet("color: #a6e3a1; font-weight: bold;");
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
    sb->setFixedHeight(26);
    sb->setStyleSheet(
        "QStatusBar {"
        "  background: #11111b; border-top: 1px solid #313244;"
        "  font-size: 11px; font-family: monospace; }"
        "QStatusBar::item { border: none; }"
    );

    // 左侧：状态文字
    statusLabel_ = new QLabel("  就绪");
    statusLabel_->setStyleSheet("color: #a6e3a1; font-weight: bold;");
    sb->addWidget(statusLabel_, 1);

    // 右侧：进度文字 + 进度条（默认隐藏）
    progressText_ = new QLabel;
    progressText_->setStyleSheet("color: #89b4fa; font-size: 11px; padding-right: 6px;");
    progressText_->setVisible(false);
    sb->addPermanentWidget(progressText_);

    statusProgress_ = new QProgressBar;
    statusProgress_->setFixedWidth(200);
    statusProgress_->setFixedHeight(14);
    statusProgress_->setRange(0, 100);
    statusProgress_->setTextVisible(true);
    statusProgress_->setFormat("%p%");
    statusProgress_->setVisible(false);
    statusProgress_->setStyleSheet(
        "QProgressBar {"
        "  border: 1px solid #45475a; border-radius: 6px;"
        "  background: #1e1e2e; text-align: center;"
        "  color: #cdd6f4; font-size: 10px; }"
        "QProgressBar::chunk {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #89b4fa, stop:1 #94e2d5);"
        "  border-radius: 5px; }"
    );
    sb->addPermanentWidget(statusProgress_);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings settings("FEModelViewer", "FEModelViewer");
    settings.setValue("lastModelPath", modelPathEdit_->text());
    settings.setValue("lastResultPath", resultPathEdit_->text());
    QMainWindow::closeEvent(event);
}
