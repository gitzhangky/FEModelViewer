/**
 * @file MainWindow.h
 * @brief 主窗口声明
 *
 * MainWindow 是应用程序的顶层窗口，负责：
 *   - 工具栏（拾取模式切换）
 *   - 左侧边栏（模型树 + 选中信息 + 监控面板）
 *   - 中央 GL 视口
 *   - 底部文件导入面板（模型文件 + 结果文件）
 *   - 状态栏（模型统计）
 */

#pragma once

#include <QMainWindow>
#include <QActionGroup>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>

class GLWidget;
class MonitorPanel;
class FEModelPanel;
class PartsPanel;

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

    void browseModelFile();
    void browseResultFile();
    void applyFiles();

    GLWidget*      glWidget_      = nullptr;
    MonitorPanel*  monitorPanel_  = nullptr;
    FEModelPanel*  feModelPanel_  = nullptr;
    PartsPanel*    partsPanel_    = nullptr;

    // 工具栏拾取模式动作组
    QActionGroup*  pickGroup_     = nullptr;

    // 状态栏
    QLabel*        statusLabel_    = nullptr;
    QProgressBar*  statusProgress_ = nullptr;
    QLabel*        progressText_   = nullptr;

    // 底部文件面板
    QLineEdit*     modelPathEdit_  = nullptr;
    QLineEdit*     resultPathEdit_ = nullptr;
};
