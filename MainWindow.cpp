/**
 * @file MainWindow.cpp
 * @brief 主窗口实现
 *
 * HyperView 风格布局：
 *   ┌─────────┬──────────────────────────────────┬─────────────┐
 *   │  工具栏: [清空] | [节点][单元][部件] | [主题]            │
 *   ├─────────┼──────────────────────────────────┼─────────────┤
 *   │         │                                  │             │
 *   │  部件   │        GLWidget (中央视口)        │  模型信息   │
 *   │ (左停靠)│                                  │  (右停靠)   │
 *   │         ├──────────────────────────────────┤             │
 *   │         │  [文件导入][结果显示][监控]       │             │
 *   │         │  当前面板内容                     │             │
 *   ├─────────┴──────────────────────────────────┴─────────────┤
 *   │  状态栏                                                  │
 *   └─────────────────────────────────────────────────────────┘
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
#include "FEResultMapper.h"
#include "FEAnimationController.h"
#include "FEDeformation.h"
#include "FEPostFilter.h"
#include "FEIsoSurface.h"

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

    // ── GL 视口 ──
    glWidget_ = new GLWidget;

    // ── 创建面板 ──
    partsPanel_ = new PartsPanel;
    feModelPanel_ = new FEModelPanel;
    monitorPanel_ = new MonitorPanel;
    resultPanel_ = new ResultPanel;
    filePanel_ = createFilePanel();

    // ── 底部标签页（文件导入 / 结果显示 / 监控）──
    bottomTabs_ = new QTabWidget;
    bottomTabs_->setTabPosition(QTabWidget::North);
    bottomTabs_->addTab(filePanel_,    "文件导入");
    bottomTabs_->addTab(resultPanel_,  "结果显示");
    bottomTabs_->addTab(monitorPanel_, "监控");

    // ── 中央区域：垂直 Splitter（GL 视口 + 底部标签页）──
    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(glWidget_);
    splitter->addWidget(bottomTabs_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({600, 180});
    splitter->setChildrenCollapsible(false);
    setCentralWidget(splitter);

    // ── 左侧停靠：部件面板 ──
    partsDock_ = new QDockWidget("部件", this);
    partsDock_->setWidget(partsPanel_);
    partsDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::LeftDockWidgetArea, partsDock_);

    // ── 右侧停靠：模型信息面板 ──
    modelInfoDock_ = new QDockWidget("模型信息", this);
    modelInfoDock_->setWidget(feModelPanel_);
    modelInfoDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, modelInfoDock_);

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
        updateFilterPlaneBounds();
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
            ? QString("color: %1; font-weight: bold;").arg(currentTheme_.green)
            : QString("color: %1; font-weight: bold;").arg(currentTheme_.red));
    });

    connect(partsPanel_, &PartsPanel::partVisibilityChanged,
            glWidget_, &GLWidget::setPartVisibility);

    connect(partsPanel_, &PartsPanel::partSelectionChanged,
            glWidget_, &GLWidget::highlightParts);

    // 部件拾取 → 同步模型树选中状态
    connect(glWidget_, &GLWidget::partsPicked,
            partsPanel_, &PartsPanel::selectParts);

    // ID 标签显隐 → GLWidget
    connect(feModelPanel_, &FEModelPanel::labelVisibilityChanged,
            glWidget_, &GLWidget::setShowLabels);

    // ID 搜索 → GLWidget 选中高亮
    connect(feModelPanel_, &FEModelPanel::searchRequested,
            glWidget_, &GLWidget::selectByIds);

    monitorPanel_->bindToWidget(glWidget_);

    // ── 结果面板连接 ──

    // resultsLoaded → 填充右侧面板
    connect(feModelPanel_, &FEModelPanel::resultsLoaded,
            resultPanel_, &ResultPanel::setResults);

    // 应用云图
    connect(resultPanel_, &ResultPanel::applyResult,
            this, [this](const FEScalarField& field, const QString& title) {
        applyContour(field, title);
        feModelPanel_->setActiveScalarField(field);
    });

    // 清除云图 → 恢复部件颜色
    connect(resultPanel_, &ResultPanel::clearResult,
            this, [this]() {
        contourActive_ = false;
        activeContourField_ = FEScalarField{};
        activeContourTitle_.clear();
        glWidget_->setUseVertexColor(false);
        glWidget_->setColorBarVisible(false);
        glWidget_->update();
        feModelPanel_->clearActiveScalarField();
    });

    // ── 动画控制器 ──
    animController_ = new FEAnimationController(this);

    connect(resultPanel_, &ResultPanel::animationPlay,
            animController_, &FEAnimationController::play);
    connect(resultPanel_, &ResultPanel::animationPause,
            animController_, &FEAnimationController::pause);
    connect(resultPanel_, &ResultPanel::animationStop,
            animController_, &FEAnimationController::stop);

    // 动画切帧：切帧 → 变形（如果开启）→ 云图
    connect(animController_, &FEAnimationController::frameChanged,
            this, [this](int frameIndex) {
        resultPanel_->selectFrame(frameIndex);

        if (deformActive_)
            applyDeformation(deformScale_, deformOverlay_);

        FEScalarField field;
        QString title;
        if (resultPanel_->currentScalarField(field, title))
            applyContour(field, title);
    });

    // 结果加载后设置帧数
    connect(feModelPanel_, &FEModelPanel::resultsLoaded,
            this, [this](const FEResultData&) {
        animController_->setFrameCount(resultPanel_->frameCount());
    });

    // ── 变形显示 ──
    connect(resultPanel_, &ResultPanel::deformationRequested,
            this, [this](float scale, bool overlayUndeformed) {
        applyDeformation(scale, overlayUndeformed);
    });

    connect(resultPanel_, &ResultPanel::deformationCleared,
            this, [this]() {
        clearDeformation();
    });

    connect(resultPanel_, &ResultPanel::autoScaleRequested,
            this, [this]() {
        const FEModel& model = feModelPanel_->currentModel();
        if (model.nodes.empty()) return;

        FEVectorField disp = resultPanel_->currentDisplacement();
        if (disp.values.empty()) return;

        float scale = FEDeformation::autoScale(model, disp);
        resultPanel_->setDeformScale(scale);
    });

    // ── 过滤 ──
    connect(resultPanel_, &ResultPanel::thresholdRequested,
            this, &MainWindow::applyThreshold);
    connect(resultPanel_, &ResultPanel::clipPlaneRequested,
            this, &MainWindow::applyClipPlane);
    connect(resultPanel_, &ResultPanel::slicePlaneRequested,
            this, &MainWindow::applySlicePlane);
    connect(resultPanel_, &ResultPanel::isoSurfaceRequested,
            this, &MainWindow::applyIsoSurface);
    connect(resultPanel_, &ResultPanel::filterCleared,
            this, &MainWindow::clearFilters);
    connect(resultPanel_, &ResultPanel::planePreviewChanged,
            this, [this](const glm::vec3& origin, const glm::vec3& normal) {
        const FEModel& model = activeModel();
        if (model.nodes.empty()) {
            glWidget_->clearClipPlanePreview();
            return;
        }
        glm::vec3 bbMin, bbMax;
        model.computeBoundingBox(bbMin, bbMax);
        glWidget_->setClipPlanePreview(bbMin, bbMax, origin, normal);
    });
    connect(resultPanel_, &ResultPanel::planePreviewCleared,
            glWidget_, &GLWidget::clearClipPlanePreview);

    // ── 初始主题（默认深色）──
    currentTheme_ = Theme::dark();
    applyTheme(currentTheme_);
}

// ════════════════════════════════════════════════════════════
// 底部文件导入面板
// ════════════════════════════════════════════════════════════

QWidget* MainWindow::createFilePanel() {
    auto* panel = new QWidget;

    auto* mainLayout = new QVBoxLayout(panel);
    mainLayout->setContentsMargins(14, 10, 14, 10);
    mainLayout->setSpacing(8);

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

    // 样式在 applyTheme() 中统一设置
    filePanelApplyBtn_ = applyBtn;

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

        // OP2 文件同时包含几何和结果，自动填充结果路径
        if (QFileInfo(path).suffix().toLower() == "op2") {
            resultPathEdit_->setText(path);
        }
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
        "结果文件 (*.odb *.op2 *.h3d *.rst *.xdb *.unv);;"
        "Nastran OP2 (*.op2);;"
        "Universal UNV (*.unv);;"
        "ABAQUS ODB (*.odb);;"
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
        FEResultData results;
        bool ok = false;

        if (suffix == "op2") {
            ok = feModelPanel_->parseNastranOp2Results(resultPath, results);
        } else if (suffix == "unv") {
            ok = feModelPanel_->parseUnvResults(resultPath, results);
        }

        if (ok) {
            emit feModelPanel_->resultsLoaded(results);
            statusLabel_->setText("  结果加载完成");
            statusLabel_->setStyleSheet(
                QString("color: %1; font-weight: bold;").arg(currentTheme_.green));
        } else if (suffix == "op2" || suffix == "unv") {
            QMessageBox::warning(this, "结果文件",
                QString("未能从结果文件中解析到数据。\n\n文件: %1").arg(resultPath));
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
    toolbar_ = addToolBar("主工具栏");
    toolbar_->setMovable(false);
    toolbar_->setIconSize(QSize(18, 18));
    toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto* clearAction = toolbar_->addAction("清空");
    clearAction->setToolTip("清空当前模型");

    toolbar_->addSeparator();

    // ── 拾取模式 ──
    pickGroup_ = new QActionGroup(this);
    pickGroup_->setExclusive(true);

    auto* nodeAction = toolbar_->addAction("节点");
    nodeAction->setCheckable(true);
    nodeAction->setChecked(true);
    nodeAction->setToolTip("节点拾取模式");
    nodeAction->setData(static_cast<int>(PickMode::Node));
    pickGroup_->addAction(nodeAction);

    auto* elemAction = toolbar_->addAction("单元");
    elemAction->setCheckable(true);
    elemAction->setToolTip("单元拾取模式");
    elemAction->setData(static_cast<int>(PickMode::Element));
    pickGroup_->addAction(elemAction);

    auto* partAction = toolbar_->addAction("部件");
    partAction->setCheckable(true);
    partAction->setToolTip("部件拾取模式");
    partAction->setData(static_cast<int>(PickMode::Part));
    pickGroup_->addAction(partAction);

    toolbar_->addSeparator();

    // ── 主题切换（下拉菜单） ──
    themeMenu_ = new QMenu(this);
    for (int i = 0; i < Theme::count(); ++i) {
        Theme th = Theme::byIndex(i);
        QAction* act = themeMenu_->addAction(th.name);
        connect(act, &QAction::triggered, this, [this, i]() {
            themeIndex_ = i;
            currentTheme_ = Theme::byIndex(i);
            applyTheme(currentTheme_);
        });
    }
    themeAction_ = toolbar_->addAction("主题");
    themeAction_->setToolTip("切换主题风格");
    themeAction_->setMenu(themeMenu_);
    // 点击按钮时弹出菜单
    connect(themeAction_, &QAction::triggered, this, [this]() {
        if (auto* btn = toolbar_->widgetForAction(themeAction_))
            themeMenu_->popup(btn->mapToGlobal(btn->rect().bottomLeft()));
    });

    // ── 连接 ──
    connect(clearAction, &QAction::triggered, this, [this]() {
        feModelPanel_->clearModel();
        statusProgress_->setVisible(false);
        progressText_->setVisible(false);
        statusLabel_->setVisible(true);
        statusLabel_->setText("  就绪");
        statusLabel_->setStyleSheet(
            QString("color: %1; font-weight: bold;").arg(currentTheme_.green));
    });

    connect(pickGroup_, &QActionGroup::triggered, this, [this](QAction* action) {
        int mode = action->data().toInt();
        glWidget_->setPickMode(static_cast<PickMode>(mode));
    });
}

// ════════════════════════════════════════════════════════════
// 状态栏
// ════════════════════════════════════════════════════════════

void MainWindow::setupStatusBar() {
    auto* sb = statusBar();
    sb->setFixedHeight(30);
    // 左侧：状态文字
    statusLabel_ = new QLabel("  就绪");
    sb->addWidget(statusLabel_, 1);

    // 右侧：进度文字 + 进度条（默认隐藏）
    progressText_ = new QLabel;
    progressText_->setVisible(false);
    sb->addPermanentWidget(progressText_);

    statusProgress_ = new QProgressBar;
    statusProgress_->setFixedWidth(200);
    statusProgress_->setFixedHeight(14);
    statusProgress_->setRange(0, 100);
    statusProgress_->setTextVisible(true);
    statusProgress_->setFormat("%p%");
    statusProgress_->setVisible(false);
    sb->addPermanentWidget(statusProgress_);
}

void MainWindow::applyTheme(const Theme& t) {
    // 主题按钮文字更新
    themeAction_->setText(t.name);

    // 主窗口背景
    setStyleSheet(QString(
        "QMainWindow { background: %1; }"
    ).arg(t.mantle));

    // 底部标签页
    bottomTabs_->setStyleSheet(QString(
        "QTabWidget { background: %1; }"
        "QTabWidget::pane {"
        "  background: %1; border: none;"
        "  border-top: 1px solid %2; }"
        "QTabBar {"
        "  background: %3; }"
        "QTabBar::tab {"
        "  background: %3; color: %4;"
        "  border-top-left-radius: 4px; border-top-right-radius: 4px;"
        "  padding: 6px 16px; font-size: 12px; margin-right: 1px; }"
        "QTabBar::tab:selected {"
        "  background: %1; color: %5; font-weight: bold; }"
        "QTabBar::tab:hover:!selected {"
        "  background: %2; }"
    ).arg(t.base, t.surface0, t.mantle, t.subtext0, t.blue));

    // 侧边栏停靠面板
    QString dockStyle = QString(
        "QDockWidget {"
        "  color: %1; font-size: 12px; font-weight: bold;"
        "  titlebar-close-icon: none; }"
        "QDockWidget::title {"
        "  background: %2; border-bottom: 1px solid %3;"
        "  padding: 6px 10px; }"
    ).arg(t.blue, t.mantle, t.surface0);
    partsDock_->setStyleSheet(dockStyle);
    modelInfoDock_->setStyleSheet(dockStyle);

    // Splitter 手柄
    if (auto* sp = findChild<QSplitter*>()) {
        sp->setStyleSheet(QString(
            "QSplitter::handle {"
            "  background: %1; height: 4px; }"
            "QSplitter::handle:hover {"
            "  background: %2; }"
        ).arg(t.surface0, t.blue));
    }

    // 工具栏 — 更宽松的间距，checked 状态用底部强调线
    toolbar_->setStyleSheet(QString(
        "QToolBar {"
        "  background: %1; border-bottom: 1px solid %2;"
        "  padding: 4px 8px; spacing: 4px; }"
        "QToolButton {"
        "  background: transparent; color: %3;"
        "  border: 1px solid transparent; border-radius: 5px;"
        "  padding: 5px 14px; font-size: 12px; margin: 1px 0; }"
        "QToolButton:hover {"
        "  background: %2; }"
        "QToolButton:pressed {"
        "  background: %4; }"
        "QToolButton:checked {"
        "  background: %4; color: %5;"
        "  border-bottom: 2px solid %5; border-radius: 5px; }"
        "QToolBar::separator {"
        "  width: 1px; background: %2; margin: 6px 8px; }"
    ).arg(t.mantle, t.surface0, t.text, t.surface1, t.blue));

    // 主题下拉菜单
    themeMenu_->setStyleSheet(QString(
        "QMenu {"
        "  background: %1; border: 1px solid %2; border-radius: 6px;"
        "  padding: 6px 0; }"
        "QMenu::item {"
        "  color: %3; padding: 8px 24px; font-size: 12px; border-radius: 4px;"
        "  margin: 2px 6px; }"
        "QMenu::item:selected {"
        "  background: %4; color: %5; }"
        "QMenu::indicator {"
        "  width: 14px; height: 14px; margin-left: 8px; }"
    ).arg(t.mantle, t.surface0, t.text, t.surface1, t.blue));

    // 标记当前主题
    auto actions = themeMenu_->actions();
    for (int i = 0; i < actions.size(); ++i)
        actions[i]->setCheckable(true);
    for (int i = 0; i < actions.size(); ++i)
        actions[i]->setChecked(i == themeIndex_);

    // 状态栏 — 更高、更宽敞
    statusBar()->setStyleSheet(QString(
        "QStatusBar {"
        "  background: %1; border-top: 1px solid %2;"
        "  font-size: 11px; padding: 2px 8px; }"
        "QStatusBar::item { border: none; }"
    ).arg(t.crust, t.surface0));

    statusLabel_->setStyleSheet(
        QString("color: %1; font-weight: bold; font-size: 11px;").arg(t.green));
    progressText_->setStyleSheet(
        QString("color: %1; font-size: 11px; padding-right: 8px;").arg(t.blue));
    statusProgress_->setStyleSheet(QString(
        "QProgressBar {"
        "  border: 1px solid %1; border-radius: 7px;"
        "  background: %2; text-align: center;"
        "  color: %3; font-size: 10px; }"
        "QProgressBar::chunk {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 %4, stop:1 %5);"
        "  border-radius: 6px; }"
    ).arg(t.surface1, t.base, t.text, t.blue, t.teal));

    // 底部文件面板
    filePanel_->setStyleSheet(QString(
        "QWidget { background: %1; color: %2; }"
        "QLabel { font-size: 12px; color: %3; font-weight: bold; }"
        "QLineEdit {"
        "  background: %4; border: 1px solid %5; border-radius: 5px;"
        "  padding: 5px 10px; font-size: 12px; color: %2;"
        "  selection-background-color: %5; }"
        "QLineEdit:focus { border-color: %3; }"
        "QLineEdit[readOnly=\"true\"] { color: %6; }"
        "QPushButton {"
        "  background: %7; color: %2; border: 1px solid %5;"
        "  border-radius: 5px; padding: 5px 12px; font-size: 12px; }"
        "QPushButton:hover { background: %5; border-color: %3; }"
        "QPushButton:pressed { background: %8; }"
    ).arg(t.crust, t.text, t.blue, t.base, t.surface1,
          t.overlay2, t.surface0, t.surface2));

    filePanelApplyBtn_->setStyleSheet(QString(
        "QPushButton {"
        "  background: %1; color: %2; border: none;"
        "  border-radius: 5px; padding: 6px 16px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %4; }"
    ).arg(t.blue, t.btnText, t.blueHover, t.bluePressed));

    // 各面板
    feModelPanel_->applyTheme(t);
    partsPanel_->applyTheme(t);
    monitorPanel_->applyTheme(t);
    resultPanel_->applyTheme(t);
    glWidget_->applyTheme(t);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings settings("FEModelViewer", "FEModelViewer");
    settings.setValue("lastModelPath", modelPathEdit_->text());
    settings.setValue("lastResultPath", resultPathEdit_->text());
    QMainWindow::closeEvent(event);
}

// ════════════════════════════════════════════════════════════
// 变形 / 云图辅助
// ════════════════════════════════════════════════════════════

const FERenderData& MainWindow::activeRenderData() const
{
    return deformActive_ ? deformedRD_ : feModelPanel_->currentRenderData();
}

const FEModel& MainWindow::activeModel() const
{
    return deformActive_ ? deformedModel_ : feModelPanel_->currentModel();
}

void MainWindow::updateFilterPlaneBounds()
{
    const FEModel& model = activeModel();
    if (model.nodes.empty()) {
        resultPanel_->setPlaneBounds(glm::vec3(0.0f), glm::vec3(0.0f));
        glWidget_->clearClipPlanePreview();
        return;
    }

    glm::vec3 bbMin, bbMax;
    model.computeBoundingBox(bbMin, bbMax);
    resultPanel_->setPlaneBounds(bbMin, bbMax);
}

void MainWindow::applyDeformation(float scale, bool overlayUndeformed)
{
    const FEModel& model = feModelPanel_->currentModel();
    if (model.nodes.empty()) return;

    FEVectorField disp = resultPanel_->currentDisplacement();
    if (disp.values.empty()) return;

    FEDeformationOptions opts;
    opts.scale = scale;
    opts.overlayUndeformed = overlayUndeformed;

    deformedModel_ = FEDeformation::apply(model, disp, opts);
    deformedRD_ = FEMeshConverter::toRenderData(deformedModel_);
    deformActive_ = true;
    deformScale_ = scale;
    deformOverlay_ = overlayUndeformed;

    if (overlayUndeformed) {
        glWidget_->setOverlayMesh(feModelPanel_->currentRenderData().mesh);
        glWidget_->setOverlayVisible(true);
    } else {
        glWidget_->setOverlayVisible(false);
    }

    glWidget_->setMesh(deformedRD_.mesh);
    glWidget_->setTriangleToElementMap(deformedRD_.triangleToElement);
    glWidget_->setVertexToNodeMap(deformedRD_.vertexToNode);
    glWidget_->setTriangleToPartMap(deformedRD_.triangleToPart);
    glWidget_->setEdgeToPartMap(deformedRD_.edgeToPart);

    float size = deformedModel_.computeSize();
    if (size > 0.0f)
        glWidget_->fitToModel(deformedModel_.computeCenter(), size);
    updateFilterPlaneBounds();
}

void MainWindow::clearDeformation()
{
    deformActive_ = false;

    const FERenderData& rd = feModelPanel_->currentRenderData();
    glWidget_->setOverlayVisible(false);
    glWidget_->setMesh(rd.mesh);
    glWidget_->setTriangleToElementMap(rd.triangleToElement);
    glWidget_->setVertexToNodeMap(rd.vertexToNode);
    glWidget_->setTriangleToPartMap(rd.triangleToPart);
    glWidget_->setEdgeToPartMap(rd.edgeToPart);

    const FEModel& model = feModelPanel_->currentModel();
    float size = model.computeSize();
    if (size > 0.0f)
        glWidget_->fitToModel(model.computeCenter(), size);
    updateFilterPlaneBounds();
}

void MainWindow::applyContour(const FEScalarField& field, const QString& title)
{
    activeContourField_ = field;
    activeContourTitle_ = title;
    contourActive_ = true;

    const FEModel& model = activeModel();
    if (model.nodes.empty()) return;

    const FERenderData& rd = filterActive_ ? filteredRD_ : activeRenderData();
    int vertCount = static_cast<int>(rd.mesh.vertices.size() / 6);
    if (vertCount == 0) return;

    const int numBands = 9;

    FEMappedScalars mapped = FEResultMapper::mapScalarToVertices(field, rd, model);

    glWidget_->setVertexScalars(mapped.scalars, mapped.minValue, mapped.maxValue, numBands);
    glWidget_->setColorBarVisible(true);
    glWidget_->setColorBarRange(mapped.minValue, mapped.maxValue);
    glWidget_->setColorBarTitle(title);
    glWidget_->setColorBarIdLabel(mapped.location == FieldLocation::Element ? "Ele ID" : "Node ID");
    glWidget_->setColorBarExtremes(mapped.minId, mapped.minValue, mapped.maxId, mapped.maxValue);
}

// ── 过滤方法 ──

static void applyFilteredRD(GLWidget* gl, const FERenderData& rd,
                             const std::vector<FEPart>& parts) {
    gl->setMesh(rd.mesh);
    gl->setTriangleToElementMap(rd.triangleToElement);
    gl->setVertexToNodeMap(rd.vertexToNode);
    gl->setTriangleToPartMap(rd.triangleToPart);
    gl->setEdgeToPartMap(rd.edgeToPart);
}

void MainWindow::applyThreshold(float minVal, float maxVal)
{
    const FERenderData& rd = activeRenderData();
    if (rd.triangleCount() == 0) return;

    FEScalarField field;
    QString title;
    if (!resultPanel_->currentScalarField(field, title)) return;

    // 对于节点场的阈值，需要先把节点值映射到单元上（取平均）
    FEScalarField elemField = field;
    if (field.location == FieldLocation::Node) {
        elemField.values.clear();
        elemField.location = FieldLocation::Element;
        const FEModel& model = activeModel();
        for (const auto& [eid, elem] : model.elements) {
            float sum = 0.0f;
            int n = 0;
            for (int nid : elem.nodeIds) {
                auto it = field.values.find(nid);
                if (it != field.values.end()) { sum += it->second; ++n; }
            }
            if (n > 0) elemField.values[eid] = sum / n;
        }
    }

    filteredRD_ = FEPostFilter::thresholdByElementValue(rd, elemField, minVal, maxVal);
    filterActive_ = true;

    glWidget_->clearSliceLines();
    glWidget_->clearIsoSurface();
    applyFilteredRD(glWidget_, filteredRD_, {});

    if (contourActive_)
        applyContour(activeContourField_, activeContourTitle_);
}

void MainWindow::applyClipPlane(const glm::vec3& origin, const glm::vec3& normal, bool keepPositive)
{
    const FERenderData& rd = activeRenderData();
    if (rd.triangleCount() == 0) return;

    FEPlane plane;
    plane.origin = origin;
    plane.normal = normal;

    filteredRD_ = FEPostFilter::clipByPlane(rd, plane, keepPositive);
    filterActive_ = true;

    glWidget_->clearSliceLines();
    glWidget_->clearIsoSurface();
    applyFilteredRD(glWidget_, filteredRD_, {});

    if (contourActive_)
        applyContour(activeContourField_, activeContourTitle_);
}

void MainWindow::applySlicePlane(const glm::vec3& origin, const glm::vec3& normal)
{
    glWidget_->clearSliceLines();
    glWidget_->clearIsoSurface();

    const FERenderData& rd = filterActive_ ? filteredRD_ : activeRenderData();
    if (rd.triangleCount() == 0) return;

    FEPlane plane;
    plane.origin = origin;
    plane.normal = normal;

    FESliceResult slice = FEPostFilter::sliceByPlane(rd, plane);
    glWidget_->setSliceLines(slice.lineVertices);
}

void MainWindow::applyIsoSurface(float isoValue)
{
    glWidget_->clearSliceLines();
    glWidget_->clearIsoSurface();

    const FEModel& model = activeModel();
    if (model.nodes.empty()) return;

    FEScalarField field;
    QString title;
    if (!resultPanel_->currentScalarField(field, title)) return;

    Mesh iso = FEIsoSurface::extract(model, field, isoValue);
    glWidget_->setIsoSurfaceMesh(iso);
}

void MainWindow::clearFilters()
{
    filterActive_ = false;
    glWidget_->clearSliceLines();
    glWidget_->clearIsoSurface();
    glWidget_->clearClipPlanePreview();

    const FERenderData& rd = activeRenderData();
    applyFilteredRD(glWidget_, rd, {});

    if (contourActive_)
        applyContour(activeContourField_, activeContourTitle_);
}
