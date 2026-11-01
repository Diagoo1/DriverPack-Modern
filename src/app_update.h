#ifndef SDIO_APP_UPDATE_H
#define SDIO_APP_UPDATE_H
#include <QString>
#include <QJsonObject>
#include <atomic>
class QWidget;
namespace AppUpdate {
QString projectRepository();
struct Release {QString version,url,page;QByteArray sha256;qint64 size=0;};
Release latest(const QString&,const std::atomic<bool>&);
void downloadVerified(const Release&,const QString&,const std::atomic<bool>&);
bool validRepository(const QString&);
Release parseRelease(const QJsonObject&,const QString &repository);
bool newer(const QString&,const QString&);
void replaceVerified(const QString &target,const QString &source,const QByteArray &sha256);
void check(QWidget*,const QString &repository);
int apply(int argc,char **argv);
}
#endif
