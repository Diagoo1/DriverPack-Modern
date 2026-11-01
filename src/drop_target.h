#ifndef SDIO_DROP_TARGET_H
#define SDIO_DROP_TARGET_H
#include <QString>
#include <QList>
#include <QUrl>
namespace DropTarget {
enum Kind { Rejected,DriverFolder,DatabaseFile };
Kind classify(const QList<QUrl> &urls,QString *path=0);
}
#endif
