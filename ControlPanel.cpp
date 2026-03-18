/**
 * @file ControlPanel.cpp
 * @brief 控制面板实现
 */

#include "ControlPanel.h"

#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <functional>

ControlPanel::ControlPanel(QWidget* parent) : QWidget(parent) {
    // 固定面板宽度
    setFixedWidth(200);

    // 垂直布局：形状 → 显示 → 颜色 → 弹性空白 → 操作提示
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);

    layout->addWidget(createShapeGroup());
    layout->addWidget(createDisplayGroup());
    layout->addWidget(createColorGroup());
    layout->addStretch();  // 弹性空白，将底部提示推到最下方

    // 底部操作提示标签
    auto* infoLabel = new QLabel("左键旋转 | 右键平移\n滚轮缩放 | ESC退出");
    infoLabel->setStyleSheet("color: #6c7086; font-size: 11px;");
    layout->addWidget(infoLabel);

    // ── 统一样式表（Catppuccin Mocha 配色方案）──
    setStyleSheet(
        // 面板整体背景和文字颜色
        "QWidget { background: #1e1e2e; color: #cdd6f4; }"

        // 按钮：渐变背景，hover 高亮边框，按下变蓝色反色
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 #45475a, stop:1 #313244);"
        "  border: 1px solid #585b70; border-radius: 5px;"
        "  padding: 7px 12px; color: #cdd6f4; font-size: 13px; }"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 #585b70, stop:1 #45475a);"
        "  border-color: #89b4fa; }"
        "QPushButton:pressed {"
        "  background: #89b4fa; color: #1e1e2e; }"

        // 分组框：深色背景，圆角边框，蓝色标题
        "QGroupBox {"
        "  background: #181825; border: 1px solid #313244;"
        "  border-radius: 6px; margin-top: 12px; padding: 16px 8px 8px 8px;"
        "  font-weight: bold; font-size: 12px; color: #a6adc8; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 10px; padding: 0 4px;"
        "  color: #89b4fa; }"

        // 标签
        "QLabel { font-size: 12px; color: #bac2de; }"

        // 复选框：自定义勾选指示器样式
        "QCheckBox { font-size: 12px; color: #bac2de; spacing: 6px; }"
        "QCheckBox::indicator {"
        "  width: 14px; height: 14px; border-radius: 3px;"
        "  border: 1px solid #585b70; background: #313244; }"
        "QCheckBox::indicator:checked {"
        "  background: #89b4fa; border-color: #89b4fa; }"

        // 下拉框：hover 蓝色边框，下拉列表统一暗色主题
        "QComboBox {"
        "  background: #313244; border: 1px solid #45475a;"
        "  border-radius: 5px; padding: 5px 8px; color: #cdd6f4; }"
        "QComboBox:hover { border-color: #89b4fa; }"
        "QComboBox::drop-down {"
        "  border: none; width: 20px; }"
        "QComboBox QAbstractItemView {"
        "  background: #313244; border: 1px solid #45475a;"
        "  selection-background-color: #89b4fa;"
        "  selection-color: #1e1e2e; color: #cdd6f4; }"
    );
}

QGroupBox* ControlPanel::createShapeGroup() {
    auto* group = new QGroupBox("形状");
    auto* layout = new QVBoxLayout(group);

    // 形状条目：显示名称 + 对应的网格生成函数
    struct Entry { QString name; std::function<Mesh()> gen; };
    std::vector<Entry> shapes = {
        {"正方体", []{ return Geometry::cube(); }},
        {"三棱锥", []{ return Geometry::tetrahedron(); }},
        {"三棱柱", []{ return Geometry::triangularPrism(); }},
        {"圆柱",   []{ return Geometry::cylinder(); }},
        {"圆锥",   []{ return Geometry::cone(); }},
        {"球体",   []{ return Geometry::sphere(); }},
        {"圆环",   []{ return Geometry::torus(); }},
    };

    // 为每个形状创建按钮，点击时生成网格并通过信号发射
    for (auto& s : shapes) {
        auto* btn = new QPushButton(s.name);
        layout->addWidget(btn);
        auto gen = s.gen;  // 捕获生成函数到 lambda
        connect(btn, &QPushButton::clicked, this, [this, gen]{
            emit shapeSelected(gen());
        });
    }
    return group;
}

QGroupBox* ControlPanel::createDisplayGroup() {
    auto* group = new QGroupBox("显示");
    auto* layout = new QVBoxLayout(group);

    // 渲染模式下拉框（索引与 GLWidget::DisplayMode 枚举对应）
    auto* modeCombo = new QComboBox;
    modeCombo->addItem("实体");          // 索引 0 → Solid
    modeCombo->addItem("线框");          // 索引 1 → Wireframe
    modeCombo->addItem("实体 + 线框");   // 索引 2 → SolidWireframe
    layout->addWidget(modeCombo);
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ControlPanel::displayModeChanged);

    // 自动旋转复选框（默认开启）
    auto* rotateCheck = new QCheckBox("自动旋转");
    rotateCheck->setChecked(true);
    layout->addWidget(rotateCheck);
    connect(rotateCheck, &QCheckBox::toggled,
            this, &ControlPanel::autoRotateChanged);

    return group;
}

QGroupBox* ControlPanel::createColorGroup() {
    auto* group = new QGroupBox("颜色");
    auto* layout = new QVBoxLayout(group);

    // 预设颜色列表
    colors_ = {
        {"天蓝",   {0.4f, 0.7f, 1.0f}},
        {"珊瑚红", {1.0f, 0.5f, 0.4f}},
        {"翡翠绿", {0.2f, 0.9f, 0.5f}},
        {"金色",   {1.0f, 0.84f, 0.0f}},
        {"紫罗兰", {0.7f, 0.4f, 1.0f}},
        {"银白",   {0.85f, 0.85f, 0.9f}},
    };

    // 颜色下拉菜单
    auto* combo = new QComboBox;
    for (auto& c : colors_) combo->addItem(c.name);
    layout->addWidget(combo);

    // 选中颜色时发射信号
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (idx >= 0 && idx < static_cast<int>(colors_.size()))
            emit colorChanged(colors_[idx].color);
    });

    return group;
}
