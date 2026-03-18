/**
 * @file FEModelPanel.cpp
 * @brief 有限元模型显示面板实现
 *
 * 包含测试模型生成算法和面板 UI 逻辑。
 */

#include "FEModelPanel.h"
#include "FEMeshConverter.h"

#include <QVBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QRegExp>
#include <QProgressDialog>
#include <QApplication>
#include <functional>

// ════════════════════════════════════════════════════════════
// 构造函数
// ════════════════════════════════════════════════════════════

FEModelPanel::FEModelPanel(QWidget* parent) : QWidget(parent) {
    setFixedWidth(200);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);

    layout->addWidget(createLoadGroup());
    layout->addWidget(createInfoGroup());
    layout->addWidget(createOptionGroup());
    layout->addStretch();

    // ── 统一样式表（Catppuccin Mocha 配色，绿色主题标题与其他面板区分）──
    setStyleSheet(
        "QWidget { background: #1e1e2e; color: #cdd6f4; }"

        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 #45475a, stop:1 #313244);"
        "  border: 1px solid #585b70; border-radius: 5px;"
        "  padding: 7px 12px; color: #cdd6f4; font-size: 13px; }"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 #585b70, stop:1 #45475a);"
        "  border-color: #a6e3a1; }"
        "QPushButton:pressed {"
        "  background: #a6e3a1; color: #1e1e2e; }"

        "QGroupBox {"
        "  background: #181825; border: 1px solid #313244;"
        "  border-radius: 6px; margin-top: 12px; padding: 16px 8px 8px 8px;"
        "  font-weight: bold; font-size: 12px; color: #a6adc8; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 10px; padding: 0 4px;"
        "  color: #a6e3a1; }"  // 绿色标题（区别于 ControlPanel 的蓝色）

        "QLabel { font-size: 12px; color: #bac2de; }"

        "QCheckBox { font-size: 12px; color: #bac2de; spacing: 6px; }"
        "QCheckBox::indicator {"
        "  width: 14px; height: 14px; border-radius: 3px;"
        "  border: 1px solid #585b70; background: #313244; }"
        "QCheckBox::indicator:checked {"
        "  background: #a6e3a1; border-color: #a6e3a1; }"

        "QComboBox {"
        "  background: #313244; border: 1px solid #45475a;"
        "  border-radius: 5px; padding: 5px 8px; color: #cdd6f4; }"
        "QComboBox:hover { border-color: #a6e3a1; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView {"
        "  background: #313244; border: 1px solid #45475a;"
        "  selection-background-color: #a6e3a1;"
        "  selection-color: #1e1e2e; color: #cdd6f4; }"
    );
}

// ════════════════════════════════════════════════════════════
// UI 分组创建
// ════════════════════════════════════════════════════════════

/**
 * @brief 创建模型加载分组
 *
 * 包含测试模型生成按钮和清空按钮。
 * 由于暂无文件解析器，使用程序化生成的测试模型演示功能。
 */
QGroupBox* FEModelPanel::createLoadGroup() {
    auto* group = new QGroupBox("模型加载");
    auto* layout = new QVBoxLayout(group);

    // 打开文件按钮
    auto* openBtn = new QPushButton("打开文件...");
    layout->addWidget(openBtn);
    connect(openBtn, &QPushButton::clicked, this, &FEModelPanel::loadModelFromFile);

    // 悬臂梁模型按钮（3D HEX8 单元网格）
    auto* beamBtn = new QPushButton("悬臂梁 (HEX8)");
    layout->addWidget(beamBtn);
    connect(beamBtn, &QPushButton::clicked, this, &FEModelPanel::generateBeamModel);

    // 平板模型按钮（2D QUAD4 单元网格）
    auto* plateBtn = new QPushButton("平板 (QUAD4)");
    layout->addWidget(plateBtn);
    connect(plateBtn, &QPushButton::clicked, this, &FEModelPanel::generatePlateModel);

    // 混合单元模型按钮（TRI3 + QUAD4）
    auto* mixedBtn = new QPushButton("混合网格");
    layout->addWidget(mixedBtn);
    connect(mixedBtn, &QPushButton::clicked, this, &FEModelPanel::generateMixedModel);

    // 清空模型
    auto* clearBtn = new QPushButton("清空模型");
    layout->addWidget(clearBtn);
    connect(clearBtn, &QPushButton::clicked, this, [this]{
        currentModel_.clear();
        currentRenderData_.clear();
        updateInfoLabels();
        // 发送空 Mesh 清空显示
        emit meshGenerated(Mesh{}, glm::vec3(0), 0, {}, {});
    });

    return group;
}

/**
 * @brief 创建模型信息分组
 *
 * 显示当前模型的统计信息。
 * 标签内容在 updateInfoLabels() 中动态更新。
 */
QGroupBox* FEModelPanel::createInfoGroup() {
    auto* group = new QGroupBox("模型信息");
    auto* layout = new QVBoxLayout(group);

    // 使用 lambda 简化标签创建
    auto makeLabel = [layout](const QString& text) -> QLabel* {
        auto* label = new QLabel(text);
        layout->addWidget(label);
        return label;
    };

    nodeCountLabel_     = makeLabel("节点数: 0");
    elementCountLabel_  = makeLabel("单元数: 0");
    triangleCountLabel_ = makeLabel("三角面数: 0");
    modelSizeLabel_     = makeLabel("尺寸: -");

    return group;
}

/**
 * @brief 创建显示选项分组
 *
 * 拾取模式选择等控制选项。
 */
QGroupBox* FEModelPanel::createOptionGroup() {
    auto* group = new QGroupBox("显示选项");
    auto* layout = new QVBoxLayout(group);

    // 拾取模式选择
    auto* pickLabel = new QLabel("拾取模式:");
    layout->addWidget(pickLabel);

    auto* pickCombo = new QComboBox;
    pickCombo->addItem("节点");   // 0 → PickMode::Node
    pickCombo->addItem("单元");   // 1 → PickMode::Element
    pickCombo->addItem("面");     // 2 → PickMode::Face
    layout->addWidget(pickCombo);

    connect(pickCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        emit pickModeChanged(static_cast<PickMode>(idx));
    });

    return group;
}

// ════════════════════════════════════════════════════════════
// 模型文件加载
// ════════════════════════════════════════════════════════════

void FEModelPanel::loadModelFromFile() {
    QString path = QFileDialog::getOpenFileName(
        this, "打开有限元模型",
        QString(),
        "ABAQUS (*.inp *.odb);;ABAQUS Input (*.inp);;ABAQUS ODB (*.odb);;所有文件 (*)");

    if (path.isEmpty()) return;

    // ODB 是 ABAQUS 私有二进制格式，无法直接解析
    if (path.endsWith(".odb", Qt::CaseInsensitive)) {
        QMessageBox::information(this, "ODB 格式不支持",
            "ODB 是 ABAQUS 私有二进制格式（HKSRD），需要 ABAQUS 环境才能读取。\n\n"
            "请在 ABAQUS/CAE 中将模型导出为 .inp 格式：\n"
            "  File → Export → Model...\n\n"
            "或使用 abaqus python 脚本：\n"
            "  abaqus python odb2inp.py model.odb");
        return;
    }

    currentModel_.clear();
    currentRenderData_.clear();

    // ── 进度对话框 ──
    QProgressDialog progress("正在加载模型...", QString(), 0, 3, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setCancelButton(nullptr);
    progress.show();
    QApplication::processEvents();

    // 步骤 1：解析文件
    progress.setLabelText("正在解析文件...");
    progress.setValue(0);
    QApplication::processEvents();

    bool ok = false;
    if (path.endsWith(".inp", Qt::CaseInsensitive)) {
        ok = parseAbaqusInp(path);
    }

    if (!ok || currentModel_.isEmpty()) {
        progress.close();
        QMessageBox::warning(this, "加载失败",
            QString("无法解析模型文件或文件中无有效数据。\n\n"
                    "解析结果：节点 %1，单元 %2")
            .arg(currentModel_.nodeCount())
            .arg(currentModel_.elementCount()));
        return;
    }

    currentModel_.name = QFileInfo(path).baseName().toStdString();
    currentModel_.filePath = path.toStdString();

    // 步骤 2：生成渲染数据
    progress.setLabelText(QString("正在生成渲染数据（%1 个单元）...")
                          .arg(currentModel_.elementCount()));
    progress.setValue(1);
    QApplication::processEvents();

    currentRenderData_ = FEMeshConverter::toRenderData(currentModel_);

    // 步骤 3：更新显示
    progress.setLabelText("正在更新显示...");
    progress.setValue(2);
    QApplication::processEvents();

    updateInfoLabels();
    emit meshGenerated(currentRenderData_.mesh, currentModel_.computeCenter(), currentModel_.computeSize(), currentRenderData_.triangleToElement, currentRenderData_.triangleToFace);

    progress.setValue(3);
    qDebug("loadModelFromFile: loaded '%s' - nodes=%d, elements=%d, triangles=%d",
           qPrintable(path), currentModel_.nodeCount(), currentModel_.elementCount(),
           currentRenderData_.triangleCount());
}

bool FEModelPanel::parseAbaqusInp(const QString& filePath) {
    // ── 工具 lambda ──

    // ABAQUS 单元类型名 → ElementType
    auto mapElemType = [](const QString& abaqusType) -> ElementType {
        QString t = abaqusType.toUpper().trimmed();
        if (t.contains("C3D20"))  return ElementType::HEX20;
        if (t.contains("C3D8"))   return ElementType::HEX8;
        if (t.contains("C3D10"))  return ElementType::TET10;
        if (t.contains("C3D4"))   return ElementType::TET4;
        if (t.contains("C3D6"))   return ElementType::WEDGE6;
        if (t.contains("C3D5"))   return ElementType::PYRAMID5;
        if (t.contains("S3") || t.contains("CPS3") || t.contains("CPE3"))
            return ElementType::TRI3;
        if (t.contains("STRI65") || t.contains("CPS6") || t.contains("CPE6"))
            return ElementType::TRI6;
        if (t.contains("S8") || t.contains("CPS8") || t.contains("CPE8"))
            return ElementType::QUAD8;
        if (t.contains("S4") || t.contains("CPS4") || t.contains("CPE4"))
            return ElementType::QUAD4;
        if (t.contains("B31") || t.contains("T2D2") || t.contains("B21"))
            return ElementType::BAR2;
        return ElementType::HEX8;
    };

    auto nodeCountForType = [](ElementType t) -> int {
        switch (t) {
            case ElementType::BAR2:     return 2;
            case ElementType::BAR3:     return 3;
            case ElementType::TRI3:     return 3;
            case ElementType::TRI6:     return 6;
            case ElementType::QUAD4:    return 4;
            case ElementType::QUAD8:    return 8;
            case ElementType::TET4:     return 4;
            case ElementType::TET10:    return 10;
            case ElementType::HEX8:     return 8;
            case ElementType::HEX20:    return 20;
            case ElementType::WEDGE6:   return 6;
            case ElementType::PYRAMID5: return 5;
        }
        return 0;
    };

    // 将逗号和 tab 统一分割为 tokens
    auto splitLine = [](const QString& line) -> QStringList {
        QString s = line;
        s.replace('\t', ',');
        QStringList parts = s.split(',');
        QStringList result;
        for (auto& p : parts) {
            QString t = p.trimmed();
            if (!t.isEmpty()) result.append(t);
        }
        return result;
    };

    // ── 解析 INCLUDE 路径：尝试在主文件目录下查找 ──
    QDir baseDir = QFileInfo(filePath).absoluteDir();

    auto resolveInclude = [&](const QString& keyword) -> QString {
        // 提取 INPUT="..." 中的路径
        QRegExp rx("INPUT\\s*=\\s*\"([^\"]*)\"");
        if (rx.indexIn(keyword) < 0) return QString();

        QString rawPath = rx.cap(1);
        // 提取文件名（去掉 Windows 路径前缀）
        QString fileName = rawPath;
        int lastSlash = rawPath.lastIndexOf('/');
        int lastBackslash = rawPath.lastIndexOf('\\');
        int sep = qMax(lastSlash, lastBackslash);
        if (sep >= 0) fileName = rawPath.mid(sep + 1);

        // 尝试多个可能的位置
        QStringList candidates;
        candidates << baseDir.filePath(fileName);
        candidates << baseDir.filePath(rawPath);
        // 尝试在子目录中查找（从原始路径提取子目录）
        if (sep >= 0) {
            // 取最后一级子目录 + 文件名
            QString parent = rawPath.left(sep);
            int prevSep = qMax(parent.lastIndexOf('/'), parent.lastIndexOf('\\'));
            if (prevSep >= 0) {
                QString subDir = parent.mid(prevSep + 1);
                candidates << baseDir.filePath(subDir + "/" + fileName);
            }
        }

        for (const auto& c : candidates) {
            if (QFile::exists(c)) return c;
        }
        return QString();
    };

    // ── 递归读取文件，展开所有 INCLUDE 为行列表 ──
    std::function<QStringList(const QString&)> readWithIncludes;
    readWithIncludes = [&](const QString& path) -> QStringList {
        QFile f(path);
        QStringList lines;
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return lines;

        QTextStream stream(&f);
        while (!stream.atEnd()) {
            QString line = stream.readLine();
            QString trimmed = line.trimmed();

            // 处理 *INCLUDE
            if (trimmed.toUpper().startsWith("*INCLUDE")) {
                QString incPath = resolveInclude(trimmed);
                if (!incPath.isEmpty()) {
                    lines.append(readWithIncludes(incPath));
                }
            } else {
                lines.append(line);
            }
        }
        return lines;
    };

    // ── 展开所有行 ──
    QStringList allLines = readWithIncludes(filePath);

    // ── 解析 ──
    enum Section { None, Node, Element } section = None;
    ElementType currentElemType = ElementType::HEX8;
    int expectedNodeCount = 8;
    int pendingElemId = -1;
    std::vector<int> pendingNodeIds;

    auto flushPendingElement = [&]() {
        if (pendingElemId >= 0 && !pendingNodeIds.empty()) {
            currentModel_.addElement(pendingElemId, currentElemType, pendingNodeIds);
            pendingElemId = -1;
            pendingNodeIds.clear();
        }
    };

    for (const auto& rawLine : allLines) {
        QString line = rawLine.trimmed();

        if (line.isEmpty()) continue;
        if (line.startsWith("**")) continue;

        // 关键字行
        if (line.startsWith("*")) {
            flushPendingElement();
            QString upper = line.toUpper();

            if (upper.startsWith("*NODE") && !upper.startsWith("*NSET")
                && !upper.contains("OUTPUT")) {
                section = Node;
            } else if (upper.startsWith("*ELEMENT") && upper.contains("TYPE")
                       && !upper.contains("OUTPUT")) {
                section = Element;
                QRegExp rx("TYPE\\s*=\\s*([A-Za-z0-9]+)");
                rx.setCaseSensitivity(Qt::CaseInsensitive);
                if (rx.indexIn(line) >= 0) {
                    currentElemType = mapElemType(rx.cap(1));
                }
                expectedNodeCount = nodeCountForType(currentElemType);
            } else {
                section = None;
            }
            continue;
        }

        // 数据行
        if (section == Node) {
            QStringList parts = splitLine(line);
            if (parts.size() >= 4) {
                int id = parts[0].toInt();
                float x = parts[1].toFloat();
                float y = parts[2].toFloat();
                float z = parts[3].toFloat();
                currentModel_.addNode(id, glm::vec3(x, y, z));
            }
        } else if (section == Element) {
            QStringList parts = splitLine(line);
            if (parts.isEmpty()) continue;

            if (pendingElemId < 0) {
                // 新单元行：第一个值是单元 ID，后面是节点 ID
                pendingElemId = parts[0].toInt();
                for (int i = 1; i < parts.size(); ++i)
                    pendingNodeIds.push_back(parts[i].toInt());
            } else {
                // 续行
                for (int i = 0; i < parts.size(); ++i)
                    pendingNodeIds.push_back(parts[i].toInt());
            }

            if (static_cast<int>(pendingNodeIds.size()) >= expectedNodeCount)
                flushPendingElement();
        }
    }

    flushPendingElement();

    qDebug("parseAbaqusInp: nodes=%d, elements=%d",
           (int)currentModel_.nodes.size(),
           (int)currentModel_.elements.size());

    // 找出缺失的单元 ID
    if ((int)currentModel_.elements.size() < 800) {
        QString missing;
        for (int i = 1; i <= 800; ++i) {
            if (currentModel_.elements.find(i) == currentModel_.elements.end()) {
                if (!missing.isEmpty()) missing += ", ";
                missing += QString::number(i);
            }
        }
        qDebug("  missing element IDs: %s", qPrintable(missing));
    }

    // 检查单元类型分布
    int hex8 = 0, wedge6 = 0, other = 0;
    for (const auto& [id, elem] : currentModel_.elements) {
        if (elem.type == ElementType::HEX8) hex8++;
        else if (elem.type == ElementType::WEDGE6) wedge6++;
        else other++;
    }
    qDebug("  HEX8=%d, WEDGE6=%d, other=%d", hex8, wedge6, other);

    return true;
}

// ════════════════════════════════════════════════════════════
// 测试模型生成
// ════════════════════════════════════════════════════════════

/**
 * @brief 生成悬臂梁测试模型
 *
 * 创建一个 8×2×2 的六面体（HEX8）网格，模拟悬臂梁。
 * 梁的尺寸：长 4.0 × 宽 1.0 × 高 1.0
 *
 * 节点编号规则：
 *   nodeId = iz * (ny+1) * (nx+1) + iy * (nx+1) + ix + 1
 *   （+1 使 ID 从 1 开始，符合 FEM 软件惯例）
 *
 * 单元编号规则：
 *   elemId = ez * ny * nx + ey * nx + ex + 1
 *
 * 六面体节点连接（标准约定）：
 *        7 ─── 6
 *       /|    /|
 *      4 ─── 5 |
 *      | 3 ──| 2
 *      |/    |/
 *      0 ─── 1
 */
void FEModelPanel::loadDefaultModel() {
    generateBeamModel();
}

void FEModelPanel::generateBeamModel() {
    currentModel_.clear();
    currentModel_.name = "Cantilever Beam";

    // 网格参数
    const int nx = 8, ny = 2, nz = 2;        // 各方向单元数
    const float lx = 4.0f, ly = 1.0f, lz = 1.0f;  // 各方向尺寸
    const float dx = lx / nx, dy = ly / ny, dz = lz / nz;

    // ── 生成节点 ──
    // (nx+1) × (ny+1) × (nz+1) 个节点
    for (int iz = 0; iz <= nz; ++iz) {
        for (int iy = 0; iy <= ny; ++iy) {
            for (int ix = 0; ix <= nx; ++ix) {
                int nodeId = iz * (ny + 1) * (nx + 1) + iy * (nx + 1) + ix + 1;
                glm::vec3 coords(
                    ix * dx - lx * 0.5f,  // 居中：x ∈ [-2, 2]
                    iy * dy - ly * 0.5f,  // 居中：y ∈ [-0.5, 0.5]
                    iz * dz - lz * 0.5f   // 居中：z ∈ [-0.5, 0.5]
                );
                currentModel_.addNode(nodeId, coords);
            }
        }
    }

    // ── 生成 HEX8 单元 ──
    // nx × ny × nz 个六面体单元
    for (int ez = 0; ez < nz; ++ez) {
        for (int ey = 0; ey < ny; ++ey) {
            for (int ex = 0; ex < nx; ++ex) {
                int elemId = ez * ny * nx + ey * nx + ex + 1;

                // 计算单元 8 个角节点的 ID
                // 底面 4 个节点（z = ez）
                int n0 = ez * (ny + 1) * (nx + 1) + ey * (nx + 1) + ex + 1;
                int n1 = n0 + 1;
                int n2 = n0 + (nx + 1) + 1;
                int n3 = n0 + (nx + 1);

                // 顶面 4 个节点（z = ez+1）
                int n4 = n0 + (ny + 1) * (nx + 1);
                int n5 = n1 + (ny + 1) * (nx + 1);
                int n6 = n2 + (ny + 1) * (nx + 1);
                int n7 = n3 + (ny + 1) * (nx + 1);

                currentModel_.addElement(elemId, ElementType::HEX8,
                                          {n0, n1, n2, n3, n4, n5, n6, n7});
            }
        }
    }

    // ── 转换为渲染数据 ──
    currentRenderData_ = FEMeshConverter::toRenderData(currentModel_);
    updateInfoLabels();
    emit meshGenerated(currentRenderData_.mesh, currentModel_.computeCenter(), currentModel_.computeSize(), currentRenderData_.triangleToElement, currentRenderData_.triangleToFace);
}

/**
 * @brief 生成平板测试模型
 *
 * 创建一个 8×8 的四边形（QUAD4）网格，模拟平板。
 * 板的尺寸：2.0 × 2.0，位于 XY 平面（z = 0）。
 *
 * 节点编号：iy * (nx+1) + ix + 1
 * 单元编号：ey * nx + ex + 1
 */
void FEModelPanel::generatePlateModel() {
    currentModel_.clear();
    currentModel_.name = "Flat Plate";

    const int nx = 8, ny = 8;
    const float lx = 2.0f, ly = 2.0f;
    const float dx = lx / nx, dy = ly / ny;

    // ── 生成节点 ──
    for (int iy = 0; iy <= ny; ++iy) {
        for (int ix = 0; ix <= nx; ++ix) {
            int nodeId = iy * (nx + 1) + ix + 1;
            glm::vec3 coords(
                ix * dx - lx * 0.5f,  // x ∈ [-1, 1]
                iy * dy - ly * 0.5f,  // y ∈ [-1, 1]
                0.0f                   // z = 0（平板）
            );
            currentModel_.addNode(nodeId, coords);
        }
    }

    // ── 生成 QUAD4 单元 ──
    for (int ey = 0; ey < ny; ++ey) {
        for (int ex = 0; ex < nx; ++ex) {
            int elemId = ey * nx + ex + 1;

            // 四边形 4 个节点（逆时针顺序）
            int n0 = ey * (nx + 1) + ex + 1;
            int n1 = n0 + 1;
            int n2 = n0 + (nx + 1) + 1;
            int n3 = n0 + (nx + 1);

            currentModel_.addElement(elemId, ElementType::QUAD4,
                                      {n0, n1, n2, n3});
        }
    }

    // ── 转换为渲染数据 ──
    currentRenderData_ = FEMeshConverter::toRenderData(currentModel_);
    updateInfoLabels();
    emit meshGenerated(currentRenderData_.mesh, currentModel_.computeCenter(), currentModel_.computeSize(), currentRenderData_.triangleToElement, currentRenderData_.triangleToFace);
}

/**
 * @brief 生成混合单元测试模型
 *
 * 在平面上创建三角形和四边形混合网格：
 *   - 左半部分使用 QUAD4 单元
 *   - 右半部分将 QUAD4 分割为两个 TRI3 单元
 *
 * 用于验证不同单元类型的混合处理能力。
 */
void FEModelPanel::generateMixedModel() {
    currentModel_.clear();
    currentModel_.name = "Mixed Mesh";

    const int nx = 8, ny = 6;
    const float lx = 2.0f, ly = 1.5f;
    const float dx = lx / nx, dy = ly / ny;
    const int halfX = nx / 2;  // 左半部分为 QUAD，右半部分为 TRI

    // ── 生成节点 ──
    for (int iy = 0; iy <= ny; ++iy) {
        for (int ix = 0; ix <= nx; ++ix) {
            int nodeId = iy * (nx + 1) + ix + 1;
            glm::vec3 coords(
                ix * dx - lx * 0.5f,
                iy * dy - ly * 0.5f,
                0.0f
            );
            currentModel_.addNode(nodeId, coords);
        }
    }

    // ── 生成单元 ──
    int elemId = 1;
    for (int ey = 0; ey < ny; ++ey) {
        for (int ex = 0; ex < nx; ++ex) {
            int n0 = ey * (nx + 1) + ex + 1;
            int n1 = n0 + 1;
            int n2 = n0 + (nx + 1) + 1;
            int n3 = n0 + (nx + 1);

            if (ex < halfX) {
                // 左半部分：QUAD4 单元
                currentModel_.addElement(elemId++, ElementType::QUAD4,
                                          {n0, n1, n2, n3});
            } else {
                // 右半部分：将四边形分割为两个 TRI3 单元
                currentModel_.addElement(elemId++, ElementType::TRI3,
                                          {n0, n1, n2});
                currentModel_.addElement(elemId++, ElementType::TRI3,
                                          {n0, n2, n3});
            }
        }
    }

    // ── 转换为渲染数据 ──
    currentRenderData_ = FEMeshConverter::toRenderData(currentModel_);
    updateInfoLabels();
    emit meshGenerated(currentRenderData_.mesh, currentModel_.computeCenter(), currentModel_.computeSize(), currentRenderData_.triangleToElement, currentRenderData_.triangleToFace);
}

// ════════════════════════════════════════════════════════════
// 信息更新
// ════════════════════════════════════════════════════════════

/**
 * @brief 更新模型信息标签
 *
 * 从 currentModel_ 和 currentRenderData_ 中提取统计数据，
 * 显示在信息面板的各个标签上。
 */
void FEModelPanel::updateInfoLabels() {
    nodeCountLabel_->setText(
        QString("节点数: %1").arg(currentModel_.nodeCount()));
    elementCountLabel_->setText(
        QString("单元数: %1").arg(currentModel_.elementCount()));
    triangleCountLabel_->setText(
        QString("三角面数: %1").arg(currentRenderData_.triangleCount()));

    if (currentModel_.isEmpty()) {
        modelSizeLabel_->setText("尺寸: -");
    } else {
        float size = currentModel_.computeSize();
        modelSizeLabel_->setText(
            QString("尺寸: %1").arg(size, 0, 'f', 2));
    }
}
