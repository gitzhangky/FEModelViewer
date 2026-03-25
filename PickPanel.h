/**
 * @file PickPanel.h
 * @brief 右侧拾取控制面板
 *
 * 单个"拾取"卡片内包含：拾取模式切换、显示/隐藏、ID标签显示/隐藏。
 * 采用 QGroupBox 卡片风格，与左侧边栏一致。
 */

#pragma once

#include <QWidget>
#include <QRadioButton>
#include <QCheckBox>
#include <QButtonGroup>
#include <QGroupBox>
#include <QLabel>

struct Theme;

class PickPanel : public QWidget {
    Q_OBJECT

public:
    explicit PickPanel(QWidget* parent = nullptr);

    /** @brief 应用主题 */
    void applyTheme(const Theme& theme);

signals:
    // ── 拾取模式 ──
    void pickModeChanged(int mode);  // 0=Node, 1=Element, 2=Part

    // ── 显示/隐藏 ──
    void nodeVisibilityChanged(bool visible);
    void elementVisibilityChanged(bool visible);
    void partVisibilityChanged(bool visible);

    // ── ID标签显示/隐藏 ──
    void nodeLabelChanged(bool visible);
    void elementLabelChanged(bool visible);
    void partLabelChanged(bool visible);

private:
    QGroupBox* pickGroup_      = nullptr;   // 卡片容器

    // 拾取模式
    QButtonGroup* modeGroup_   = nullptr;
    QRadioButton* nodeRadio_   = nullptr;
    QRadioButton* elemRadio_   = nullptr;
    QRadioButton* partRadio_   = nullptr;

    // 显示/隐藏
    QLabel*    visLabel_       = nullptr;
    QCheckBox* nodeVisCheck_   = nullptr;
    QCheckBox* elemVisCheck_   = nullptr;
    QCheckBox* partVisCheck_   = nullptr;

    // ID标签
    QLabel*    labelLabel_     = nullptr;
    QCheckBox* nodeLabelCheck_ = nullptr;
    QCheckBox* elemLabelCheck_ = nullptr;
    QCheckBox* partLabelCheck_ = nullptr;
};
