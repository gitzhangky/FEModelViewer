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

class PartsPanel : public QWidget {
    Q_OBJECT

public:
    explicit PartsPanel(QWidget* parent = nullptr);

    /** @brief 用新的模型名称、部件列表和颜色更新树 */
    void setParts(const QString& modelName,
                  const std::vector<FEPart>& parts,
                  const std::vector<glm::vec3>& partColors);

signals:
    /** @brief 某个部件的可见性被用户切换 */
    void partVisibilityChanged(int partIndex, bool visible);

private slots:
    void onItemChanged(QTreeWidgetItem* item, int column);

private:
    QPixmap makeColorSwatch(const glm::vec3& color, int size = 12) const;

    QTreeWidget*  tree_       = nullptr;
    QTreeWidgetItem* rootItem_ = nullptr;
    bool          updating_   = false;   // 防止信号递归
};
