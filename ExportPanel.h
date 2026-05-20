/**
 * @file ExportPanel.h
 * @brief 截图 / 录像导出面板声明
 *
 * 提供中央视口画面的导出能力：
 *   - 单帧截图保存为 PNG/JPG
 *   - 录制为 MP4（通过 ffmpeg 子进程编码）
 *
 * 本面板只负责 UI 与参数收集；实际的帧抓取与 ffmpeg 编码由 MainWindow
 * 监听本面板发出的请求信号后实现。
 */

#pragma once

#include <QWidget>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QPushButton;
class QLabel;
struct Theme;

class ExportPanel : public QWidget {
    Q_OBJECT

public:
    explicit ExportPanel(QWidget* parent = nullptr);

    /** @brief 应用主题 */
    void applyTheme(const Theme& theme);

    /** @brief ffmpeg 是否可用（决定录制按钮的可用性） */
    void setFfmpegAvailable(bool available);

    /** @brief 录制中状态切换（禁用参数控件、切换开始/停止按钮可用性） */
    void setRecording(bool recording);

    /** @brief 录制状态更新（已录帧数）。录制中不显示大小：ffmpeg 缓冲使磁盘大小不可靠 */
    void updateRecordingStats(int frames, int droppedFrames = 0);

    /** @brief 录制完成后显示最终文件大小（ffmpeg flush 完成后调用） */
    void setRecordingFinished(int frames, qint64 finalBytes, int droppedFrames = 0);

    // ── 用户配置访问器（供 MainWindow 读取参数） ──
    QString outputDir() const;
    int  framerate() const;
    int  maxDurationSec() const;
    /** @brief 分辨率选择：0=跟随视口, 1=1080p, 2=自定义 */
    int  resolutionMode() const;
    int  customWidth() const;
    int  customHeight() const;

signals:
    /** @brief 用户点击"截图" */
    void screenshotRequested();
    /** @brief 用户点击"开始录制" */
    void recordStartRequested();
    /** @brief 用户点击"停止录制" */
    void recordStopRequested();
    /** @brief 用户改了输出目录（持久化用） */
    void outputDirChanged(const QString& dir);

private slots:
    void onBrowseDir();
    void onResolutionModeChanged(int index);

private:
    // ── 输出路径 ──
    QLineEdit*   outputDirEdit_ = nullptr;
    QPushButton* browseBtn_     = nullptr;

    // ── 参数 ──
    QSpinBox*  fpsSpin_         = nullptr;
    QSpinBox*  maxDurSpin_      = nullptr;
    QComboBox* resolutionCombo_ = nullptr;
    QSpinBox*  customWSpin_     = nullptr;
    QSpinBox*  customHSpin_     = nullptr;
    QWidget*   customResRow_    = nullptr;  // 自定义分辨率行（仅自定义模式可见）

    // ── 操作按钮 ──
    QPushButton* screenshotBtn_ = nullptr;
    QPushButton* startBtn_      = nullptr;
    QPushButton* stopBtn_       = nullptr;

    // ── 状态显示 ──
    QLabel* ffmpegStatusLabel_ = nullptr;  // ffmpeg 检测结果
    QLabel* recStatusLabel_    = nullptr;  // 录制状态（帧数 / 大小 / 时长）

    bool ffmpegAvailable_ = false;
    bool recording_       = false;
};
