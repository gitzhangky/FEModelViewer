/**
 * @file PartsPanel.cpp
 * @brief 部件模型树面板实现
 */

#include "PartsPanel.h"
#include "Theme.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QPixmap>
#include <QPainter>
#include <QIcon>
#include <QSignalBlocker>
#include <algorithm>
#include <set>

namespace {

constexpr int kIndexRole = Qt::UserRole;
constexpr int kKindRole = Qt::UserRole + 1;

enum ItemKind {
    ModelRootItem = 0,
    PartGroupItem,
    PartItem,
    NodeSetGroupItem,
    NodeSetItem,
    ElementSetGroupItem,
    ElementSetItem
};

ItemKind itemKind(const QTreeWidgetItem* item) {
    if (!item) return ModelRootItem;
    return static_cast<ItemKind>(item->data(0, kKindRole).toInt());
}

void setItemMeta(QTreeWidgetItem* item, ItemKind kind, int index = -1) {
    item->setData(0, kKindRole, static_cast<int>(kind));
    item->setData(0, kIndexRole, index);
}

QString countLabel(const std::string& name, size_t count) {
    QString label = QString::fromStdString(name);
    if (label.isEmpty()) label = "未命名";
    label += QString("  (%1)").arg(count);
    return label;
}

std::vector<int> sortedUnique(std::vector<int> ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

}

PartsPanel::PartsPanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(140);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(0);

    tree_ = new QTreeWidget;
    tree_->setHeaderLabel("模型树");
    tree_->setColumnCount(1);
    tree_->setIndentation(16);
    tree_->setAnimated(true);
    tree_->setUniformRowHeights(true);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(tree_);

    connect(tree_, &QTreeWidget::itemChanged, this, &PartsPanel::onItemChanged);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &PartsPanel::onSelectionChanged);

    // 默认主题在 MainWindow 中统一调用 applyTheme() 设置
}

void PartsPanel::applyTheme(const Theme& t) {
    setStyleSheet(QString(
        "QWidget { background: %1; color: %2; }"
        "QTreeWidget {"
        "  background: %3; border: 1px solid %4;"
        "  border-radius: 8px; outline: none; padding: 2px; }"
        "QTreeWidget::item {"
        "  padding: 4px 4px; border-radius: 4px; margin: 1px 0; }"
        "QTreeWidget::item:hover { background: %4; }"
        "QTreeWidget::item:selected { background: %5; }"
        "QHeaderView::section {"
        "  background: %3; border: none; border-bottom: 1px solid %4;"
        "  padding: 6px 10px; font-weight: bold; font-size: 12px; color: %6; }"
        "QTreeWidget::indicator {"
        "  width: 16px; height: 16px; border-radius: 4px;"
        "  border: 2px solid %7; background: %4; }"
        "QTreeWidget::indicator:checked { background: %6; border-color: %6; }"
        "QTreeWidget::indicator:indeterminate { background: %5; border-color: %6; }"
        "QScrollBar:vertical {"
        "  background: transparent; width: 10px; border-radius: 5px; margin: 4px 0; }"
        "QScrollBar::handle:vertical {"
        "  background: %5; border-radius: 5px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: %7; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
    ).arg(t.base, t.text, t.mantle, t.surface0, t.surface1, t.blue, t.surface2));
}

QPixmap PartsPanel::makeColorSwatch(const glm::vec3& color, int size) const {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QColor c(static_cast<int>(color.x * 255),
              static_cast<int>(color.y * 255),
              static_cast<int>(color.z * 255));
    p.setBrush(c);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(1, 1, size - 2, size - 2, 3, 3);
    return pm;
}

void PartsPanel::setParts(const QString& modelName,
                          const std::vector<FEPart>& parts,
                          const std::vector<FENodeSet>& nodeSets,
                          const std::vector<FEElementSet>& elementSets,
                          const std::vector<glm::vec3>& partColors) {
    updating_ = true;
    tree_->clear();
    rootItem_ = nullptr;
    partGroupItem_ = nullptr;
    nodeSetGroupItem_ = nullptr;
    elementSetGroupItem_ = nullptr;
    parts_ = parts;
    nodeSets_ = nodeSets;
    elementSets_ = elementSets;

    // 无部件、无 set 集时不创建根节点
    if (parts.empty() && nodeSets.empty() && elementSets.empty()) {
        updating_ = false;
        return;
    }

    // 根节点 = 模型名称
    QString rootName = modelName.isEmpty() ? "模型" : modelName;
    rootItem_ = new QTreeWidgetItem(tree_, {rootName});
    rootItem_->setCheckState(0, Qt::Checked);
    rootItem_->setFlags(rootItem_->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    setItemMeta(rootItem_, ModelRootItem);
    QFont rootFont = rootItem_->font(0);
    rootFont.setBold(true);
    rootFont.setPointSize(rootFont.pointSize() + 1);
    rootItem_->setFont(0, rootFont);

    if (!parts.empty()) {
        partGroupItem_ = new QTreeWidgetItem(rootItem_, {"部件"});
        partGroupItem_->setCheckState(0, Qt::Checked);
        partGroupItem_->setFlags(partGroupItem_->flags() | Qt::ItemIsUserCheckable |
                                 Qt::ItemIsEnabled);
        setItemMeta(partGroupItem_, PartGroupItem);

        for (int i = 0; i < static_cast<int>(parts.size()); ++i) {
            const FEPart& part = parts[i];

            QString label = QString::fromStdString(part.name);
            if (label.isEmpty()) label = QString("Part %1").arg(i + 1);
            if (!part.elementIds.empty())
                label += QString("  (%1)").arg(part.elementIds.size());

            auto* item = new QTreeWidgetItem(partGroupItem_, {label});
            item->setCheckState(0, part.visible ? Qt::Checked : Qt::Unchecked);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            setItemMeta(item, PartItem, i);   // Qt::UserRole 保持存 part index

            // 颜色色块图标
            if (i < static_cast<int>(partColors.size())) {
                item->setIcon(0, QIcon(makeColorSwatch(partColors[i])));
            }
        }
    }

    if (!nodeSets.empty()) {
        nodeSetGroupItem_ = new QTreeWidgetItem(rootItem_, {"节点集"});
        nodeSetGroupItem_->setCheckState(0, Qt::Checked);
        nodeSetGroupItem_->setFlags(nodeSetGroupItem_->flags() | Qt::ItemIsUserCheckable |
                                    Qt::ItemIsEnabled);
        setItemMeta(nodeSetGroupItem_, NodeSetGroupItem);
        for (int i = 0; i < static_cast<int>(nodeSets.size()); ++i) {
            auto* item = new QTreeWidgetItem(nodeSetGroupItem_,
                                             {countLabel(nodeSets[i].name, nodeSets[i].nodeIds.size())});
            item->setCheckState(0, Qt::Checked);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            setItemMeta(item, NodeSetItem, i);
        }
    }

    if (!elementSets.empty()) {
        elementSetGroupItem_ = new QTreeWidgetItem(rootItem_, {"单元集"});
        elementSetGroupItem_->setCheckState(0, Qt::Checked);
        elementSetGroupItem_->setFlags(elementSetGroupItem_->flags() | Qt::ItemIsUserCheckable |
                                       Qt::ItemIsEnabled);
        setItemMeta(elementSetGroupItem_, ElementSetGroupItem);
        for (int i = 0; i < static_cast<int>(elementSets.size()); ++i) {
            auto* item = new QTreeWidgetItem(elementSetGroupItem_,
                                             {countLabel(elementSets[i].name, elementSets[i].elementIds.size())});
            item->setCheckState(0, Qt::Checked);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            setItemMeta(item, ElementSetItem, i);
        }
    }

    tree_->expandAll();
    updating_ = false;
}

void PartsPanel::onItemChanged(QTreeWidgetItem* item, int /*column*/) {
    if (updating_) return;
    updating_ = true;

    Qt::CheckState state = item->checkState(0);
    if (state == Qt::PartiallyChecked) {
        updating_ = false;
        return;
    }

    ItemKind kind = itemKind(item);
    if (kind == ModelRootItem || kind == PartGroupItem ||
        kind == NodeSetGroupItem || kind == ElementSetGroupItem) {
        setChildrenCheckState(item, state);
        updateParentCheckStates(item);
    } else {
        emitLeafVisibility(item, state == Qt::Checked);
        updateParentCheckStates(item);
    }

    updating_ = false;
}

void PartsPanel::onSelectionChanged() {
    if (updating_) return;

    QList<QTreeWidgetItem*> selectedItems = tree_->selectedItems();
    if (selectedItems.isEmpty()) {
        emit partSelectionChanged({});
        return;
    }

    QTreeWidgetItem* current = tree_->currentItem();
    if (!current) current = selectedItems.front();

    ItemKind currentKind = itemKind(current);
    if (currentKind == NodeSetGroupItem) {
        std::vector<int> ids;
        for (const auto& set : nodeSets_)
            ids.insert(ids.end(), set.nodeIds.begin(), set.nodeIds.end());
        ids = sortedUnique(ids);
        if (!ids.empty())
            emit setSelectionRequested(PickMode::Node, ids);
        return;
    }

    if (currentKind == ElementSetGroupItem) {
        std::vector<int> ids;
        for (const auto& set : elementSets_)
            ids.insert(ids.end(), set.elementIds.begin(), set.elementIds.end());
        ids = sortedUnique(ids);
        if (!ids.empty())
            emit setSelectionRequested(PickMode::Element, ids);
        return;
    }

    if (currentKind == NodeSetItem) {
        std::vector<int> ids;
        for (auto* item : selectedItems) {
            if (itemKind(item) != NodeSetItem) continue;
            int idx = item->data(0, kIndexRole).toInt();
            if (idx < 0 || idx >= static_cast<int>(nodeSets_.size())) continue;
            ids.insert(ids.end(), nodeSets_[idx].nodeIds.begin(), nodeSets_[idx].nodeIds.end());
        }
        ids = sortedUnique(ids);
        if (!ids.empty())
            emit setSelectionRequested(PickMode::Node, ids);
        return;
    }

    if (currentKind == ElementSetItem) {
        std::vector<int> ids;
        for (auto* item : selectedItems) {
            if (itemKind(item) != ElementSetItem) continue;
            int idx = item->data(0, kIndexRole).toInt();
            if (idx < 0 || idx >= static_cast<int>(elementSets_.size())) continue;
            ids.insert(ids.end(), elementSets_[idx].elementIds.begin(), elementSets_[idx].elementIds.end());
        }
        ids = sortedUnique(ids);
        if (!ids.empty())
            emit setSelectionRequested(PickMode::Element, ids);
        return;
    }

    std::vector<int> selectedParts;
    for (auto* item : selectedItems) {
        if (itemKind(item) != PartItem) continue;
        selectedParts.push_back(item->data(0, kIndexRole).toInt());
    }
    selectedParts = sortedUnique(selectedParts);
    if (!selectedParts.empty())
        emit partSelectionChanged(selectedParts);
}

void PartsPanel::selectParts(const std::vector<int>& partIndices) {
    if (!partGroupItem_) return;
    QSignalBlocker treeSignalBlocker(tree_);
    updating_ = true;

    // 构建快速查找集合
    std::set<int> indexSet(partIndices.begin(), partIndices.end());

    tree_->clearSelection();
    for (int i = 0; i < partGroupItem_->childCount(); ++i) {
        QTreeWidgetItem* child = partGroupItem_->child(i);
        int partIndex = child->data(0, kIndexRole).toInt();
        child->setSelected(indexSet.count(partIndex) > 0);
    }

    // 确保选中项可见
    if (!partIndices.empty()) {
        for (int i = 0; i < partGroupItem_->childCount(); ++i) {
            QTreeWidgetItem* child = partGroupItem_->child(i);
            if (child->isSelected()) {
                tree_->scrollToItem(child);
                break;
            }
        }
    }

    updating_ = false;
}

void PartsPanel::emitLeafVisibility(QTreeWidgetItem* item, bool visible) {
    ItemKind kind = itemKind(item);
    int idx = item->data(0, kIndexRole).toInt();
    if (kind == PartItem) {
        if (idx >= 0 && idx < static_cast<int>(parts_.size()))
            emit partVisibilityChanged(idx, visible);
    } else if (kind == NodeSetItem) {
        if (idx >= 0 && idx < static_cast<int>(nodeSets_.size()))
            emit setVisibilityRequested(PickMode::Node, nodeSets_[idx].nodeIds, visible);
    } else if (kind == ElementSetItem) {
        if (idx >= 0 && idx < static_cast<int>(elementSets_.size()))
            emit setVisibilityRequested(PickMode::Element, elementSets_[idx].elementIds, visible);
    }
}

void PartsPanel::setChildrenCheckState(QTreeWidgetItem* item, Qt::CheckState state) {
    if (!item) return;
    const bool visible = (state == Qt::Checked);
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem* child = item->child(i);
        child->setCheckState(0, state);
        ItemKind kind = itemKind(child);
        if (kind == ModelRootItem || kind == PartGroupItem ||
            kind == NodeSetGroupItem || kind == ElementSetGroupItem) {
            setChildrenCheckState(child, state);
        } else {
            emitLeafVisibility(child, visible);
        }
    }
}

void PartsPanel::updateParentCheckStates(QTreeWidgetItem* item) {
    QTreeWidgetItem* parent = item ? item->parent() : nullptr;
    while (parent) {
        int checkedCount = 0;
        int uncheckedCount = 0;
        for (int i = 0; i < parent->childCount(); ++i) {
            Qt::CheckState state = parent->child(i)->checkState(0);
            if (state == Qt::Checked) ++checkedCount;
            else if (state == Qt::Unchecked) ++uncheckedCount;
            else {
                ++checkedCount;
                ++uncheckedCount;
            }
        }

        if (uncheckedCount == 0)
            parent->setCheckState(0, Qt::Checked);
        else if (checkedCount == 0)
            parent->setCheckState(0, Qt::Unchecked);
        else
            parent->setCheckState(0, Qt::PartiallyChecked);

        parent = parent->parent();
    }
}
