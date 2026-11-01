#include "portable_library.h"
#include "../native/driverpack_catalog.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QCryptographicHash>
#include <QStorageInfo>
#include <stdexcept>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
namespace PortableLibrary {
QVector<Entry> entries(const QString &program,const QString &drivers,const QString &indexes,const QStringList &needed,bool onlyNeeded){
 const QString database=QDir(indexes).filePath(QStringLiteral("db.sqlite"));driverpack::Catalog validate(database);
 if(!QFileInfo(program).isFile())throw std::runtime_error("The application executable is unavailable.");
 QVector<Entry> result;result.append(Entry{program,QStringLiteral("DriverPackModern.exe")});result.append(Entry{database,QStringLiteral("index/db.sqlite")});
 const QDir packages(drivers);for(const QString &name:packages.entryList(QStringList()<<QStringLiteral("DP_*.7z"),QDir::Files|QDir::NoSymLinks))
  if(!onlyNeeded||needed.contains(name,Qt::CaseInsensitive))result.append(Entry{packages.filePath(name),QStringLiteral("drivers/")+name});
 if(onlyNeeded&&result.size()==2)throw std::runtime_error("No selected driver packages are present in this library.");
 return result;
}

QVector<Entry> softwareEntries(const QVector<DriverPackSoftware::Program> &programs,const QString &catalogFile){
 QVector<Entry> files;QSet<QString> seen;QJsonArray records;
 for(const auto &program:programs){if(program.excluded||program.localPath.isEmpty())continue;const QFileInfo source(program.localPath);if(!source.isFile()||source.isSymLink())continue;const QString name=source.fileName();if(!name.endsWith(QStringLiteral(".exe"),Qt::CaseInsensitive)||name.startsWith(QStringLiteral("downloader"),Qt::CaseInsensitive))continue;
  if(!seen.contains(name.toCaseFolded())){seen.insert(name.toCaseFolded());files.append(Entry{source.canonicalFilePath(),QStringLiteral("programs/")+name});}
  QJsonObject row;row.insert(QStringLiteral("id"),program.id);row.insert(QStringLiteral("name"),program.name);row.insert(QStringLiteral("file"),name);if(!program.url.isEmpty())row.insert(QStringLiteral("url"),program.url);row.insert(QStringLiteral("version"),program.version);row.insert(QStringLiteral("publisher"),program.publisher);row.insert(QStringLiteral("category"),program.category);row.insert(QStringLiteral("registry"),program.registry);records.append(row);
 }
 if(files.isEmpty())throw std::runtime_error("No eligible local software packages are available.");
 QJsonObject catalog;catalog.insert(QStringLiteral("format"),QStringLiteral("DriverPackModernSoftwareCatalog"));catalog.insert(QStringLiteral("schema"),1);catalog.insert(QStringLiteral("programs"),records);QSaveFile out(catalogFile);const QByteArray bytes=QJsonDocument(catalog).toJson();if(!out.open(QIODevice::WriteOnly)||out.write(bytes)!=bytes.size()||!out.commit())throw std::runtime_error("Cannot prepare the portable software catalog.");files.append(Entry{catalogFile,QStringLiteral("programs/catalog.json")});return files;
}
static void fail(const char *message){throw std::runtime_error(message);}
void copy(const QVector<Entry> &files,const QString &destination,const std::atomic<bool> &stop,const std::function<void(qint64,qint64)> &progress){
    if(files.isEmpty())fail("No library files were selected.");
    if(QFileInfo(destination).isSymLink() || !QDir().mkpath(destination))fail("Choose a regular writable destination.");
    QDir root(destination);const QString canonical=QFileInfo(destination).canonicalFilePath();qint64 total=0,done=0;
    for(const Entry &e:files){const QString path=QDir::cleanPath(e.relative);
        if(path!=e.relative || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char(':')) || path.contains(QLatin1Char('\\')) || path==QStringLiteral("..") || path.startsWith(QStringLiteral("../")))fail("Invalid portable library path.");
        if(!QFileInfo(e.source).isFile() || QFileInfo(e.source).isSymLink())fail("A source file is unavailable.");total+=QFileInfo(e.source).size();}
    QStorageInfo volume(destination);if(volume.isValid() && volume.isReady() && volume.bytesAvailable()<total+32*1024*1024)fail("The destination needs more free space.");
    for(const Entry &e:files){
        if(stop.load())fail("Portable copy cancelled. Completed files were kept.");
        const QString target=root.filePath(e.relative);QFileInfo info(target);if(info.isSymLink())fail("Cannot prepare the destination.");
        QString current=canonical;const QStringList parts=e.relative.split(QLatin1Char('/'));
        for(int i=0;i<parts.size()-1;++i){current=QDir(current).filePath(parts[i]);if(QFileInfo(current).isSymLink())fail("Destination contains a symbolic link.");if(!QDir().mkpath(current))fail("Cannot create a destination folder.");}
        const QString parent=QFileInfo(info.absolutePath()).canonicalFilePath();if(parent.compare(canonical,Qt::CaseInsensitive)!=0 && !parent.startsWith(canonical+QLatin1Char('/'),Qt::CaseInsensitive))fail("Destination leaves the portable folder.");
        if(QFileInfo(e.source).canonicalFilePath().compare(info.canonicalFilePath(),Qt::CaseInsensitive)==0)fail("Source and destination must be different.");
        QFile source(e.source);QSaveFile output(target);QCryptographicHash original(QCryptographicHash::Sha256);
        if(!source.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))fail("Cannot open a library file.");
        while(!source.atEnd()){if(stop.load())fail("Portable copy cancelled. Completed files were kept.");const QByteArray bytes=source.read(1024*1024);if(bytes.isEmpty() && source.error()!=QFile::NoError)fail("Source read failed.");if(output.write(bytes)!=bytes.size())fail("Destination write failed.");original.addData(bytes);done+=bytes.size();if(progress)progress(done,total);}
        if(!output.commit())fail("Cannot finish the portable file.");
        QFile result(target);QCryptographicHash verified(QCryptographicHash::Sha256);if(!result.open(QIODevice::ReadOnly))fail("Cannot verify the copied file.");
        while(!result.atEnd()){if(stop.load())fail("Portable copy cancelled. Completed files were kept.");const QByteArray bytes=result.read(1024*1024);if(bytes.isEmpty() && result.error()!=QFile::NoError)fail("Verification read failed.");verified.addData(bytes);}
        if(original.result()!=verified.result())fail("A copied file failed verification.");
    }
}
}
