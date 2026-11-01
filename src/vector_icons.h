#ifndef SDIOMODERN_VECTOR_ICONS_H
#define SDIOMODERN_VECTOR_ICONS_H

#include <QColor>
#include <QIcon>
#include <QString>
#include <QRectF>
class QPainter;

// Small, original, consistent SVG outline icons embedded in the Qt resource file.
// Rendering uses QtSvg (not a browser, HTML renderer, or external runtime).
namespace VectorIcons {
    QIcon icon(const QString &name, const QColor &color, int logicalSize = 24);
    QPixmap pixmap(const QString &name,const QColor &color,int logicalSize=24);
    void paint(QPainter *painter, const QString &name, const QRectF &rect, const QColor &color);
    QString forDeviceClass(const QString &deviceClass);
    QString deviceCategory(const QString &deviceClass,const QString &name);
}

#endif
