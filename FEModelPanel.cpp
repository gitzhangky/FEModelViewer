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
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QEventLoop>
#include <functional>
#include <atomic>
#include <map>
#include <set>
#include <unordered_set>

// ════════════════════════════════════════════════════════════
// 构造函数
// ════════════════════════════════════════════════════════════

FEModelPanel::FEModelPanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(140);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    layout->addWidget(createInfoGroup());
    layout->addWidget(createSelectionGroup());
    layout->addStretch();

    // ── 统一样式表（Catppuccin Mocha 配色）──
    setStyleSheet(
        "QWidget { background: #1e1e2e; color: #cdd6f4; }"

        "QGroupBox {"
        "  background: #181825; border: 1px solid #313244;"
        "  border-radius: 6px; margin-top: 12px; padding: 16px 8px 8px 8px;"
        "  font-weight: bold; font-size: 12px; color: #a6adc8; }"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 10px; padding: 0 4px;"
        "  color: #a6e3a1; }"

        "QLabel { font-size: 12px; color: #bac2de; }"
    );
}

// ════════════════════════════════════════════════════════════
// UI 分组创建
// ════════════════════════════════════════════════════════════

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

// ════════════════════════════════════════════════════════════
// 模型文件加载
// ════════════════════════════════════════════════════════════

void FEModelPanel::loadModelFromFile() {
    QSettings settings("FEModelViewer", "FEModelViewer");
    QString lastDir = settings.value("lastOpenDir", QString()).toString();
    if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
        lastDir = QDir::homePath() + "/Desktop";
        if (!QDir(lastDir).exists()) lastDir = QDir::homePath();
    }

    QFileDialog dialog(this, "打开有限元模型", lastDir,
                       "所有支持格式 (*.inp *.bdf *.fem *.op2 *.odb);;"
                       "ABAQUS Input (*.inp);;"
                       "Nastran BDF (*.bdf *.fem);;"
                       "Nastran OP2 (*.op2);;"
                       "ABAQUS ODB (*.odb);;"
                       "所有文件 (*)");
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    if (dialog.exec() != QDialog::Accepted) return;
    QString path = dialog.selectedFiles().first();
    if (path.isEmpty()) return;

    settings.setValue("lastOpenDir", QFileInfo(path).absolutePath());
    loadModelFromPath(path);
}

void FEModelPanel::loadModelFromPath(const QString& path) {
    if (path.isEmpty()) return;

    if (path.endsWith(".odb", Qt::CaseInsensitive)) {
        emit loadFinished(false, "ODB 是 ABAQUS 私有二进制格式，需要导出为 .inp 格式");
        return;
    }

    currentModel_.clear();
    currentRenderData_.clear();

    emit loadProgress(0, "正在解析节点数据...");

    // ── 后台线程做所有重计算，原子变量传进度 ──
    std::atomic<int>  targetVal{0};
    std::atomic<int>  phase{0};
    std::atomic<int>  elemCount{0};
    bool workerOk = false;
    FEModel           resultModel;
    FERenderData      resultRender;

    QThread* worker = QThread::create([&]() {
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
        } else if (path.endsWith(".op2", Qt::CaseInsensitive)) {
            ok = parseNastranOp2(path, resultModel, [&](int pct) {
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

        phase.store(1);
        resultRender = FEMeshConverter::toRenderData(resultModel, [&](int pct) {
            targetVal.store(500 + pct * 450 / 100);
        });
        targetVal.store(950);
        phase.store(2);
    });

    worker->start();
    int displayed = 0;
    while (!worker->isFinished()) {
        int target = targetVal.load();
        if (displayed < target) {
            int diff = target - displayed;
            int step = qMax(1, diff / 4);
            displayed = qMin(displayed + step, target);
        }

        int pct = displayed / 10;  // 0-100
        int p = phase.load();
        if (p == 0) {
            int filePct = targetVal.load() / 5;
            emit loadProgress(pct, filePct < 50 ? "正在解析节点数据..." : "正在解析单元数据...");
        } else if (p == 1) {
            emit loadProgress(pct, QString("正在生成渲染数据（%1 个单元）...").arg(elemCount.load()));
        } else {
            emit loadProgress(pct, "正在更新显示...");
        }

        QApplication::processEvents(QEventLoop::AllEvents);
        worker->wait(16);
    }
    worker->wait();
    delete worker;

    if (!workerOk || resultModel.isEmpty()) {
        emit loadProgress(0, "");
        emit loadFinished(false,
            QString("加载失败：节点 %1，单元 %2")
            .arg(resultModel.nodeCount())
            .arg(resultModel.elementCount()));
        return;
    }

    currentModel_ = resultModel;
    currentRenderData_ = resultRender;

    // 收尾
    emit loadProgress(100, "正在更新显示...");
    QApplication::processEvents();

    updateInfoLabels();
    emit meshGenerated(currentRenderData_.mesh, currentModel_.computeCenter(),
                       currentModel_.computeSize(), currentRenderData_.triangleToElement,
                       currentRenderData_.vertexToNode);
    emit partsChanged(QString::fromStdString(currentModel_.name), currentModel_.parts,
                      currentRenderData_.triangleToPart, currentRenderData_.edgeToPart);

    emit loadProgress(0, "");
    emit loadFinished(true,
        QString("节点: %1  |  单元: %2  |  三角面: %3")
        .arg(currentModel_.nodeCount())
        .arg(currentModel_.elementCount())
        .arg(currentRenderData_.triangleCount()));
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
    QString currentElset;                               // 当前 *Element 关键字上的 ELSET= 值
    std::map<QString, int> elsetToPartIndex;            // ELSET 名 → model.parts 索引

    auto flushPendingElement = [&]() {
        if (pendingElemId >= 0 && !pendingNodeIds.empty()) {
            model.addElement(pendingElemId, currentElemType, pendingNodeIds);
            // 如果该 *Element 段指定了 ELSET，将单元 ID 归入对应部件
            if (!currentElset.isEmpty()) {
                auto it = elsetToPartIndex.find(currentElset);
                if (it == elsetToPartIndex.end()) {
                    // 新 ELSET：创建对应部件
                    FEPart part;
                    part.name = currentElset.toStdString();
                    model.parts.push_back(part);
                    elsetToPartIndex[currentElset] = static_cast<int>(model.parts.size()) - 1;
                    it = elsetToPartIndex.find(currentElset);
                }
                model.parts[it->second].elementIds.push_back(pendingElemId);
            }
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
                // 提取可选的 ELSET= 参数（用于部件归属）
                QRegExp rxElset("ELSET\\s*=\\s*([A-Za-z0-9_\\-\\.]+)");
                rxElset.setCaseSensitivity(Qt::CaseInsensitive);
                currentElset = (rxElset.indexIn(line) >= 0) ? rxElset.cap(1).trimmed() : QString();
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

    // 单元 ID → Property ID 映射（用于按 PID 分组生成部件）
    std::unordered_map<int, int> elemPid;

    // Property 名称（从 PSHELL/PSOLID/PCOMP/PBAR/PBEAM 卡中提取，暂用 PID 编号）
    std::set<int> propertyIds;

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
            int pid = fields[2].toInt();
            std::vector<int> nids;
            for (int i = 3; i < fields.size() && !fields[i].isEmpty(); ++i)
                nids.push_back(fields[i].toInt());
            if (nids.size() >= 10)
                model.addElement(eid, ElementType::TET10, std::vector<int>(nids.begin(), nids.begin() + 10));
            else if (nids.size() >= 4)
                model.addElement(eid, ElementType::TET4, std::vector<int>(nids.begin(), nids.begin() + 4));
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CHEXA") {
            if (fields.size() < 6) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            std::vector<int> nids;
            for (int i = 3; i < fields.size() && !fields[i].isEmpty(); ++i)
                nids.push_back(fields[i].toInt());
            if (nids.size() >= 20)
                model.addElement(eid, ElementType::HEX20, std::vector<int>(nids.begin(), nids.begin() + 20));
            else if (nids.size() >= 8)
                model.addElement(eid, ElementType::HEX8, std::vector<int>(nids.begin(), nids.begin() + 8));
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CPENTA") {
            if (fields.size() < 6) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            std::vector<int> nids;
            for (int i = 3; i < fields.size() && !fields[i].isEmpty(); ++i)
                nids.push_back(fields[i].toInt());
            if (nids.size() >= 6)
                model.addElement(eid, ElementType::WEDGE6, std::vector<int>(nids.begin(), nids.begin() + 6));
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CPYRAM") {
            if (fields.size() < 6) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            std::vector<int> nids;
            for (int i = 3; i < fields.size() && !fields[i].isEmpty(); ++i)
                nids.push_back(fields[i].toInt());
            if (nids.size() >= 5)
                model.addElement(eid, ElementType::PYRAMID5, std::vector<int>(nids.begin(), nids.begin() + 5));
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CTRIA3") {
            if (fields.size() < 6) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            model.addElement(eid, ElementType::TRI3,
                {fields[3].toInt(), fields[4].toInt(), fields[5].toInt()});
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CTRIA6") {
            if (fields.size() < 9) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            std::vector<int> nids;
            for (int i = 3; i < 9 && i < fields.size(); ++i)
                nids.push_back(fields[i].toInt());
            model.addElement(eid, ElementType::TRI6, nids);
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CQUAD4") {
            if (fields.size() < 7) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            model.addElement(eid, ElementType::QUAD4,
                {fields[3].toInt(), fields[4].toInt(), fields[5].toInt(), fields[6].toInt()});
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CQUAD8") {
            if (fields.size() < 11) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            std::vector<int> nids;
            for (int i = 3; i < 11 && i < fields.size(); ++i)
                nids.push_back(fields[i].toInt());
            model.addElement(eid, ElementType::QUAD8, nids);
            elemPid[eid] = pid; propertyIds.insert(pid);
        }
        else if (card == "CBAR" || card == "CBEAM") {
            if (fields.size() < 5) continue;
            int eid = fields[1].toInt();
            int pid = fields[2].toInt();
            model.addElement(eid, ElementType::BAR2,
                {fields[3].toInt(), fields[4].toInt()});
            elemPid[eid] = pid; propertyIds.insert(pid);
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

    // ── 按 Property ID 分组生成部件 ──
    if (!propertyIds.empty()) {
        // PID → parts 索引
        std::map<int, int> pidToPartIndex;
        for (int pid : propertyIds) {
            int idx = static_cast<int>(model.parts.size());
            FEPart part;
            part.name = "Property " + std::to_string(pid);
            part.visible = true;
            model.parts.push_back(part);
            pidToPartIndex[pid] = idx;
        }
        // 将每个单元分配到对应的部件
        for (const auto& [eid, pid] : elemPid) {
            auto it = pidToPartIndex.find(pid);
            if (it != pidToPartIndex.end()) {
                model.parts[it->second].elementIds.push_back(eid);
            }
        }
        // 收集每个部件的节点（从单元的节点列表中提取）
        for (auto& part : model.parts) {
            std::unordered_set<int> nodeSet;
            for (int eid : part.elementIds) {
                auto eit = model.elements.find(eid);
                if (eit != model.elements.end()) {
                    for (int nid : eit->second.nodeIds)
                        nodeSet.insert(nid);
                }
            }
            part.nodeIds.assign(nodeSet.begin(), nodeSet.end());
        }
        qDebug("parseNastranBdf: %d parts generated from Property IDs",
               (int)model.parts.size());
    }

    return true;
}

// ════════════════════════════════════════════════════════════
// Nastran OP2 二进制文件解析
// ════════════════════════════════════════════════════════════

bool FEModelPanel::parseNastranOp2(const QString& filePath, FEModel& model,
                                    const std::function<void(int)>& progress)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("parseNastranOp2: cannot open %s", qPrintable(filePath));
        return false;
    }

    const qint64 fileSize = file.size();
    if (fileSize < 48) {
        qWarning("parseNastranOp2: file too small");
        return false;
    }

    // ── 字节序检测 ──
    bool needSwap = false;
    {
        quint32 first4;
        file.read(reinterpret_cast<char*>(&first4), 4);
        file.seek(0);
        if (first4 == 4) {
            needSwap = false;
        } else {
            quint32 swapped = ((first4 >> 24) & 0xFF) |
                              ((first4 >> 8)  & 0xFF00) |
                              ((first4 << 8)  & 0xFF0000) |
                              ((first4 << 24) & 0xFF000000);
            if (swapped == 4) {
                needSwap = true;
            } else {
                qWarning("parseNastranOp2: not a valid OP2 file (first 4 bytes: 0x%08X)", first4);
                return false;
            }
        }
    }

    // ── 辅助 lambda ──
    auto swapBytes32 = [](quint32 v) -> quint32 {
        return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
               ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
    };

    auto readInt32 = [&]() -> qint32 {
        quint32 v;
        file.read(reinterpret_cast<char*>(&v), 4);
        if (needSwap) v = swapBytes32(v);
        return static_cast<qint32>(v);
    };

    // 读取一个 Fortran 无格式记录: [4字节长度][数据][4字节长度]
    auto readFortranRecord = [&]() -> QByteArray {
        qint32 len = readInt32();
        if (len < 0) len = -len;
        QByteArray data = file.read(len);
        readInt32(); // trailing length
        return data;
    };

    // 读取 marker 记录（包含单个 int32 的 Fortran 记录）
    auto readMarker = [&]() -> qint32 {
        QByteArray rec = readFortranRecord();
        if (rec.size() < 4) return 0;
        quint32 raw;
        memcpy(&raw, rec.constData(), 4);
        if (needSwap) raw = swapBytes32(raw);
        return static_cast<qint32>(raw);
    };

    // 读取一个子表的全部数据（多个 marker(N)+record 对，直到 marker(0)）
    auto readSubtableData = [&]() -> QByteArray {
        QByteArray allData;
        while (true) {
            qint32 m = readMarker();
            if (m == 0) break;
            QByteArray rec = readFortranRecord();
            allData.append(rec);
        }
        return allData;
    };

    // 跳过一个完整的表（header + 所有子表）
    auto skipTable = [&]() {
        readMarker();        // marker(7)
        readFortranRecord(); // header record
        readMarker();        // marker(-2) 首个子表序号
        // 子表循环：数据记录以 marker(0) 结束，
        // 随后的 marker 为 0 表示表结束，负数为下一子表序号
        while (true) {
            while (true) {
                qint32 m = readMarker();
                if (m == 0) break;
                readFortranRecord();
            }
            qint32 m = readMarker();
            if (m == 0) break;
            // m 为负数（-3, -4, ...），表示下一子表序号，继续循环
        }
    };

    // ── 跳过文件头（8 个 Fortran 记录）──
    // marker(3) + date + marker(7) + label + marker(2) + OS + marker(-1) + marker(0)
    for (int i = 0; i < 8; ++i)
        readFortranRecord();

    // ── 收集数据 ──
    std::set<int> propertyIds;
    std::map<int, int> elemPid;

    // ── 表级导航循环 ──
    while (!file.atEnd()) {
        if (progress) {
            int pct = static_cast<int>(file.pos() * 80 / fileSize);
            progress(pct);
        }

        // 读取表名 marker — 若为 0 则文件结束
        qint32 nameMarker = readMarker();
        if (file.atEnd() || nameMarker == 0) break;

        // 读取表名
        QByteArray nameRec = readFortranRecord();
        QString tableName = QString::fromLatin1(nameRec.left(8)).trimmed();

        // 读取 marker(-1)
        readMarker();

        qDebug("parseNastranOp2: table '%s' at pos %lld", qPrintable(tableName), file.pos());

        // 判断是否为几何表
        bool isGeom1 = tableName.startsWith("GEOM1");
        bool isGeom2 = tableName.startsWith("GEOM2");

        if (!isGeom1 && !isGeom2) {
            skipTable();
            continue;
        }

        // ── 读取 header ──
        readMarker();        // marker(7)
        readFortranRecord(); // header record (28 bytes)
        readMarker();        // marker(-2)

        // ── 读取所有子表数据 ──
        // OP2 数据格式：marker(N) + data_block 对，marker(0) 结束一个逻辑记录。
        // 逻辑记录间的 marker 是下一记录首块的 word count（非 0 则继续）。
        // 双 marker(0) 表示表结束。
        std::vector<QByteArray> subtables;
        bool hasFirstBlock = false;
        QByteArray firstBlock;
        while (!file.atEnd()) {
            QByteArray stData;
            // 如果上一轮已读取了首块数据，先放入
            if (hasFirstBlock) {
                stData.append(firstBlock);
                hasFirstBlock = false;
            }
            // 读取 marker(N)+record 对，直到 marker(0)
            while (true) {
                qint32 m = readMarker();
                if (m == 0) break;
                stData.append(readFortranRecord());
            }
            subtables.push_back(stData);
            // 读取下一个 marker：0=表结束，否则是下一记录首块的 word count
            qint32 nextM = readMarker();
            if (nextM == 0) break;
            // 读取该 marker 对应的数据块，留给下一轮使用
            firstBlock = readFortranRecord();
            hasFirstBlock = true;
        }

        if (isGeom1) {
            // ── GEOM1: 解析节点 ──
            // 子表格式：key(2-3 words) + 8-word GRID 条目
            // GRID key: (4501, 40) 标准 Nastran 或 (4501, 45) OptiStruct
            for (const auto& stData : subtables) {
                int nWords = stData.size() / 4;
                if (nWords < 11) continue; // 至少 key + 1 个 GRID

                const qint32* words = reinterpret_cast<const qint32*>(stData.constData());

                // 检查 GRID key
                if (words[0] != 4501) continue;

                // 确定数据起始位置：标准 Nastran key=2 words，OptiStruct key=3 words
                // 通过检查 word[2] 或 word[3] 是否为合理的 NID 来判断
                int dataStart = 2;
                if (nWords > 3 && words[2] > 0 && words[2] <= 100000000) {
                    // word[2] 可能是 NID（标准格式）或 extra header word
                    // 检查 word[3]：如果也是小正整数（CP=0 通常），则 word[2] 是 NID
                    // 但如果 (nWords-3) % 8 == 0 或 1，则 dataStart=3
                    int rem2 = (nWords - 2) % 8;
                    int rem3 = (nWords - 3) % 8;
                    if (rem3 <= 1 && rem2 > 1) {
                        dataStart = 3; // OptiStruct: 3-word key
                    }
                }

                int parsed = 0;
                for (int i = dataStart; i + 8 <= nWords; i += 8) {
                    qint32 nid = words[i];
                    if (nid <= 0) break;

                    quint32 rawX, rawY, rawZ;
                    memcpy(&rawX, &words[i + 2], 4);
                    memcpy(&rawY, &words[i + 3], 4);
                    memcpy(&rawZ, &words[i + 4], 4);

                    if (needSwap) {
                        rawX = swapBytes32(rawX);
                        rawY = swapBytes32(rawY);
                        rawZ = swapBytes32(rawZ);
                    }

                    float x, y, z;
                    memcpy(&x, &rawX, 4);
                    memcpy(&y, &rawY, 4);
                    memcpy(&z, &rawZ, 4);

                    model.addNode(nid, glm::vec3(x, y, z));
                    parsed++;
                }
                if (parsed > 0) {
                    qDebug("parseNastranOp2: parsed %d GRID nodes (dataStart=%d)", parsed, dataStart);
                }
            }
        }

        if (isGeom2) {
            // ── GEOM2: 解析单元 ──
            // 子表格式：key(2 words) + [extra word] + element 条目
            // key 标识单元类型，通过 key 查表确定节点数

            struct ElemInfo {
                ElementType type;
                int nodeCount;
            };
            // key1 → ElemInfo（key2 仅用于验证）
            std::map<int, ElemInfo> elemKeyMap = {
                {2408,  {ElementType::BAR2,     2}},   // CBAR
                {3001,  {ElementType::BAR2,     2}},   // CBAR (OptiStruct)
                {5408,  {ElementType::BAR2,     2}},   // CBEAM
                {5959,  {ElementType::TRI3,     3}},   // CTRIA3
                {4801,  {ElementType::TRI6,     6}},   // CTRIA6
                {2958,  {ElementType::QUAD4,    4}},   // CQUAD4
                {4701,  {ElementType::QUAD8,    8}},   // CQUAD8
                {5508,  {ElementType::TET4,     4}},   // CTETRA4
                {16600, {ElementType::TET10,   10}},   // CTETRA10
                {7308,  {ElementType::HEX8,     8}},   // CHEXA8
                {16300, {ElementType::HEX20,   20}},   // CHEXA20
                {4108,  {ElementType::WEDGE6,   6}},   // CPENTA6
                {16500, {ElementType::WEDGE6,   6}},   // CPENTA15
                {17200, {ElementType::PYRAMID5,  5}},   // CPYRAM
            };

            for (const auto& stData : subtables) {
                int nWords = stData.size() / 4;
                if (nWords < 6) continue;

                const qint32* words = reinterpret_cast<const qint32*>(stData.constData());

                // 查找 key
                qint32 key1 = words[0];
                auto it = elemKeyMap.find(key1);
                if (it == elemKeyMap.end()) continue;

                const ElemInfo& info = it->second;

                // 确定数据起始位置和 stride
                // 标准 Nastran: dataStart=2, OptiStruct: dataStart=3（有额外 header word）
                // 尝试两种起始位置，选择能产生合理 stride 的那个
                int dataStart = 0;
                int stride = 0;

                for (int tryStart = 3; tryStart >= 2 && stride == 0; --tryStart) {
                    if (tryStart >= nWords) continue;
                    qint32 tryEid = words[tryStart];
                    if (tryEid <= 0 || tryEid > 100000000) continue;

                    // 扫描找到第二个 EID
                    for (int s = info.nodeCount + 2; s <= std::min(30, nWords - tryStart - 1); ++s) {
                        int idx = tryStart + s;
                        if (idx + 1 >= nWords) break;
                        qint32 eid2 = words[idx];
                        qint32 pid2 = words[idx + 1];
                        if (eid2 > tryEid && eid2 <= tryEid + 100000 &&
                            pid2 > 0 && pid2 <= 100000000) {
                            // 验证第三个条目
                            int idx3 = tryStart + s * 2;
                            if (idx3 + 1 < nWords) {
                                qint32 eid3 = words[idx3];
                                if (eid3 <= eid2 || eid3 > eid2 + 100000) continue;
                            }
                            dataStart = tryStart;
                            stride = s;
                            break;
                        }
                    }
                }
                if (stride == 0) continue;

                qint32 firstEid = words[dataStart];

                // 解析所有条目
                int dataWords = nWords - dataStart;
                int maxEntries = dataWords / stride;
                int count = 0;

                for (int e = 0; e < maxEntries; ++e) {
                    int base = dataStart + e * stride;
                    if (base + 2 + info.nodeCount > nWords) break;

                    qint32 eid = words[base];
                    qint32 pid = words[base + 1];
                    if (eid <= 0) break;

                    std::vector<int> nodeIds(info.nodeCount);
                    for (int ni = 0; ni < info.nodeCount; ++ni) {
                        nodeIds[ni] = words[base + 2 + ni];
                    }

                    model.addElement(eid, info.type, nodeIds);
                    elemPid[eid] = pid;
                    propertyIds.insert(pid);
                    count++;
                }

                qDebug("parseNastranOp2: parsed %d elements (key=%d, stride=%d, nodes=%d)",
                       count, key1, stride, info.nodeCount);
            }
        }
    }

    file.close();
    if (progress) progress(85);

    qDebug("parseNastranOp2: nodes=%d, elements=%d",
           (int)model.nodes.size(), (int)model.elements.size());

    // ── 按 Property ID 分组生成部件 ──（85-95%）
    if (!propertyIds.empty()) {
        std::map<int, int> pidToPartIndex;
        for (int pid : propertyIds) {
            int idx = static_cast<int>(model.parts.size());
            FEPart part;
            part.name = "Property " + std::to_string(pid);
            part.visible = true;
            model.parts.push_back(part);
            pidToPartIndex[pid] = idx;
        }
        for (const auto& [eid, pid] : elemPid) {
            auto it = pidToPartIndex.find(pid);
            if (it != pidToPartIndex.end()) {
                model.parts[it->second].elementIds.push_back(eid);
            }
        }
        for (auto& part : model.parts) {
            std::unordered_set<int> nodeSet;
            for (int eid : part.elementIds) {
                auto eit = model.elements.find(eid);
                if (eit != model.elements.end()) {
                    for (int nid : eit->second.nodeIds)
                        nodeSet.insert(nid);
                }
            }
            part.nodeIds.assign(nodeSet.begin(), nodeSet.end());
        }
        qDebug("parseNastranOp2: %d parts generated from Property IDs",
               (int)model.parts.size());
    }
    if (progress) progress(95);

    if (model.nodes.empty()) {
        qWarning("parseNastranOp2: no nodes found in file");
        return false;
    }

    if (progress) progress(100);
    return true;
}

// ════════════════════════════════════════════════════════════
// OP2 结果解析（位移 OUG + 应力 OES）
// ════════════════════════════════════════════════════════════

bool FEModelPanel::parseNastranOp2Results(const QString& filePath, FEResultData& results)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("parseNastranOp2Results: cannot open %s", qPrintable(filePath));
        return false;
    }

    const qint64 fileSize = file.size();
    if (fileSize < 48) return false;

    // ── 字节序检测 ──
    bool needSwap = false;
    {
        quint32 first4;
        file.read(reinterpret_cast<char*>(&first4), 4);
        file.seek(0);
        if (first4 == 4) {
            needSwap = false;
        } else {
            quint32 swapped = ((first4 >> 24) & 0xFF) |
                              ((first4 >> 8)  & 0xFF00) |
                              ((first4 << 8)  & 0xFF0000) |
                              ((first4 << 24) & 0xFF000000);
            if (swapped == 4) needSwap = true;
            else return false;
        }
    }

    // ── 辅助 lambda（与几何解析完全相同） ──
    auto swapBytes32 = [](quint32 v) -> quint32 {
        return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
               ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
    };

    auto readInt32 = [&]() -> qint32 {
        quint32 v;
        file.read(reinterpret_cast<char*>(&v), 4);
        if (needSwap) v = swapBytes32(v);
        return static_cast<qint32>(v);
    };

    auto readFortranRecord = [&]() -> QByteArray {
        qint32 len = readInt32();
        if (len < 0) len = -len;
        QByteArray data = file.read(len);
        readInt32();
        return data;
    };

    auto readMarker = [&]() -> qint32 {
        QByteArray rec = readFortranRecord();
        if (rec.size() < 4) return 0;
        quint32 raw;
        memcpy(&raw, rec.constData(), 4);
        if (needSwap) raw = swapBytes32(raw);
        return static_cast<qint32>(raw);
    };

    auto skipTable = [&]() {
        readMarker();        // marker(7)
        readFortranRecord(); // header record
        readMarker();        // marker(-2)
        while (true) {
            while (true) {
                qint32 m = readMarker();
                if (m == 0) break;
                readFortranRecord();
            }
            qint32 m = readMarker();
            if (m == 0) break;
            readFortranRecord(); // 读取 marker 对应的数据块
        }
    };

    auto readFloat = [&](const char* ptr) -> float {
        quint32 raw;
        memcpy(&raw, ptr, 4);
        if (needSwap) raw = swapBytes32(raw);
        float val;
        memcpy(&val, &raw, 4);
        return val;
    };

    auto readInt = [&](const char* ptr) -> qint32 {
        quint32 raw;
        memcpy(&raw, ptr, 4);
        if (needSwap) raw = swapBytes32(raw);
        return static_cast<qint32>(raw);
    };

    // ── 跳过文件头 ──
    for (int i = 0; i < 8; ++i)
        readFortranRecord();

    results.clear();
    // subcase ID → index in results.subcases
    std::map<int, int> subcaseMap;

    bool foundAny = false;

    // ── 表级导航循环 ──
    while (!file.atEnd()) {
        qint32 nameMarker = readMarker();
        if (file.atEnd() || nameMarker == 0) break;

        QByteArray nameRec = readFortranRecord();
        QString tableName = QString::fromLatin1(nameRec.left(8)).trimmed();

        readMarker(); // marker(-1)

        bool isOUG = tableName.startsWith("OUG") || tableName.startsWith("OUGV");
        bool isOES = tableName.startsWith("OES");

        if (!isOUG && !isOES) {
            skipTable();
            continue;
        }

        qDebug("parseNastranOp2Results: found result table '%s'", qPrintable(tableName));

        // ── 读取 header (7 words = 28 bytes) ──
        readMarker();        // marker(7)
        readFortranRecord(); // header record（不从中提取信息，改用 IDENT）
        readMarker();        // marker(-2)

        // ── 读取所有子表数据 ──
        std::vector<QByteArray> subtables;
        bool hasFirstBlock = false;
        QByteArray firstBlock;
        while (!file.atEnd()) {
            QByteArray stData;
            if (hasFirstBlock) {
                stData.append(firstBlock);
                hasFirstBlock = false;
            }
            while (true) {
                qint32 m = readMarker();
                if (m == 0) break;
                stData.append(readFortranRecord());
            }
            subtables.push_back(stData);
            qint32 nextM = readMarker();
            if (nextM == 0) break;
            firstBlock = readFortranRecord();
            hasFirstBlock = true;
        }

        // ── 子表结构：subtable[0]=元数据，然后交替排列 IDENT + 数据 ──
        // IDENT 记录 (147 words = 588 bytes):
        //   word 0: approach_code
        //   word 1: table_code
        //   word 2: element_type（OES 用）
        //   word 3: subcase ID
        const int IDENT_SIZE = 588; // 147 words

        // 辅助 lambda：获取或创建 subcase
        auto getOrCreateSubcase = [&](int scId) -> FESubcase& {
            auto it = subcaseMap.find(scId);
            if (it == subcaseMap.end()) {
                int idx = static_cast<int>(results.subcases.size());
                FESubcase sc;
                sc.id = scId;
                sc.name = "Subcase " + std::to_string(scId);
                results.subcases.push_back(sc);
                subcaseMap[scId] = idx;
                return results.subcases[idx];
            }
            return results.subcases[it->second];
        };

        if (isOUG) {
            // ════════════════════════════════════════════
            // OUG: 位移结果
            // 子表交替排列: IDENT(588B) + 数据, 每对对应一个工况
            // 数据格式: NID(4) + type(4) + tx,ty,tz,rx,ry,rz(24) = 32 bytes/entry
            // ════════════════════════════════════════════
            for (int si = 1; si + 1 < static_cast<int>(subtables.size()); si += 2) {
                const auto& identRec = subtables[si];
                const auto& dataRec  = subtables[si + 1];

                // 验证 IDENT 记录
                if (identRec.size() < 16) continue;
                int subcaseId = readInt(identRec.constData() + 12); // word 3
                if (subcaseId <= 0) subcaseId = 1;

                if (dataRec.size() < 32) continue;

                FESubcase& subcase = getOrCreateSubcase(subcaseId);

                // 构建位移结果
                FEResultType dispType;
                dispType.name = "Displacement";
                dispType.hasVector = true;

                FEResultComponent compMag, compX, compY, compZ;
                compMag.name = "Magnitude"; compMag.field.name = "Displacement Magnitude";
                compMag.field.unit = "mm";   compMag.field.location = FieldLocation::Node;
                compX.name = "X"; compX.field.name = "Displacement X";
                compX.field.unit = "mm"; compX.field.location = FieldLocation::Node;
                compY.name = "Y"; compY.field.name = "Displacement Y";
                compY.field.unit = "mm"; compY.field.location = FieldLocation::Node;
                compZ.name = "Z"; compZ.field.name = "Displacement Z";
                compZ.field.unit = "mm"; compZ.field.location = FieldLocation::Node;

                dispType.vectorField.name = "Displacement";
                dispType.vectorField.unit = "mm";
                dispType.vectorField.location = FieldLocation::Node;

                int parsed = 0;
                const char* ptr = dataRec.constData();
                int nBytes = dataRec.size();

                for (int offset = 0; offset + 32 <= nBytes; offset += 32) {
                    qint32 nid = readInt(ptr + offset);
                    if (nid <= 0 || nid > 100000000) continue;

                    float tx = readFloat(ptr + offset + 8);
                    float ty = readFloat(ptr + offset + 12);
                    float tz = readFloat(ptr + offset + 16);
                    float mag = std::sqrt(tx*tx + ty*ty + tz*tz);

                    compMag.field.values[nid] = mag;
                    compX.field.values[nid] = tx;
                    compY.field.values[nid] = ty;
                    compZ.field.values[nid] = tz;
                    dispType.vectorField.values[nid] = glm::vec3(tx, ty, tz);
                    parsed++;
                }

                if (parsed > 0) {
                    dispType.components.push_back(std::move(compMag));
                    dispType.components.push_back(std::move(compX));
                    dispType.components.push_back(std::move(compY));
                    dispType.components.push_back(std::move(compZ));
                    subcase.resultTypes.push_back(std::move(dispType));
                    foundAny = true;
                    qDebug("parseNastranOp2Results: parsed %d disp nodes (subcase %d)",
                           parsed, subcaseId);
                }
            }

        } else if (isOES) {
            // ════════════════════════════════════════════
            // OES: 应力结果
            // 子表交替排列: IDENT(588B) + 数据
            // 每个 IDENT 携带 element_type(word2) 和 subcase_id(word3)
            // 同一工况可能有多种单元类型（壳+实体+杆）
            // ════════════════════════════════════════════
            for (int si = 1; si + 1 < static_cast<int>(subtables.size()); si += 2) {
                const auto& identRec = subtables[si];
                const auto& dataRec  = subtables[si + 1];

                if (identRec.size() < 16 || dataRec.size() < 32) continue;

                int elemTypeCode = readInt(identRec.constData() + 8);  // word 2
                int subcaseId    = readInt(identRec.constData() + 12); // word 3
                if (subcaseId <= 0) subcaseId = 1;

                // 杆单元: CROD(1), CTUBE(3), CBAR(34), CBEAM(2)
                bool isRod = (elemTypeCode == 1 || elemTypeCode == 3 ||
                              elemTypeCode == 34 || elemTypeCode == 2);
                // 壳单元: CTRIA3(74), CQUAD4(33), CTRIA6(75), CQUAD4-144(144)
                bool isShell = (elemTypeCode == 33 || elemTypeCode == 74 ||
                               elemTypeCode == 75 || elemTypeCode == 144);
                // 实体: CHEXA(67), CTETRA(39), CPENTA(68)
                bool isSolid = (elemTypeCode == 39 || elemTypeCode == 67 ||
                               elemTypeCode == 68);

                if (!isRod && !isShell && !isSolid) {
                    qDebug("parseNastranOp2Results: skipping OES elem type %d (subcase %d)",
                           elemTypeCode, subcaseId);
                    continue;
                }

                FESubcase& subcase = getOrCreateSubcase(subcaseId);

                // 查找或创建该工况的 Stress 结果类型
                FEResultType* stressPtr = nullptr;
                for (auto& rt : subcase.resultTypes) {
                    if (rt.name == "Stress") { stressPtr = &rt; break; }
                }
                if (!stressPtr) {
                    FEResultType stressType;
                    stressType.name = "Stress";
                    stressType.hasVector = false;
                    subcase.resultTypes.push_back(std::move(stressType));
                    stressPtr = &subcase.resultTypes.back();
                }

                const char* ptr = dataRec.constData();
                int nBytes = dataRec.size();
                int parsed = 0;

                if (isRod) {
                    // CROD: EID + 4 floats (axial, MSa, torsion, MSt) = 5 words = 20 bytes
                    int entrySize = 20;

                    auto ensureCompIdx = [&](const std::string& name, const std::string& fullName) -> int {
                        for (int i = 0; i < static_cast<int>(stressPtr->components.size()); i++) {
                            if (stressPtr->components[i].name == name) return i;
                        }
                        FEResultComponent c;
                        c.name = name; c.field.name = fullName;
                        c.field.unit = "MPa"; c.field.location = FieldLocation::Element;
                        stressPtr->components.push_back(std::move(c));
                        return static_cast<int>(stressPtr->components.size()) - 1;
                    };
                    int iAxial   = ensureCompIdx("Axial",   "Axial Stress");
                    int iTorsion = ensureCompIdx("Torsion", "Torsional Stress");

                    for (int offset = 0; offset + entrySize <= nBytes; offset += entrySize) {
                        qint32 eid = readInt(ptr + offset);
                        if (eid <= 0 || eid > 100000000) continue;

                        stressPtr->components[iAxial].field.values[eid]   = readFloat(ptr + offset + 4);
                        stressPtr->components[iTorsion].field.values[eid] = readFloat(ptr + offset + 12);
                        parsed++;
                    }
                } else if (isShell) {
                    // 壳单元: EID + [fiber sx sy txy angle smaj smin svm] × 2 = 17 words = 68 bytes
                    int entrySize = 68;

                    // 确保分量存在（多种壳单元共享同一组分量）
                    // 注意: 必须先用 index 查找，避免 push_back 导致引用失效
                    auto ensureCompIdx = [&](const std::string& name, const std::string& fullName) -> int {
                        for (int i = 0; i < static_cast<int>(stressPtr->components.size()); i++) {
                            if (stressPtr->components[i].name == name) return i;
                        }
                        FEResultComponent c;
                        c.name = name; c.field.name = fullName;
                        c.field.unit = "MPa"; c.field.location = FieldLocation::Element;
                        stressPtr->components.push_back(std::move(c));
                        return static_cast<int>(stressPtr->components.size()) - 1;
                    };
                    int iSx  = ensureCompIdx("Sigma-X",   "Normal Stress X");
                    int iSy  = ensureCompIdx("Sigma-Y",   "Normal Stress Y");
                    int iTxy = ensureCompIdx("Tau-XY",     "Shear Stress XY");
                    int iVm  = ensureCompIdx("Von Mises",  "Von Mises Stress");

                    for (int offset = 0; offset + entrySize <= nBytes; offset += entrySize) {
                        qint32 eid = readInt(ptr + offset);
                        if (eid <= 0 || eid > 100000000) continue;

                        stressPtr->components[iSx].field.values[eid]  = readFloat(ptr + offset + 8);
                        stressPtr->components[iSy].field.values[eid]  = readFloat(ptr + offset + 12);
                        stressPtr->components[iTxy].field.values[eid] = readFloat(ptr + offset + 16);
                        stressPtr->components[iVm].field.values[eid]  = readFloat(ptr + offset + 32);
                        parsed++;
                    }
                } else if (isSolid) {
                    // 实体: EID + center(cid+9floats=40B) + nnodes × corner(40B)
                    int nnodes = 0;
                    if (elemTypeCode == 67) nnodes = 8;       // CHEXA
                    else if (elemTypeCode == 39) nnodes = 4;  // CTETRA
                    else if (elemTypeCode == 68) nnodes = 6;  // CPENTA
                    int entrySize = 44 + nnodes * 40;

                    auto ensureCompIdx = [&](const std::string& name, const std::string& fullName) -> int {
                        for (int i = 0; i < static_cast<int>(stressPtr->components.size()); i++) {
                            if (stressPtr->components[i].name == name) return i;
                        }
                        FEResultComponent c;
                        c.name = name; c.field.name = fullName;
                        c.field.unit = "MPa"; c.field.location = FieldLocation::Element;
                        stressPtr->components.push_back(std::move(c));
                        return static_cast<int>(stressPtr->components.size()) - 1;
                    };
                    int iSxx = ensureCompIdx("Sigma-XX", "Normal Stress XX");
                    int iSyy = ensureCompIdx("Sigma-YY", "Normal Stress YY");
                    int iSzz = ensureCompIdx("Sigma-ZZ", "Normal Stress ZZ");
                    int iTxy = ensureCompIdx("Tau-XY",   "Shear Stress XY");
                    int iTyz = ensureCompIdx("Tau-YZ",   "Shear Stress YZ");
                    int iTxz = ensureCompIdx("Tau-XZ",   "Shear Stress XZ");
                    int iVm  = ensureCompIdx("Von Mises","Von Mises Stress");

                    int offset = 0;
                    while (offset + entrySize <= nBytes) {
                        qint32 eid = readInt(ptr + offset);
                        if (eid <= 0 || eid > 100000000) break;

                        stressPtr->components[iSxx].field.values[eid] = readFloat(ptr + offset + 8);
                        stressPtr->components[iSyy].field.values[eid] = readFloat(ptr + offset + 12);
                        stressPtr->components[iSzz].field.values[eid] = readFloat(ptr + offset + 16);
                        stressPtr->components[iTxy].field.values[eid] = readFloat(ptr + offset + 20);
                        stressPtr->components[iTyz].field.values[eid] = readFloat(ptr + offset + 24);
                        stressPtr->components[iTxz].field.values[eid] = readFloat(ptr + offset + 28);
                        stressPtr->components[iVm].field.values[eid]  = readFloat(ptr + offset + 32);
                        parsed++;
                        offset += entrySize;
                    }
                }

                if (parsed > 0) {
                    foundAny = true;
                    qDebug("parseNastranOp2Results: parsed %d stress elems (subcase %d, elemType %d)",
                           parsed, subcaseId, elemTypeCode);
                }
            }
        }
    }

    file.close();

    if (foundAny) {
        qDebug("parseNastranOp2Results: %d subcases loaded", (int)results.subcases.size());
    }

    return foundAny;
}

// ════════════════════════════════════════════════════════════
// 清空模型
// ════════════════════════════════════════════════════════════

void FEModelPanel::clearModel() {
    currentModel_.clear();
    currentRenderData_.clear();
    updateInfoLabels();
    emit meshGenerated(Mesh{}, glm::vec3(0), 0, {}, {});
    emit partsChanged(QString(), {}, {}, {});
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

// ════════════════════════════════════════════════════════════
// 选中信息
// ════════════════════════════════════════════════════════════

QGroupBox* FEModelPanel::createSelectionGroup() {
    auto* group = new QGroupBox("选中信息");
    auto* layout = new QVBoxLayout(group);

    selModeLabel_  = new QLabel("模式: -");
    selCountLabel_ = new QLabel("数量: 0");
    selIdsLabel_   = new QLabel("ID: -");
    selIdsLabel_->setWordWrap(true);

    layout->addWidget(selModeLabel_);
    layout->addWidget(selCountLabel_);
    layout->addWidget(selIdsLabel_);

    return group;
}

void FEModelPanel::updateSelectionInfo(PickMode mode, int count, const std::vector<int>& ids) {
    QString modeName;
    switch (mode) {
        case PickMode::Node:    modeName = "节点"; break;
        case PickMode::Element: modeName = "单元"; break;
        case PickMode::Part:    modeName = "部件"; break;
    }
    selModeLabel_->setText(QString("模式: %1").arg(modeName));
    selCountLabel_->setText(QString("数量: %1").arg(count));

    if (ids.empty()) {
        selIdsLabel_->setText("ID: -");
    } else {
        QStringList idStrs;
        int limit = qMin(static_cast<int>(ids.size()), 20);
        for (int i = 0; i < limit; ++i)
            idStrs.append(QString::number(ids[i]));
        QString text = "ID: " + idStrs.join(", ");
        if (static_cast<int>(ids.size()) > 20)
            text += QString(" ... (+%1)").arg(ids.size() - 20);
        selIdsLabel_->setText(text);
    }
}
