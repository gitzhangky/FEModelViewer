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
#include <QSettings>
#include <QDir>
#include <QRegExp>
#include <QProgressDialog>
#include <QProgressBar>
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QEventLoop>
#include <functional>
#include <atomic>

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
    QSettings settings("FEModelViewer", "FEModelViewer");
    QString lastDir = settings.value("lastOpenDir", QString()).toString();
    // 如果上次目录无效，使用桌面作为起始位置（加载快）
    if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
        lastDir = QDir::homePath() + "/Desktop";
        if (!QDir(lastDir).exists()) lastDir = QDir::homePath();
    }

    // macOS 16 + Qt5 原生 NSOpenPanel 不弹窗，使用 Qt 自带对话框
    // 不设自定义样式，保持系统默认外观
    QFileDialog dialog(this, "打开有限元模型", lastDir,
                       "所有支持格式 (*.inp *.bdf *.fem *.odb);;"
                       "ABAQUS Input (*.inp);;"
                       "Nastran BDF (*.bdf *.fem);;"
                       "ABAQUS ODB (*.odb);;"
                       "所有文件 (*)");
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setStyleSheet("");  // 清除继承的父级样式，使用系统默认
    dialog.resize(720, 480);

    if (dialog.exec() != QDialog::Accepted) return;
    QString path = dialog.selectedFiles().first();

    if (path.isEmpty()) return;
    settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());

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
    QProgressDialog dlg("正在加载模型...", QString(), 0, 1000, this);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(0);
    dlg.setCancelButton(nullptr);
    dlg.setStyleSheet(
        "QProgressDialog { background: #1e1e2e; }"
        "QLabel { color: #cdd6f4; font-size: 13px; }"
        "QProgressBar {"
        "  border: 1px solid #45475a; border-radius: 5px;"
        "  background: #313244; text-align: center;"
        "  color: #cdd6f4; font-size: 11px; height: 18px; }"
        "QProgressBar::chunk {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #a6e3a1, stop:1 #94e2d5);"
        "  border-radius: 4px; }"
    );
    dlg.setValue(0);
    dlg.show();

    // ── 后台线程做所有重计算，原子变量传进度 ──
    std::atomic<int>  targetVal{0};    // 目标进度 0-1000
    std::atomic<int>  phase{0};        // 0=解析 1=渲染 2=完成
    std::atomic<int>  elemCount{0};    // 单元数（供标签显示）
    bool workerOk = false;
    FEModel           resultModel;
    FERenderData      resultRender;

    QThread* worker = QThread::create([&]() {
        // 阶段 1：解析文件 (0-500)
        phase.store(0);
        bool ok = false;
        if (path.endsWith(".inp", Qt::CaseInsensitive)) {
            ok = parseAbaqusInp(path, resultModel, [&](int pct) {
                targetVal.store(pct * 5);
            });
        } else if (path.endsWith(".bdf", Qt::CaseInsensitive) ||
                   path.endsWith(".fem", Qt::CaseInsensitive)) {
            ok = parseNastranBdf(path, resultModel, [&](int pct) {
                targetVal.store(pct * 5);
            });
        }
        workerOk = ok;
        if (!ok || resultModel.isEmpty()) {
            phase.store(2);
            return;
        }
        resultModel.name = QFileInfo(path).baseName().toStdString();
        resultModel.filePath = path.toStdString();
        elemCount.store(resultModel.elementCount());
        targetVal.store(500);

        // 阶段 2：生成渲染数据 (500-950)
        phase.store(1);
        resultRender = FEMeshConverter::toRenderData(resultModel, [&](int pct) {
            targetVal.store(500 + pct * 450 / 100);
        });
        targetVal.store(950);
        phase.store(2);
    });

    // ── 主线程：轮询驱动进度条（兼容 macOS Cocoa） ──
    worker->start();
    int displayed = 0;
    while (!worker->isFinished()) {
        int target = targetVal.load();

        // ease-out 插值
        if (displayed < target) {
            int diff = target - displayed;
            int step = qMax(1, diff / 4);
            displayed = qMin(displayed + step, target);
        }
        dlg.setValue(displayed);

        // 阶段文字
        int p = phase.load();
        if (p == 0) {
            int pct = targetVal.load() / 5;
            dlg.setLabelText(pct < 50 ? "正在解析节点数据..." : "正在解析单元数据...");
        } else if (p == 1) {
            dlg.setLabelText(QString("正在生成渲染数据（%1 个单元）...").arg(elemCount.load()));
        } else {
            dlg.setLabelText("正在更新显示...");
        }

        // 直接驱动事件循环，避免嵌套 QEventLoop 与 Cocoa 冲突
        QApplication::processEvents(QEventLoop::AllEvents);
        worker->wait(16);  // 等待最多 16ms（~60fps），不阻塞主线程
    }
    worker->wait();
    delete worker;

    // ── 检查结果 ──
    if (!workerOk || resultModel.isEmpty()) {
        dlg.close();
        QMessageBox::warning(this, "加载失败",
            QString("无法解析模型文件或文件中无有效数据。\n\n"
                    "解析结果：节点 %1，单元 %2")
            .arg(resultModel.nodeCount())
            .arg(resultModel.elementCount()));
        return;
    }

    currentModel_ = resultModel;
    currentRenderData_ = resultRender;

    // ── 收尾动画 ──
    dlg.setLabelText("正在更新显示...");
    while (displayed < 1000) {
        int diff = 1000 - displayed;
        displayed = qMin(displayed + qMax(1, diff / 3), 1000);
        dlg.setValue(displayed);
        QApplication::processEvents();
        QThread::msleep(16);
    }

    updateInfoLabels();
    emit meshGenerated(currentRenderData_.mesh, currentModel_.computeCenter(),
                       currentModel_.computeSize(), currentRenderData_.triangleToElement,
                       currentRenderData_.triangleToFace);

    dlg.close();
    qDebug("loadModelFromFile: loaded '%s' - nodes=%d, elements=%d, triangles=%d",
           qPrintable(path), currentModel_.nodeCount(), currentModel_.elementCount(),
           currentRenderData_.triangleCount());
}

bool FEModelPanel::parseAbaqusInp(const QString& filePath, FEModel& model, const std::function<void(int)>& progress) {
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

    // ── 递归读取文件，展开所有 INCLUDE 为行列表（0-40%）──
    const qint64 mainFileSize = std::max((qint64)1, QFileInfo(filePath).size());
    qint64 bytesRead = 0;

    std::function<QStringList(const QString&)> readWithIncludes;
    readWithIncludes = [&](const QString& path) -> QStringList {
        QFile f(path);
        QStringList lines;
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return lines;

        QTextStream stream(&f);
        while (!stream.atEnd()) {
            QString line = stream.readLine();
            bytesRead += line.size() + 1;
            if (progress) {
                int pct = static_cast<int>(std::min(bytesRead * 40 / mainFileSize, (qint64)40));
                progress(pct);
            }

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
    if (progress) progress(40);

    // ── 解析（40-100%）──
    enum Section { None, Node, Element } section = None;
    ElementType currentElemType = ElementType::HEX8;
    int expectedNodeCount = 8;
    int pendingElemId = -1;
    std::vector<int> pendingNodeIds;

    auto flushPendingElement = [&]() {
        if (pendingElemId >= 0 && !pendingNodeIds.empty()) {
            model.addElement(pendingElemId, currentElemType, pendingNodeIds);
            pendingElemId = -1;
            pendingNodeIds.clear();
        }
    };

    const int totalLines = allLines.size();
    const int lineReportInterval = std::max(1, totalLines / 100);
    int lineIndex = 0;

    for (const auto& rawLine : allLines) {
        if (progress && (++lineIndex % lineReportInterval == 0)) {
            progress(40 + lineIndex * 60 / totalLines);
        }
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
                model.addNode(id, glm::vec3(x, y, z));
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
           (int)model.nodes.size(),
           (int)model.elements.size());

    // 找出缺失的单元 ID
    if ((int)model.elements.size() < 800) {
        QString missing;
        for (int i = 1; i <= 800; ++i) {
            if (model.elements.find(i) == model.elements.end()) {
                if (!missing.isEmpty()) missing += ", ";
                missing += QString::number(i);
            }
        }
        qDebug("  missing element IDs: %s", qPrintable(missing));
    }

    // 检查单元类型分布
    int hex8 = 0, wedge6 = 0, other = 0;
    for (const auto& [id, elem] : model.elements) {
        if (elem.type == ElementType::HEX8) hex8++;
        else if (elem.type == ElementType::WEDGE6) wedge6++;
        else other++;
    }
    qDebug("  HEX8=%d, WEDGE6=%d, other=%d", hex8, wedge6, other);

    return true;
}

// ════════════════════════════════════════════════════════════
// Nastran BDF/FEM 解析
// ════════════════════════════════════════════════════════════

bool FEModelPanel::parseNastranBdf(const QString& filePath, FEModel& model, const std::function<void(int)>& progress) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    // 读取全部行（0-15%）
    const qint64 fileSize = std::max((qint64)1, QFileInfo(filePath).size());
    qint64 bytesRead = 0;
    QStringList allLines;
    {
        QTextStream stream(&f);
        while (!stream.atEnd()) {
            QString line = stream.readLine();
            bytesRead += line.size() + 1;
            if (progress) {
                int pct = static_cast<int>(std::min(bytesRead * 15 / fileSize, (qint64)15));
                progress(pct);
            }
            allLines.append(line);
        }
    }
    f.close();
    if (progress) progress(15);

    const int totalLines = allLines.size();
    const int lineReportInterval = std::max(1, totalLines / 100);

    // ── Nastran 字段解析工具 ──

    // 解析 Nastran 实数（支持隐式指数：如 "1.5-3" → "1.5e-3"）
    auto parseNastranReal = [](const QString& s) -> double {
        QString t = s.trimmed();
        if (t.isEmpty()) return 0.0;
        // 处理隐式指数：数字中间的 +/- 是指数符号
        // 例如 "1.5-3" → "1.5e-3", "2.0+1" → "2.0e+1"
        // 但不处理开头的 +/- 号
        for (int i = 1; i < t.size(); ++i) {
            QChar c = t[i];
            if ((c == '+' || c == '-') && t[i-1] != 'e' && t[i-1] != 'E'
                && t[i-1] != 'd' && t[i-1] != 'D'
                && t[i-1] != '+' && t[i-1] != '-') {
                // 前一个字符是数字或小数点 → 隐式指数
                if (t[i-1].isDigit() || t[i-1] == '.') {
                    t.insert(i, 'e');
                    break;
                }
            }
        }
        t.replace(QChar('d'), QChar('e'), Qt::CaseInsensitive);
        return t.toDouble();
    };

    // 将一行拆分为字段（自动检测自由格式/小字段/大字段）
    auto splitBdfFields = [](const QString& line) -> QStringList {
        QStringList fields;
        if (line.contains(',')) {
            // 自由格式：逗号分隔
            for (const auto& f : line.split(','))
                fields.append(f.trimmed());
        } else {
            // 固定格式：第一个字段 8 字符，后续每 8 或 16 字符
            QString first = line.left(8).trimmed();
            fields.append(first);
            bool largeField = first.endsWith('*');
            int fieldWidth = largeField ? 16 : 8;
            int pos = 8;
            while (pos < line.size()) {
                int end = qMin(pos + fieldWidth, line.size());
                fields.append(line.mid(pos, end - pos).trimmed());
                pos = end;
            }
        }
        return fields;
    };

    // ── 合并续行（15-25%）──
    // Nastran 固定格式：cols 1-8=卡名, cols 9-72=数据, cols 73-80=续行标记
    // 续行行：cols 1-8=续行标记(+或*), cols 9-72=数据, cols 73-80=下一续行标记
    QStringList mergedLines;
    {
        QString current;
        bool currentIsFreeFormat = false;
        const int mergeTotal = allLines.size();
        const int mergeReportInterval = std::max(1, mergeTotal / 10);
        int mergeIndex = 0;

        for (const auto& rawLine : allLines) {
            if (progress && (++mergeIndex % mergeReportInterval == 0)) {
                progress(15 + mergeIndex * 10 / mergeTotal);  // 15-25%
            }
            QString line = rawLine;
            // 去除行尾注释（$ 后的内容）
            int dollarPos = line.indexOf('$');
            if (dollarPos >= 0) line = line.left(dollarPos);
            if (line.trimmed().isEmpty()) continue;

            // 检查是否是续行（以 +, * 开头）
            QChar firstChar = line.trimmed().at(0);
            bool isContinuation = (firstChar == '+' || firstChar == '*')
                                  && !current.isEmpty();

            if (isContinuation) {
                if (currentIsFreeFormat) {
                    // 自由格式续行
                    QString continuation = line.trimmed();
                    if (continuation.startsWith('+') || continuation.startsWith('*')) {
                        int commaPos = continuation.indexOf(',');
                        if (commaPos >= 0) continuation = continuation.mid(commaPos + 1);
                        else continuation = continuation.mid(8);
                    }
                    current += "," + continuation;
                } else {
                    // 固定格式续行：只取 cols 9-72 的数据部分
                    int dataEnd = qMin(72, line.size());
                    QString contData = (dataEnd > 8) ? line.mid(8, dataEnd - 8) : "";
                    current += contData;
                }
            } else {
                // 新卡：保存上一条
                if (!current.isEmpty()) mergedLines.append(current);
                currentIsFreeFormat = line.contains(',');
                if (!currentIsFreeFormat) {
                    // 固定格式：只取 cols 1-72（去掉续行标记字段）
                    current = line.left(qMin(72, line.size()));
                } else {
                    current = line;
                }
            }
        }
        if (!current.isEmpty()) mergedLines.append(current);
    }
    if (progress) progress(25);

    // ── CORD2R 坐标系数据结构 ──
    struct CoordSys {
        int id = 0;
        int rid = 0;       // 参考坐标系 ID（0 = 全局）
        glm::dvec3 A{0};   // 原点
        glm::dvec3 B{0};   // Z 轴方向点
        glm::dvec3 C{0};   // XZ 平面内的点
        glm::dmat3 R{1};   // 旋转矩阵（解析后计算）
        glm::dvec3 origin{0}; // 全局原点（解析后计算）
        bool resolved = false;
    };
    std::unordered_map<int, CoordSys> coordSystems;

    // 临时存储 GRID 的原始数据（坐标系变换需要先解析所有 CORD2R）
    struct RawGrid {
        int id;
        int cp;        // 坐标系 ID
        glm::dvec3 xyz;
    };
    std::vector<RawGrid> rawGrids;

    // ── 第一遍：解析 CORD2R 和 GRID，收集单元（25-80%）──
    int lineIndex = 0;
    const int mergedTotal2 = mergedLines.size();
    const int mergedReportInterval = std::max(1, mergedTotal2 / 100);

    for (const auto& line : mergedLines) {
        if (progress && (++lineIndex % mergedReportInterval == 0)) {
            progress(25 + lineIndex * 55 / mergedTotal2);  // 25-80%
        }

        QStringList fields = splitBdfFields(line);
        if (fields.isEmpty()) continue;

        QString card = fields[0].toUpper().remove('*');

        // ── CORD2R: 矩形坐标系 ──
        if (card == "CORD2R") {
            if (fields.size() < 6) continue;
            CoordSys cs;
            cs.id  = fields[1].toInt();
            cs.rid = fields[2].toInt();
            cs.A.x = parseNastranReal(fields[3]);
            cs.A.y = parseNastranReal(fields[4]);
            cs.A.z = (fields.size() > 5)  ? parseNastranReal(fields[5])  : 0.0;
            cs.B.x = (fields.size() > 6)  ? parseNastranReal(fields[6])  : 0.0;
            cs.B.y = (fields.size() > 7)  ? parseNastranReal(fields[7])  : 0.0;
            cs.B.z = (fields.size() > 8)  ? parseNastranReal(fields[8])  : 0.0;
            cs.C.x = (fields.size() > 9)  ? parseNastranReal(fields[9])  : 0.0;
            cs.C.y = (fields.size() > 10) ? parseNastranReal(fields[10]) : 0.0;
            cs.C.z = (fields.size() > 11) ? parseNastranReal(fields[11]) : 0.0;
            coordSystems[cs.id] = cs;
        }
        // ── GRID: 节点（暂存，后续变换） ──
        else if (card == "GRID") {
            if (fields.size() < 5) continue;
            RawGrid g;
            g.id = fields[1].toInt();
            g.cp = fields[2].toInt();
            g.xyz.x = parseNastranReal(fields[3]);
            g.xyz.y = parseNastranReal(fields[4]);
            g.xyz.z = (fields.size() > 5) ? parseNastranReal(fields[5]) : 0.0;
            rawGrids.push_back(g);
        }
        // ── 单元卡 ──
        else if (card == "CTETRA") {
            if (fields.size() < 6) continue;
            int eid = fields[1].toInt();
            std::vector<int> nids;
            for (int i = 3; i < fields.size() && !fields[i].isEmpty(); ++i)
                nids.push_back(fields[i].toInt());
            if (nids.size() >= 10)
                model.addElement(eid, ElementType::TET10, std::vector<int>(nids.begin(), nids.begin() + 10));
            else if (nids.size() >= 4)
                model.addElement(eid, ElementType::TET4, std::vector<int>(nids.begin(), nids.begin() + 4));
        }
        else if (card == "CHEXA") {
            if (fields.size() < 6) continue;
            int eid = fields[1].toInt();
            std::vector<int> nids;
            for (int i = 3; i < fields.size() && !fields[i].isEmpty(); ++i)
                nids.push_back(fields[i].toInt());
            if (nids.size() >= 20)
                model.addElement(eid, ElementType::HEX20, std::vector<int>(nids.begin(), nids.begin() + 20));
            else if (nids.size() >= 8)
                model.addElement(eid, ElementType::HEX8, std::vector<int>(nids.begin(), nids.begin() + 8));
        }
        else if (card == "CPENTA") {
            if (fields.size() < 6) continue;
            int eid = fields[1].toInt();
            std::vector<int> nids;
            for (int i = 3; i < fields.size() && !fields[i].isEmpty(); ++i)
                nids.push_back(fields[i].toInt());
            if (nids.size() >= 6)
                model.addElement(eid, ElementType::WEDGE6, std::vector<int>(nids.begin(), nids.begin() + 6));
        }
        else if (card == "CPYRAM") {
            if (fields.size() < 6) continue;
            int eid = fields[1].toInt();
            std::vector<int> nids;
            for (int i = 3; i < fields.size() && !fields[i].isEmpty(); ++i)
                nids.push_back(fields[i].toInt());
            if (nids.size() >= 5)
                model.addElement(eid, ElementType::PYRAMID5, std::vector<int>(nids.begin(), nids.begin() + 5));
        }
        else if (card == "CTRIA3") {
            if (fields.size() < 6) continue;
            int eid = fields[1].toInt();
            model.addElement(eid, ElementType::TRI3,
                {fields[3].toInt(), fields[4].toInt(), fields[5].toInt()});
        }
        else if (card == "CTRIA6") {
            if (fields.size() < 9) continue;
            int eid = fields[1].toInt();
            std::vector<int> nids;
            for (int i = 3; i < 9 && i < fields.size(); ++i)
                nids.push_back(fields[i].toInt());
            model.addElement(eid, ElementType::TRI6, nids);
        }
        else if (card == "CQUAD4") {
            if (fields.size() < 7) continue;
            int eid = fields[1].toInt();
            model.addElement(eid, ElementType::QUAD4,
                {fields[3].toInt(), fields[4].toInt(), fields[5].toInt(), fields[6].toInt()});
        }
        else if (card == "CQUAD8") {
            if (fields.size() < 11) continue;
            int eid = fields[1].toInt();
            std::vector<int> nids;
            for (int i = 3; i < 11 && i < fields.size(); ++i)
                nids.push_back(fields[i].toInt());
            model.addElement(eid, ElementType::QUAD8, nids);
        }
        else if (card == "CBAR" || card == "CBEAM") {
            if (fields.size() < 5) continue;
            int eid = fields[1].toInt();
            model.addElement(eid, ElementType::BAR2,
                {fields[3].toInt(), fields[4].toInt()});
        }
    }

    // ── 解析 CORD2R 坐标系变换矩阵（支持链式引用） ──
    std::function<void(CoordSys&)> resolveCoordSys;
    resolveCoordSys = [&](CoordSys& cs) {
        if (cs.resolved) return;
        cs.resolved = true;  // 防止循环引用

        // 如果引用了其他坐标系，先解析引用的坐标系
        // 然后将 A, B, C 从引用坐标系变换到全局
        if (cs.rid != 0) {
            auto it = coordSystems.find(cs.rid);
            if (it != coordSystems.end()) {
                resolveCoordSys(it->second);
                CoordSys& ref = it->second;
                cs.A = ref.origin + ref.R * cs.A;
                cs.B = ref.origin + ref.R * cs.B;
                cs.C = ref.origin + ref.R * cs.C;
            }
        }

        // 计算旋转矩阵：Z = normalize(B-A), X = 投影(C-A)到垂直于Z的平面
        glm::dvec3 zDir = glm::normalize(cs.B - cs.A);
        glm::dvec3 xzVec = cs.C - cs.A;
        glm::dvec3 yDir = glm::normalize(glm::cross(zDir, xzVec));
        glm::dvec3 xDir = glm::cross(yDir, zDir);

        // 旋转矩阵的列向量是局部坐标系的轴方向
        cs.R = glm::dmat3(xDir, yDir, zDir);
        cs.origin = cs.A;
    };

    for (auto& [id, cs] : coordSystems) {
        resolveCoordSys(cs);
    }

    qDebug("parseNastranBdf: %d coordinate systems resolved", (int)coordSystems.size());

    // ── 将 GRID 节点从局部坐标系变换到全局坐标系 ──（80-95%）
    int gridIndex = 0;
    const int gridTotal = static_cast<int>(rawGrids.size());
    const int gridReportInterval = std::max(1, gridTotal / 15);

    for (const auto& g : rawGrids) {
        if (progress && (++gridIndex % gridReportInterval == 0)) {
            progress(80 + gridIndex * 15 / gridTotal);
        }

        glm::dvec3 globalPos = g.xyz;

        if (g.cp != 0) {
            auto it = coordSystems.find(g.cp);
            if (it != coordSystems.end()) {
                const CoordSys& cs = it->second;
                globalPos = cs.origin + cs.R * g.xyz;
            }
        }

        model.addNode(g.id, glm::vec3(
            static_cast<float>(globalPos.x),
            static_cast<float>(globalPos.y),
            static_cast<float>(globalPos.z)));
    }
    if (progress) progress(95);

    qDebug("parseNastranBdf: nodes=%d, elements=%d, coordSystems=%d",
           (int)model.nodes.size(),
           (int)model.elements.size(),
           (int)coordSystems.size());

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
