/**
 * @file ResultPanel.h
 * @brief 右侧结果面板声明
 *
 * 提供工况/类型/分量/色谱的级联选择，控制云图显示。
 */

#pragma once

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>

#include "FEResultData.h"
#include "FEField.h"

class ResultPanel : public QWidget {
    Q_OBJECT

public:
    explicit ResultPanel(QWidget* parent = nullptr);

    /** @brief 设置结果数据（来自 OP2 解析） */
    void setResults(const FEResultData& results);

    /** @brief 清空面板 */
    void clearResults();

signals:
    /** @brief 用户点击应用，发射标量场和标题 */
    void applyResult(const FEScalarField& field, const QString& title);

    /** @brief 用户点击清除 */
    void clearResult();

private slots:
    void onSubcaseChanged(int index);
    void onTypeChanged(int index);
    void onComponentChanged(int index);
    void onApplyClicked();
    void onClearClicked();

private:
    void setupUI();
    void refreshTypes();
    void refreshComponents();

    FEResultData results_;

    QComboBox* subcaseCombo_   = nullptr;
    QComboBox* typeCombo_      = nullptr;
    QComboBox* componentCombo_ = nullptr;
    QComboBox* colormapCombo_  = nullptr;
    QPushButton* applyBtn_     = nullptr;
    QPushButton* clearBtn_     = nullptr;
    QLabel* infoLabel_         = nullptr;
};
