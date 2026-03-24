/**
 * @file Theme.h
 * @brief 深色/浅色主题颜色定义
 *
 * 基于 Catppuccin Mocha（深色）和 Catppuccin Latte（浅色）配色方案。
 * 所有面板通过 applyTheme() 方法接收主题并更新样式。
 */

#pragma once

#include <QString>

struct Theme {
    // ── UI 颜色（Qt 样式表用） ──
    QString base;         // 面板主背景
    QString mantle;       // 分组/深层背景
    QString crust;        // 最深层背景
    QString surface0;     // 分隔线/次要背景
    QString surface1;     // hover/选中背景
    QString surface2;     // 边框/indicator
    QString overlay0;     // 弱文字（提示）
    QString overlay2;     // 次要文字
    QString subtext0;     // 子标题
    QString subtext1;     // 标签文字
    QString text;         // 主文字
    QString blue;         // 强调色
    QString blueHover;    // 蓝色 hover
    QString bluePressed;  // 蓝色 pressed
    QString green;        // 成功/绿色标题
    QString red;          // 错误/红色标题
    QString teal;         // 渐变辅助色
    QString btnText;      // 蓝色按钮上的文字
    QString gradTop;      // 按钮渐变顶色
    QString gradBot;      // 按钮渐变底色
    QString gradTopHov;   // 按钮渐变顶色 hover
    QString gradBotHov;   // 按钮渐变底色 hover

    // ── GL 视口背景渐变 ──
    float bgTopR, bgTopG, bgTopB;   // 顶部颜色
    float bgBotR, bgBotG, bgBotB;   // 底部颜色

    // ── 色标文字颜色 ──
    int barTextR, barTextG, barTextB;

    bool isDark;

    // ── 工厂方法 ──
    static Theme dark() {
        Theme t;
        t.isDark      = true;
        t.base        = "#1e1e2e";
        t.mantle      = "#181825";
        t.crust       = "#11111b";
        t.surface0    = "#313244";
        t.surface1    = "#45475a";
        t.surface2    = "#585b70";
        t.overlay0    = "#6c7086";
        t.overlay2    = "#9399b2";
        t.subtext0    = "#a6adc8";
        t.subtext1    = "#bac2de";
        t.text        = "#cdd6f4";
        t.blue        = "#89b4fa";
        t.blueHover   = "#b4d0fb";
        t.bluePressed = "#74a8f7";
        t.green       = "#a6e3a1";
        t.red         = "#f38ba8";
        t.teal        = "#94e2d5";
        t.btnText     = "#1e1e2e";
        t.gradTop     = "#45475a";
        t.gradBot     = "#313244";
        t.gradTopHov  = "#585b70";
        t.gradBotHov  = "#45475a";
        // GL 视口渐变
        t.bgTopR = 0.38f; t.bgTopG = 0.45f; t.bgTopB = 0.58f;
        t.bgBotR = 0.68f; t.bgBotG = 0.74f; t.bgBotB = 0.82f;
        // 色标文字
        t.barTextR = 255; t.barTextG = 255; t.barTextB = 255;
        return t;
    }

    static Theme light() {
        Theme t;
        t.isDark      = false;
        t.base        = "#eff1f5";
        t.mantle      = "#e6e9ef";
        t.crust       = "#e6e9ef";
        t.surface0    = "#ccd0da";
        t.surface1    = "#bcc0cc";
        t.surface2    = "#acb0be";
        t.overlay0    = "#9ca0b0";
        t.overlay2    = "#7c7f93";
        t.subtext0    = "#6c6f85";
        t.subtext1    = "#5c5f77";
        t.text        = "#4c4f69";
        t.blue        = "#1e66f5";
        t.blueHover   = "#4d8af7";
        t.bluePressed = "#1559d6";
        t.green       = "#40a02b";
        t.red         = "#d20f39";
        t.teal        = "#179299";
        t.btnText     = "#ffffff";
        t.gradTop     = "#dce0e8";
        t.gradBot     = "#ccd0da";
        t.gradTopHov  = "#ccd0da";
        t.gradBotHov  = "#bcc0cc";
        // GL 视口渐变
        t.bgTopR = 0.93f; t.bgTopG = 0.95f; t.bgTopB = 0.97f;
        t.bgBotR = 0.82f; t.bgBotG = 0.85f; t.bgBotB = 0.90f;
        // 色标文字
        t.barTextR = 30; t.barTextG = 30; t.barTextB = 30;
        return t;
    }
};
