#ifndef DRIVERPACK_SOFTWARE_CATALOG_H
#define DRIVERPACK_SOFTWARE_CATALOG_H
#include <QString>
#include <QVector>
#include <QByteArray>
namespace DriverPackSoftware
{
struct Program
{
    QString sourceId, fileName, origin, id, name, version, publisher, category, url, license, registry,
        localPath, reason, description, icon, policy;
    int rating = 0;
    bool knownPackage = false, toolkit = false, installed = false, excluded = false;
};
struct Installed
{
    QString name, publisher, version;
};
int runReport(int argc, char **argv);
QVector<Program> parse(const QByteArray &source);
QVector<Program> parseJsonCatalog(const QByteArray &source);
QVector<Program> load(const QString &library, bool includeOtherWindows = false, bool persistCatalog = true);
QVector<Program> catalogRecords(const QString &library, bool create = true);
QByteArray catalogBytes(const QVector<Program> &programs);
void writeCatalog(const QString &library, const QVector<Program> &programs);
QString removeProgram(const QString &library, const QString &id, bool removeInstaller = false);
QString categoryFolder(const QString &category);
bool promotionalPackage(const QString &file);
void resolvePackageVariants(QVector<Program> &programs, bool russian);
void saveCustomProgram(const QString &library, const Program &program);
QString iconPath(const QString &library, const Program &program);
QString localInstaller(const QString &library, const QString &url, bool verifyHeader = false);
QString localProgram(const QString &library, const Program &program, bool verifyHeader = false);
QVector<Installed> installedPrograms();
bool allowedUrl(const QString &url);
} // namespace DriverPackSoftware
#endif
