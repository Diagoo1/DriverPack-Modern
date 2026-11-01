#ifndef DRIVERPACK_MODERN_OFFLINE_PATHS_H
#define DRIVERPACK_MODERN_OFFLINE_PATHS_H
#include <QString>
#include <QDir>
#include <QFileInfo>
namespace DriverPackLibrary
{
struct Location
{
    QString drivers, database;
    bool valid() const { return !drivers.isEmpty() && !database.isEmpty(); }
};
inline Location locate(const QString &input)
{
    const QFileInfo info(input);
    QString base = info.isFile() ? info.absolutePath() : info.absoluteFilePath();
    QStringList roots;
    roots << base << QFileInfo(base).absolutePath();
    for (const QString &root : roots)
    {
        const QString drivers = QDir(root).filePath(QStringLiteral("drivers"));
        for (const QString &index : QStringList({QStringLiteral("index"), QStringLiteral("indexes")}))
        {
            const QFileInfo db(QDir(root).filePath(index + QStringLiteral("/db.sqlite")));
            if (QDir(drivers).exists() && db.isFile() && !db.isSymLink())
                return Location{QFileInfo(drivers).canonicalFilePath(), db.canonicalFilePath()};
        }
    }
    return Location();
}
} // namespace DriverPackLibrary
inline QString libraryFamily(const QString &, const QString &) { return QStringLiteral("DriverPack"); }
inline QString resolveIndexDirectory(const QString &path, const QString &drivers = QString())
{
    if (QFileInfo(path).isFile())
        return QFileInfo(path).absolutePath();
    if (QFileInfo(QDir(path).filePath(QStringLiteral("db.sqlite"))).isFile())
        return QFileInfo(path).absoluteFilePath();
    QStringList roots;
    roots << path << QFileInfo(drivers).absolutePath();
    for (const QString &root : roots)
    {
        const QStringList aliases = QStringList() << QStringLiteral("index") << QStringLiteral("indexes")
                                                  << QStringLiteral("Index");
        for (const QString &alias : aliases)
        {
            const QString directory = QDir(root).filePath(alias);
            if (QFileInfo(QDir(directory).filePath(QStringLiteral("db.sqlite"))).isFile())
                return directory;
        }
    }
    return path;
}
inline QString prepareLibraryIndexes(const QString &drivers, const QString &indexes)
{
    return resolveIndexDirectory(indexes, drivers);
}
#endif
