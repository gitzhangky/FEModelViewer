/**
 * @file MainWindow.h
 * @brief 主窗口声明
 *
 * 基于 QDockWidget 的可停靠面板布局：
 *   - 工具栏（拾取模式切换 + 主题选择）
 *   - 中央 GL 视口
 *   - 停靠面板：模型树、模型信息、监控、结果显示、文件导入
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

#include "Theme.h"

class GLWidget;
class MonitorPanel;
class FEModelPanel;
class PartsPanel;
class ResultPanel;

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

    GLWidget*      glWidget_      = nullptr;
    MonitorPanel*  monitorPanel_  = nullptr;
    FEModelPanel*  feModelPanel_  = nullptr;
    PartsPanel*    partsPanel_    = nullptr;
    ResultPanel*   resultPanel_   = nullptr;

    // 工具栏拾取模式动作组
    QActionGroup*  pickGroup_     = nullptr;

    // 状态栏
    QLabel*        statusLabel_    = nullptr;
    QProgressBar*  statusProgress_ = nullptr;
    QLabel*        progressText_   = nullptr;

    // 底部文件面板
    QLineEdit*     modelPathEdit_  = nullptr;
    QLineEdit*     resultPathEdit_ = nullptr;

    // 主题相关
    Theme          currentTheme_;
    QWidget*       filePanel_      = nullptr;
    QToolBar*      toolbar_        = nullptr;
    QPushButton*   filePanelApplyBtn_ = nullptr;
    QAction*       themeAction_    = nullptr;
    QMenu*         themeMenu_      = nullptr;
    int            themeIndex_     = 0;
};
