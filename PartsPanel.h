/**
 * @file PartsPanel.h
 * @brief 部件模型树面板声明
 *
 * 以树形结构显示 FEM 模型的组成：
 *   - 根节点：模型名称
 *   - 子节点：每个部件（带颜色色块 + 复选框控制显隐）
 */

#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <vector>
#include <glm/glm.hpp>

#include "FEGroup.h"
#include "FEPickResult.h"

struct Theme;

class PartsPanel : public QWidget {
    Q_OBJECT

public:
    explicit PartsPanel(QWidget* parent = nullptr);

    /** @brief 应用主题 */
    void applyTheme(const Theme& theme);

    /** @brief 用新的模型名称、部件、set 集和颜色更新树 */
    void setParts(const QString& modelName,
                  const std::vector<FEPart>& parts,
                  const std::vector<FENodeSet>& nodeSets,
                  const std::vector<FEElementSet>& elementSets,
                  const std::vector<glm::vec3>& partColors);

public slots:
    /** @brief 程序化选中指定部件（由 GLWidget 部件拾取触发） */
    void selectParts(const std::vector<int>& partIndices);

signals:
    /** @brief 某个部件的可见性被用户切换 */
    void partVisibilityChanged(int partIndex, bool visible);

    /** @brief 模型树中选中的部件发生变化（多选） */
    void partSelectionChanged(const std::vector<int>& selectedParts);

    /** @brief 模型树中选中的节点集/单元集发生变化 */
    void setSelectionRequested(PickMode mode, const std::vector<int>& ids);

    /** @brief 节点集/单元集的可见性被用户切换 */
    void setVisibilityRequested(PickMode mode, const std::vector<int>& ids, bool visible);

private slots:
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onSelectionChanged();

private:
    QPixmap makeColorSwatch(const glm::vec3& color, int size = 12) const;
    void emitLeafVisibility(QTreeWidgetItem* item, bool visible);
    void setChildrenCheckState(QTreeWidgetItem* item, Qt::CheckState state);
    void updateParentCheckStates(QTreeWidgetItem* item);

    QTreeWidget*  tree_       = nullptr;
    QTreeWidgetItem* rootItem_ = nullptr;
    QTreeWidgetItem* partGroupItem_ = nullptr;
    QTreeWidgetItem* nodeSetGroupItem_ = nullptr;
    QTreeWidgetItem* elementSetGroupItem_ = nullptr;
    bool          updating_   = false;   // 防止信号递归

    std::vector<FEPart> parts_;
    std::vector<FENodeSet> nodeSets_;
    std::vector<FEElementSet> elementSets_;
};
