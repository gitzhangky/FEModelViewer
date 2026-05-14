/**
 * @file MainWindow.h
 * @brief 主窗口声明
 *
 * HyperView 风格布局：
 *   - 工具栏（拾取模式切换 + 主题选择）
 *   - 左侧停靠：部件面板
 *   - 中央 GL 视口 + 底部标签页（文件导入 / 结果显示 / 监控）
 *   - 右侧停靠：模型信息面板
 *   - 状态栏
 */

#pragma once

#include <QMainWindow>
#include <QActionGroup>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QToolBar>
#include <QMenu>
#include <QTabWidget>
#include <QDockWidget>

#include "Theme.h"
#include "FERenderData.h"
#include "FEModel.h"
#include "FEField.h"

class GLWidget;
class MonitorPanel;
class FEModelPanel;
class PartsPanel;
class ResultPanel;
class FEAnimationController;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupToolBar();
    void setupStatusBar();
    QWidget* createFilePanel();
    void applyTheme(const Theme& theme);

    void browseModelFile();
    void browseResultFile();
    void applyFiles();

    void applyDeformation(float scale, bool overlayUndeformed);
    void clearDeformation();
    void applyContour(const FEScalarField& field, const QString& title);
    void applyThreshold(float minVal, float maxVal);
    void applyClipPlane(const glm::vec3& origin, const glm::vec3& normal, bool keepPositive);
    void applySlicePlane(const glm::vec3& origin, const glm::vec3& normal);
    void applyIsoSurface(float isoValue);
    void clearFilters();
    const FERenderData& activeRenderData() const;
    const FEModel& activeModel() const;

    GLWidget*              glWidget_        = nullptr;
    MonitorPanel*          monitorPanel_    = nullptr;
    FEModelPanel*          feModelPanel_    = nullptr;
    PartsPanel*            partsPanel_      = nullptr;
    ResultPanel*           resultPanel_     = nullptr;
    FEAnimationController* animController_  = nullptr;

    // 工具栏拾取模式动作组
    QActionGroup*  pickGroup_     = nullptr;

    // 状态栏
    QLabel*        statusLabel_    = nullptr;
    QProgressBar*  statusProgress_ = nullptr;
    QLabel*        progressText_   = nullptr;

    // 底部标签页面板
    QTabWidget*    bottomTabs_     = nullptr;
    QLineEdit*     modelPathEdit_  = nullptr;
    QLineEdit*     resultPathEdit_ = nullptr;

    // 侧边栏停靠
    QDockWidget*   partsDock_      = nullptr;
    QDockWidget*   modelInfoDock_  = nullptr;

    // 变形状态
    bool           deformActive_   = false;
    float          deformScale_    = 1.0f;
    bool           deformOverlay_  = false;
    FERenderData   deformedRD_;
    FEModel        deformedModel_;

    // 过滤状态
    bool           filterActive_   = false;
    FERenderData   filteredRD_;
    FEScalarField  activeContourField_;
    QString        activeContourTitle_;
    bool           contourActive_  = false;

    // 主题相关
    Theme          currentTheme_;
    QWidget*       filePanel_      = nullptr;
    QToolBar*      toolbar_        = nullptr;
    QPushButton*   filePanelApplyBtn_ = nullptr;
    QAction*       themeAction_    = nullptr;
    QMenu*         themeMenu_      = nullptr;
    int            themeIndex_     = 0;
};
