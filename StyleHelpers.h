/**
 * @file StyleHelpers.h
 * @brief 通用 Qt 样式片段生成器
 *
 * Qt5 stylesheet 的 `image: url(...)` 接受文件路径或 qrc，但不支持 data: URI；
 * 同时 CSS 三角形技巧（用透明 border 拼三角）也不生效，会渲染成实心方块。
 *
 * 解决方案：用 QPainter 把箭头画成小 PNG 写到临时目录，stylesheet 引用文件路径。
 * 文件按颜色哈希命名，重复调用不重建。
 */

#pragma once

#include <QString>
#include <QImage>
#include <QPainter>
#include <QPolygon>
#include <QStandardPaths>
#include <QDir>
#include <QFile>

namespace StyleHelpers {

inline QString arrowPngPath(const QString& cssColor, bool isDown) {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                  + "/femv_arrows";
    QDir().mkpath(dir);
    QString colorKey = cssColor;
    colorKey.remove('#').remove('%').remove('(').remove(')').remove(',');
    QString name = QString("%1_%2.png").arg(isDown ? "down" : "up", colorKey);
    QString path = QString("%1/%2").arg(dir, name);
    if (QFile::exists(path)) return path;

    QImage img(20, 12, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(QColor(cssColor));
    p.setPen(Qt::NoPen);
    QPolygon poly;
    if (isDown) {
        poly << QPoint(2, 2) << QPoint(18, 2) << QPoint(10, 11);
    } else {
        poly << QPoint(2, 10) << QPoint(18, 10) << QPoint(10, 1);
    }
    p.drawPolygon(poly);
    p.end();
    img.save(path, "PNG");
    return path;
}

/** @brief QComboBox 下拉箭头样式 */
inline QString comboArrowStyle(const QString& arrowColor, const QString& borderColor) {
    QString down = arrowPngPath(arrowColor, true);
    return QString(
        "QComboBox::drop-down { subcontrol-origin: padding;"
        "  subcontrol-position: top right; width: 24px;"
        "  border-left: 1px solid %2; }"
        "QComboBox::down-arrow {"
        "  image: url(\"%1\"); width: 12px; height: 8px; }"
    ).arg(down, borderColor);
}

/** @brief QSpinBox / QDoubleSpinBox 上下箭头样式（同时作用于两种） */
inline QString spinArrowStyle(const QString& arrowColor,
                              const QString& btnBg,
                              const QString& btnHover,
                              const QString& borderColor) {
    QString up = arrowPngPath(arrowColor, false);
    QString down = arrowPngPath(arrowColor, true);
    return QString(
        "QSpinBox::up-button, QDoubleSpinBox::up-button {"
        "  subcontrol-origin: border; subcontrol-position: top right;"
        "  width: 18px; border-left: 1px solid %5; background: %3; }"
        "QSpinBox::down-button, QDoubleSpinBox::down-button {"
        "  subcontrol-origin: border; subcontrol-position: bottom right;"
        "  width: 18px; border-left: 1px solid %5; background: %3; }"
        "QSpinBox::up-button:hover, QSpinBox::down-button:hover,"
        "QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {"
        "  background: %4; }"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {"
        "  image: url(\"%1\"); width: 10px; height: 6px; }"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {"
        "  image: url(\"%2\"); width: 10px; height: 6px; }"
    ).arg(up, down, btnBg, btnHover, borderColor);
}

} // namespace StyleHelpers
