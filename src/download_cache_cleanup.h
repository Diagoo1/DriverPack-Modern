// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DRIVERPACK_DOWNLOAD_CACHE_CLEANUP_H
#define DRIVERPACK_DOWNLOAD_CACHE_CLEANUP_H
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegExp>
namespace DownloadCacheCleanup {
struct Result {int files=0,failed=0;qint64 bytes=0;};
inline Result clear(const QString &library){
    Result result;const QString drivers=QDir(library).filePath(QStringLiteral("drivers"));
    const QFileInfo root(QDir(drivers).filePath(QStringLiteral(".driverpack-modern-downloads")));
    if(root.isSymLink() || !root.isDir() || root.canonicalPath()!=QFileInfo(drivers).canonicalFilePath())return result;
    auto remove=[&](const QFileInfo &file){if(file.isSymLink()||!file.isFile())return;const qint64 size=file.size();if(QFile::remove(file.absoluteFilePath())){++result.files;result.bytes+=size;}else ++result.failed;};
    for(const auto &file:QDir(root.absoluteFilePath()).entryInfoList(QDir::Files|QDir::NoDotAndDotDot))
        if(file.fileName()==QStringLiteral("catalog.torrent")||file.fileName()==QStringLiteral("update-events.jsonl"))remove(file);
    for(const auto &dir:QDir(root.absoluteFilePath()).entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot)){
        if(dir.isSymLink() || dir.canonicalPath()!=root.canonicalFilePath() || !QRegExp(QStringLiteral("[0-9a-f]{40}")).exactMatch(dir.fileName()))continue;
        for(const auto &file:QDir(dir.absoluteFilePath()).entryInfoList(QDir::Files|QDir::NoDotAndDotDot))
            if(QRegExp(QStringLiteral("[0-9]+\\.block")).exactMatch(file.fileName()) || file.fileName()==QStringLiteral("sharing.torrent"))remove(file);
        QDir(root.absoluteFilePath()).rmdir(dir.fileName()); // succeeds only when truly empty
    }
    return result;
}
}
#endif
