#ifndef SDIO_SUPPORT_REPORT_H
#define SDIO_SUPPORT_REPORT_H
#include <QJsonObject>
#include <QString>
#include <QUrl>
class QWidget;
#include "driver_catalog.h"
QJsonObject supportReport(const DriverCatalog &catalog,const sdiomodern::InstallReport &installation,const QString &lastError);
namespace BugReport {
QString environment(const QString &windows,const QString &architecture,const QString &language,bool dark);
QUrl issueUrl(const QString &environment);
void show(QWidget *parent);
}
#endif
