/**
 * @file MainWindow.h
 * @brief 主窗口声明
 *
 * MainWindow 是应用程序的顶层窗口，负责：
 *   - 工具栏（模型加载、拾取模式切换）
 *   - 左侧边栏（模型树 + 选中信息 + 监控面板）
 *   - 中央 GL 视口
 *   - 状态栏（模型统计）
 */

#pragma once

#include <QMainWindow>
#include <QActionGroup>
#include <QLabel>

class GLWidget;
class MonitorPanel;
class FEModelPanel;
class PartsPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

private:
    void setupToolBar();
    void setupStatusBar();

    GLWidget*      glWidget_      = nullptr;
    MonitorPanel*  monitorPanel_  = nullptr;
    FEModelPanel*  feModelPanel_  = nullptr;
    PartsPanel*    partsPanel_    = nullptr;

    // 工具栏拾取模式动作组
    QActionGroup*  pickGroup_     = nullptr;

    // 状态栏标签
    QLabel*        statusLabel_   = nullptr;
};
