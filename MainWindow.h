/**
 * @file MainWindow.h
 * @brief 主窗口声明
 *
 * MainWindow 是应用程序的顶层窗口，负责：
 *   - 组装左侧边栏（FEModelPanel + MonitorPanel）和 GL 视口
 *   - 将监控面板绑定到 GLWidget 以读取实时数据
 */

#pragma once

#include <QMainWindow>

class GLWidget;
class MonitorPanel;
class FEModelPanel;
class PartsPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

private:
    GLWidget*      glWidget_      = nullptr;
    MonitorPanel*  monitorPanel_  = nullptr;
    FEModelPanel*  feModelPanel_  = nullptr;
    PartsPanel*    partsPanel_    = nullptr;
};
