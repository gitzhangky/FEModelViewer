/**
 * @file FEModelPanel.h
 * @brief 有限元模型显示面板声明
 *
 * 提供 FEM 模型的加载、显示控制和信息展示：
 *   - 测试模型生成（演示用）
 *   - 模型信息显示（节点数、单元数、包围盒等）
 *   - 显示选项（显示节点、单元着色、拾取模式等）
 *
 * ┌─────────────────────────┐
 * │  有限元模型              │
 * │ ┌─────────────────────┐ │
 * │ │  模型加载            │ │
 * │ │  [生成悬臂梁模型]    │ │
 * │ │  [生成板模型]        │ │
 * │ │  [清空模型]          │ │
 * │ └─────────────────────┘ │
 * │ ┌─────────────────────┐ │
 * │ │  模型信息            │ │
 * │ │  节点数: 0           │ │
 * │ │  单元数: 0           │ │
 * │ │  三角面数: 0         │ │
 * │ └─────────────────────┘ │
 * │ ┌─────────────────────┐ │
 * │ │  显示选项            │ │
 * │ │  [✓] 显示节点        │ │
 * │ │  [✓] 显示单元编号    │ │
 * │ │  拾取模式: [节点▼]   │ │
 * │ └─────────────────────┘ │
 * └─────────────────────────┘
 */

#pragma once

#include <QWidget>
#include <QGroupBox>
#include <QLabel>
#include <QString>
#include <functional>

#include "FEModel.h"
#include "FERenderData.h"
#include "FEPickResult.h"
#include "FEGroup.h"

class FEModelPanel : public QWidget {
    Q_OBJECT

public:
    explicit FEModelPanel(QWidget* parent = nullptr);

    /** @brief 生成默认的悬臂梁模型（可用于启动时自动加载） */
    void loadDefaultModel();

signals:
    /**
     * @brief 生成了新的 FEM 渲染数据，通知 GLWidget 显示
     *
     * 当用户点击"生成模型"按钮后，面板内部完成：
     *   FEModel → FEMeshConverter::toRenderData() → FERenderData
     * 然后通过此信号将 Mesh 传递给 GLWidget 渲染。
     */
    void meshGenerated(const Mesh& mesh, const glm::vec3& center, float size,
                       const std::vector<int>& triToElem,
                       const std::vector<int>& triToFace);

    /** @brief 拾取模式改变 */
    void pickModeChanged(PickMode mode);

    /** @brief 模型的部件列表发生变化（新模型加载 / 清空） */
    void partsChanged(const QString& modelName, const std::vector<FEPart>& parts,
                      const std::vector<int>& triToPart, const std::vector<int>& edgeToPart);

private:
    // ── 创建各分组 ──
    QGroupBox* createLoadGroup();     // 模型加载分组
    QGroupBox* createInfoGroup();     // 模型信息分组
    QGroupBox* createOptionGroup();   // 显示选项分组

    // ── 模型加载 ──

    /** @brief 打开文件对话框并加载 FEM 模型 */
    void loadModelFromFile();

    /** @brief 解析 ABAQUS .inp 文件，支持进度回调（0-100） */
    bool parseAbaqusInp(const QString& filePath, FEModel& model, const std::function<void(int)>& progress = nullptr);

    /** @brief 解析 Nastran BDF/FEM 文件，支持进度回调（0-100） */
    bool parseNastranBdf(const QString& filePath, FEModel& model, const std::function<void(int)>& progress = nullptr);

    // ── 测试模型生成 ──

    void generateBeamModel();

    /**
     * @brief 生成平板测试模型（2D QUAD4 单元）
     *
     * 创建一个 nx × ny 的四边形网格，模拟薄板结构。
     * 用于验证 2D 单元的三角化和拾取功能。
     */
    void generatePlateModel();

    /**
     * @brief 生成混合单元测试模型（TRI3 + QUAD4）
     *
     * 创建一个包含三角形和四边形混合单元的平面网格。
     * 用于验证不同单元类型的处理。
     */
    void generateMixedModel();

    /** @brief 更新模型信息显示标签 */
    void updateInfoLabels();

    // ── 数据 ──
    FEModel currentModel_;            // 当前加载的 FEM 模型
    FERenderData currentRenderData_;  // 当前的渲染数据包

    // ── 信息显示标签 ──
    QLabel* nodeCountLabel_     = nullptr;  // 节点数
    QLabel* elementCountLabel_  = nullptr;  // 单元数
    QLabel* triangleCountLabel_ = nullptr;  // 渲染三角面数
    QLabel* modelSizeLabel_     = nullptr;  // 模型尺寸
};
