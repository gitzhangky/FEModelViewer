/**
 * @file DisplayPanel.h
 * @brief 显示控制面板声明
 *
 * 控制几何渲染外观：
 *   - 显示模式：实体 / 线框 / 实体+线框
 *   - 投影方式：透视 / 正交
 *   - 物体颜色、边线颜色
 *
 * 本面板只发射信号，由 MainWindow 连接到 GLWidget 的对应接口。
 */

#pragma once

#include <QWidget>
#include <QColor>
#include <functional>
#include <glm/glm.hpp>

class QComboBox;
class QSpinBox;
class QSlider;
class QLabel;
class QPushButton;
struct Theme;

class DisplayPanel : public QWidget {
    Q_OBJECT

public:
    explicit DisplayPanel(QWidget* parent = nullptr);

    /** @brief 应用主题 */
    void applyTheme(const Theme& theme);

signals:
    /** @brief 显示模式改变（0=实体, 1=线框, 2=实体+线框） */
    void displayModeChanged(int mode);
    /** @brief 投影模式改变（0=透视, 1=正交） */
    void projectionModeChanged(int mode);
    /** @brief 物体颜色改变 */
    void objectColorChanged(const glm::vec3& color);
    /** @brief 边线颜色改变 */
    void edgeColorChanged(const glm::vec3& color);
    /** @brief 边线宽度改变（像素） */
    void edgeWidthChanged(int px);
    /** @brief 边线不透明度改变（百分比 0-100） */
    void edgeAlphaChanged(int percent);
    /** @brief 实体不透明度改变（百分比 0-100） */
    void surfaceAlphaChanged(int percent);

private:
    /** @brief 创建一个色块按钮，点击弹出取色对话框；选色后回填色块并通过 onPicked 回调 */
    QPushButton* makeColorButton(const QColor& initial,
                                 std::function<void(const glm::vec3&)> onPicked);
    /** @brief 把所有控件恢复到默认值（并发射相应信号） */
    void resetToDefaults();

    QComboBox*   modeCombo_   = nullptr;
    QComboBox*   projCombo_   = nullptr;
    QPushButton* objColorBtn_  = nullptr;
    QPushButton* edgeColorBtn_ = nullptr;
    QSpinBox*    edgeWidthSpin_    = nullptr;
    QSlider*     edgeAlphaSlider_    = nullptr;
    QSlider*     surfaceAlphaSlider_ = nullptr;
    QLabel*      edgeAlphaValue_     = nullptr;
    QLabel*      surfaceAlphaValue_  = nullptr;
    QPushButton* resetBtn_         = nullptr;
};
