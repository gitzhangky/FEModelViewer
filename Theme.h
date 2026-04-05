/**
 * @file Theme.h
 * @brief 多主题颜色定义
 *
 * 内置 6 种主题：
 *   - Dark（Catppuccin Mocha）   — 经典深色
 *   - Light（Catppuccin Latte）  — 经典浅色
 *   - Ocean（深海蓝）             — 深蓝色调，科技感
 *   - Forest（森林）              — 深绿色调，自然感
 *   - Sunset（暮光）              — 暖色调，沉稳
 *   - Nord（北欧极光）            — 冷灰蓝色调，柔和
 *
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
    QString name;         // 主题名称（用于 UI 显示）

    // ── 主题数量 ──
    static constexpr int count() { return 6; }

    // ── 按索引获取主题 ──
    static Theme byIndex(int index) {
        switch (index) {
        case 0: return dark();
        case 1: return light();
        case 2: return ocean();
        case 3: return forest();
        case 4: return sunset();
        case 5: return nord();
        default: return dark();
        }
    }

    // ── 工厂方法 ──

    /** 经典深色 — Catppuccin Mocha */
    static Theme dark() {
        Theme t;
        t.name        = "深色";
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

    /** 经典浅色 — Catppuccin Latte */
    static Theme light() {
        Theme t;
        t.name        = "浅色";
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

    /** 深海蓝 — 深蓝色调，科技感 */
    static Theme ocean() {
        Theme t;
        t.name        = "深海蓝";
        t.isDark      = true;
        t.base        = "#0d1b2a";
        t.mantle      = "#0a1628";
        t.crust       = "#071020";
        t.surface0    = "#1b2d45";
        t.surface1    = "#243b55";
        t.surface2    = "#2e4a66";
        t.overlay0    = "#4a6a8a";
        t.overlay2    = "#7a9ab5";
        t.subtext0    = "#a0bdd0";
        t.subtext1    = "#b8d0e0";
        t.text        = "#d6e5f0";
        t.blue        = "#48b9e6";
        t.blueHover   = "#6fcbef";
        t.bluePressed = "#35a5d4";
        t.green       = "#50d890";
        t.red         = "#ff6b7a";
        t.teal        = "#00d4aa";
        t.btnText     = "#0d1b2a";
        t.gradTop     = "#243b55";
        t.gradBot     = "#1b2d45";
        t.gradTopHov  = "#2e4a66";
        t.gradBotHov  = "#243b55";
        // GL 视口渐变：深海蓝到靛蓝
        t.bgTopR = 0.10f; t.bgTopG = 0.18f; t.bgTopB = 0.32f;
        t.bgBotR = 0.22f; t.bgBotG = 0.40f; t.bgBotB = 0.58f;
        // 色标文字
        t.barTextR = 214; t.barTextG = 229; t.barTextB = 240;
        return t;
    }

    /** 森林 — 深绿色调，自然感 */
    static Theme forest() {
        Theme t;
        t.name        = "森林";
        t.isDark      = true;
        t.base        = "#1a2318";
        t.mantle      = "#151e14";
        t.crust       = "#101810";
        t.surface0    = "#2a3a26";
        t.surface1    = "#354832";
        t.surface2    = "#40563d";
        t.overlay0    = "#5a7a55";
        t.overlay2    = "#82a57c";
        t.subtext0    = "#a5c4a0";
        t.subtext1    = "#b8d4b4";
        t.text        = "#d4e8d0";
        t.blue        = "#7ec880";
        t.blueHover   = "#9cd89e";
        t.bluePressed = "#66b868";
        t.green       = "#a8e6a0";
        t.red         = "#e87070";
        t.teal        = "#60d0a0";
        t.btnText     = "#1a2318";
        t.gradTop     = "#354832";
        t.gradBot     = "#2a3a26";
        t.gradTopHov  = "#40563d";
        t.gradBotHov  = "#354832";
        // GL 视口渐变：林间雾气
        t.bgTopR = 0.16f; t.bgTopG = 0.24f; t.bgTopB = 0.18f;
        t.bgBotR = 0.38f; t.bgBotG = 0.52f; t.bgBotB = 0.40f;
        // 色标文字
        t.barTextR = 212; t.barTextG = 232; t.barTextB = 208;
        return t;
    }

    /** 暮光 — 暖色调，沉稳 */
    static Theme sunset() {
        Theme t;
        t.name        = "暮光";
        t.isDark      = true;
        t.base        = "#1f1520";
        t.mantle      = "#1a111c";
        t.crust       = "#140d16";
        t.surface0    = "#302435";
        t.surface1    = "#3e3045";
        t.surface2    = "#4d3d55";
        t.overlay0    = "#6e5a78";
        t.overlay2    = "#9580a0";
        t.subtext0    = "#b8a5c5";
        t.subtext1    = "#c8b8d2";
        t.text        = "#e4d8ec";
        t.blue        = "#e8a060";
        t.blueHover   = "#f0b878";
        t.bluePressed = "#d89050";
        t.green       = "#c8d87a";
        t.red         = "#f06888";
        t.teal        = "#e89060";
        t.btnText     = "#1f1520";
        t.gradTop     = "#3e3045";
        t.gradBot     = "#302435";
        t.gradTopHov  = "#4d3d55";
        t.gradBotHov  = "#3e3045";
        // GL 视口渐变：日落晚霞
        t.bgTopR = 0.22f; t.bgTopG = 0.14f; t.bgTopB = 0.24f;
        t.bgBotR = 0.60f; t.bgBotG = 0.35f; t.bgBotB = 0.30f;
        // 色标文字
        t.barTextR = 228; t.barTextG = 216; t.barTextB = 236;
        return t;
    }

    /** 北欧极光 — Nord 冷灰蓝色调，柔和 */
    static Theme nord() {
        Theme t;
        t.name        = "北欧极光";
        t.isDark      = true;
        t.base        = "#2e3440";
        t.mantle      = "#292e39";
        t.crust       = "#242933";
        t.surface0    = "#3b4252";
        t.surface1    = "#434c5e";
        t.surface2    = "#4c566a";
        t.overlay0    = "#616e88";
        t.overlay2    = "#8891a5";
        t.subtext0    = "#a3adc2";
        t.subtext1    = "#b8c1d4";
        t.text        = "#d8dee9";
        t.blue        = "#88c0d0";
        t.blueHover   = "#a0d0dd";
        t.bluePressed = "#70b0c0";
        t.green       = "#a3be8c";
        t.red         = "#bf616a";
        t.teal        = "#8fbcbb";
        t.btnText     = "#2e3440";
        t.gradTop     = "#434c5e";
        t.gradBot     = "#3b4252";
        t.gradTopHov  = "#4c566a";
        t.gradBotHov  = "#434c5e";
        // GL 视口渐变：北极光
        t.bgTopR = 0.22f; t.bgTopG = 0.26f; t.bgTopB = 0.32f;
        t.bgBotR = 0.46f; t.bgBotG = 0.58f; t.bgBotB = 0.65f;
        // 色标文字
        t.barTextR = 216; t.barTextG = 222; t.barTextB = 233;
        return t;
    }
};
