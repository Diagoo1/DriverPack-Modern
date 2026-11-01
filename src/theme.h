#ifndef SDIOMODERN_THEME_H
#define SDIOMODERN_THEME_H
#include <QString>
#include <QColor>
#include <QJsonObject>
class QWidget;
namespace Theme {
bool isDark(const QWidget *widget);
QString styleSheet(bool dark);
QJsonObject defaultDocument();
QJsonObject document();
QJsonObject readPack(const QString &path);
void validate(const QJsonObject &pack);
void setDocument(const QJsonObject &pack);
void writePack(const QString &path,const QJsonObject &pack);
QColor color(const char *role,bool dark=false);
}
#endif
