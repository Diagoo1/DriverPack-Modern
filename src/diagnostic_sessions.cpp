// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagnostic_sessions.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegExp>
#include <QSet>
#include <QVector>
#include <QLockFile>
namespace {
const QByteArray marker("DriverPack Modern diagnostic session v1\n");
struct Session {QString path;bool crashed=false,keep=false; qint64 bytes=0;QStringList files;};
bool inspect(const QString &root,const QFileInfo &entry,Session &session) {
    if(entry.isSymLink() || !entry.isDir() || entry.canonicalPath()!=QFileInfo(root).canonicalFilePath())return false;
    if(!QRegExp(QStringLiteral("session-[0-9]{8}-[0-9]{6}-[0-9]{3}-[0-9]+")).exactMatch(entry.fileName()))return false;
    QDir dir(entry.absoluteFilePath());QFile tag(dir.filePath(QStringLiteral(".driverpack-session")));
    if(!tag.open(QIODevice::ReadOnly) || tag.read(marker.size()+1)!=marker)return false;
    const QSet<QString> allowed={QStringLiteral(".driverpack-session"),QStringLiteral("active.lock"),QStringLiteral("session.txt"),QStringLiteral("crash.dmp"),QStringLiteral("native-events.jsonl"),QStringLiteral("native-events.previous.jsonl")};
    for(const auto &file:dir.entryInfoList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot)){
        if(file.isSymLink() || !file.isFile() || !allowed.contains(file.fileName()))return false;
        session.files<<file.absoluteFilePath();session.bytes+=file.size();
        if(file.fileName()==QStringLiteral("crash.dmp"))session.crashed=true;
    }
    QFile text(dir.filePath(QStringLiteral("session.txt")));
    if(!session.crashed && text.open(QIODevice::ReadOnly)){text.seek(qMax(qint64(0),text.size()-4096));session.crashed=text.readAll().contains("FATAL");}
    session.path=entry.absoluteFilePath();return true;
}
}
QString DiagnosticSessions::create(const QString &root){
    QDir dir(root);if(!dir.exists()&&!dir.mkpath(QStringLiteral(".")))return QString();
    const QString stem=QStringLiteral("session-%1-%2").arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))).arg(QCoreApplication::applicationPid());
    if(!dir.mkdir(stem))return QString();
    const QString result=dir.filePath(stem);QFile file(QDir(result).filePath(QStringLiteral(".driverpack-session")));
    if(!file.open(QIODevice::WriteOnly) || file.write(marker)!=marker.size())return QString();
    return QFileInfo(result).absoluteFilePath();
}
void DiagnosticSessions::prune(const QString &root,const QString &current,int normalLimit,int crashLimit,qint64 budget){
    if(normalLimit<1 || crashLimit<1 || budget<0)return;
    QVector<Session> sessions;int normal=0,crash=0;qint64 bytes=0;int latestCrash=-1;
    const QString currentPath=QFileInfo(current).canonicalFilePath();
    for(const auto &entry:QDir(root).entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name|QDir::Reversed)){
        Session s;if(!inspect(root,entry,s))continue;
        const bool active=entry.canonicalFilePath()==currentPath;
        if(s.crashed){if(latestCrash<0)latestCrash=sessions.size();s.keep=++crash<=crashLimit;}
        else s.keep=++normal<=normalLimit;
        if(active)s.keep=true;
        if(s.keep)bytes+=s.bytes;sessions<<s;
    }
    // Keep the current session and newest crash even if one dump alone is large.
    for(int i=sessions.size()-1;i>=0 && bytes>budget;--i){auto &s=sessions[i];
        if(s.keep && QFileInfo(s.path).canonicalFilePath()!=currentPath && i!=latestCrash){s.keep=false;bytes-=s.bytes;}}
    for(const auto &s:sessions)if(!s.keep && QFileInfo(s.path).canonicalFilePath()!=currentPath){
        // No recursive delete: never touch unrecognized files, links or subfolders.
        Session checked;if(!inspect(root,QFileInfo(s.path),checked))continue;
        QLockFile lock(QDir(s.path).filePath(QStringLiteral("active.lock")));lock.setStaleLockTime(0);
        if(!lock.tryLock(0))continue;
        bool clean=true;for(const auto &file:checked.files)if(QFileInfo(file).fileName()!=QStringLiteral("active.lock")&&QFileInfo(file).fileName()!=QStringLiteral(".driverpack-session"))clean=QFile::remove(file)&&clean;
        if(clean)QFile::remove(QDir(s.path).filePath(QStringLiteral(".driverpack-session")));
        lock.unlock();
        QDir(root).rmdir(QFileInfo(s.path).fileName());
    }
}
int DiagnosticSessions::clearFinished(const QString &root,const QString &current){
    int removed=0;bool keptCrash=false;const QString active=QFileInfo(current).canonicalFilePath();
    for(const auto &entry:QDir(root).entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name|QDir::Reversed)){
        Session session;if(!inspect(root,entry,session))continue;
        if(entry.canonicalFilePath()==active)continue;
        if(session.crashed&&!keptCrash){keptCrash=true;continue;}
        QLockFile lock(QDir(session.path).filePath(QStringLiteral("active.lock")));lock.setStaleLockTime(0);if(!lock.tryLock(0))continue;
        bool ok=true;for(const auto &file:session.files)if(QFileInfo(file).fileName()!=QStringLiteral("active.lock")&&QFileInfo(file).fileName()!=QStringLiteral(".driverpack-session"))ok=QFile::remove(file)&&ok;
        if(ok)ok=QFile::remove(QDir(session.path).filePath(QStringLiteral(".driverpack-session")));
        lock.unlock();if(ok&&QDir(root).rmdir(entry.fileName()))++removed;
    }
    return removed;
}
