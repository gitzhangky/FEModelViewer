/**
 * @file AppearancePanel.h
 * @brief 外观控制面板声明
 *
 * 控制背景与云图色彩：
 *   - 背景渐变：上/下颜色自定义 + 恢复主题
 *   - 云图色谱：Jet / 灰度 / 冷暖
 *   - 云图分段数
 *
 * 本面板只发射信号，由 MainWindow 连接到 GLWidget 的对应接口。
 */

#pragma once

#include <QWidget>
#include <QColor>
#include <glm/glm.hpp>

class QComboBox;
class QSpinBox;
class QPushButton;
class QCheckBox;
struct Theme;

class AppearancePanel : public QWidget {
    Q_OBJECT

public:
    explicit AppearancePanel(QWidget* parent = nullptr);

    /** @brief 应用主题（同时把背景色块复位为该主题预设） */
    void applyTheme(const Theme& theme);

signals:
    /** @brief 背景上/下渐变色改变 */
    void backgroundColorsChanged(const glm::vec3& top, const glm::vec3& bottom);
    /** @brief 请求把背景恢复为当前主题预设 */
    void backgroundResetRequested();
    /** @brief 云图色谱改变（0=Jet, 1=灰度, 2=冷暖） */
    void colormapChanged(int map);
    /** @brief 云图分段数改变 */
    void numBandsChanged(int bands);
    /** @brief 反转色谱开关改变 */
    void colormapInvertedChanged(bool inverted);

private:
    void emitBackground();
    /** @brief 把色谱/分段数/反转恢复默认（并发射相应信号） */
    void resetColormapDefaults();

    QPushButton* topColorBtn_ = nullptr;
    QPushButton* botColorBtn_ = nullptr;
    QPushButton* resetBgBtn_  = nullptr;
    QComboBox*   colormapCombo_ = nullptr;
    QSpinBox*    bandsSpin_     = nullptr;
    QCheckBox*   invertCheck_   = nullptr;
    QPushButton* resetColormapBtn_ = nullptr;
};
