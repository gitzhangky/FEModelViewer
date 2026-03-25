/**
 * @file FEModelPanel.h
 * @brief 有限元模型信息面板声明
 *
 * 提供 FEM 模型信息显示和选中信息：
 *   - 模型统计信息（节点数、单元数、三角面数、尺寸）
 *   - 选中信息（模式、数量、ID 列表）
 *   - 模型文件加载逻辑（供工具栏调用）
 */

#pragma once

#include <QWidget>
#include <QGroupBox>
#include <QLabel>
#include <QCheckBox>
#include <QString>
#include <functional>

#include "FEModel.h"
#include "FERenderData.h"
#include "FEPickResult.h"
#include "FEGroup.h"
#include "FEResultData.h"

struct Theme;

class FEModelPanel : public QWidget {
    Q_OBJECT

public:
    explicit FEModelPanel(QWidget* parent = nullptr);

    /** @brief 打开文件对话框并加载 FEM 模型（供工具栏调用） */
    void loadModelFromFile();

    /** @brief 从指定路径加载 FEM 模型（供底部面板调用） */
    void loadModelFromPath(const QString& path);

    /** @brief 清空当前模型 */
    void clearModel();

    /** @brief 从 OP2 文件解析结果数据（位移/应力），独立于几何解析 */
    bool parseNastranOp2Results(const QString& filePath, FEResultData& results);

    /** @brief 从 UNV 文件解析结果数据（Dataset 2414/55），独立于几何解析 */
    bool parseUnvResults(const QString& filePath, FEResultData& results);

    /** @brief 应用主题 */
    void applyTheme(const Theme& theme);

    /** @brief 获取当前模型 */
    const FEModel& currentModel() const { return currentModel_; }

    /** @brief 获取当前渲染数据 */
    const FERenderData& currentRenderData() const { return currentRenderData_; }

signals:
    void meshGenerated(const Mesh& mesh, const glm::vec3& center, float size,
                       const std::vector<int>& triToElem,
                       const std::vector<int>& vertexToNode);

    void partsChanged(const QString& modelName, const std::vector<FEPart>& parts,
                      const std::vector<int>& triToPart, const std::vector<int>& edgeToPart);

    /** @brief 加载进度更新 (0-100, 描述文字) */
    void loadProgress(int percent, const QString& text);

    /** @brief 加载完成 (成功/失败, 消息) */
    void loadFinished(bool success, const QString& message);

    /** @brief 结果数据加载完成 */
    void resultsLoaded(const FEResultData& results);

    /** @brief ID标签显示/隐藏 */
    void labelVisibilityChanged(bool visible);

public slots:
    void updateSelectionInfo(PickMode mode, int count, const std::vector<int>& ids);

private:
    QGroupBox* createInfoGroup();       // 模型信息分组
    QGroupBox* createSelectionGroup();  // 选中信息分组

    bool parseAbaqusInp(const QString& filePath, FEModel& model, const std::function<void(int)>& progress = nullptr);
    bool parseNastranBdf(const QString& filePath, FEModel& model, const std::function<void(int)>& progress = nullptr);
    bool parseNastranOp2(const QString& filePath, FEModel& model, const std::function<void(int)>& progress = nullptr);

    void updateInfoLabels();

    // ── 数据 ──
    FEModel currentModel_;
    FERenderData currentRenderData_;

    // ── 信息显示标签 ──
    QLabel* nodeCountLabel_     = nullptr;
    QLabel* elementCountLabel_  = nullptr;
    QLabel* triangleCountLabel_ = nullptr;
    QLabel* modelSizeLabel_     = nullptr;

    // ── 选中信息标签 ──
    QLabel* selModeLabel_   = nullptr;
    QLabel* selCountLabel_  = nullptr;
    QLabel* selIdsLabel_    = nullptr;
    QCheckBox* labelCheck_  = nullptr;
};
