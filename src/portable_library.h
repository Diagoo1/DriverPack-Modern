#ifndef SDIO_PORTABLE_LIBRARY_H
#define SDIO_PORTABLE_LIBRARY_H
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>
#include "software_catalog.h"
class QWidget;
namespace PortableLibrary {
struct Entry {QString source,relative;};
QVector<Entry> entries(const QString &program,const QString &drivers,const QString &indexes,const QStringList &needed,bool onlyNeeded);
QVector<Entry> softwareEntries(const QVector<DriverPackSoftware::Program>&,const QString &catalogFile);
void copy(const QVector<Entry>&,const QString&,const std::atomic<bool>&,const std::function<void(qint64,qint64)>&);
}
void openPortableLibrary(QWidget*,const QString&,const QString&,const QStringList&,bool);
#endif
