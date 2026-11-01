#pragma once
#include <QString>
#include <QVector>
#include <QJsonObject>
class QWidget;
namespace ActivityLog
{
QVector<QJsonObject> readRecent(const QString &path, int maxRows = 1500, qint64 maxBytes = 2 * 1024 * 1024);
void show(QWidget *parent, const QString &library, bool arabic, bool preview = false);
} // namespace ActivityLog
