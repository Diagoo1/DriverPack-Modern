// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/diagnostic_sessions.h"
#include "../src/download_cache_cleanup.h"
#include "../src/software_setup_status.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <cstdio>
#include <stdexcept>
static void write(const QString &name,const QByteArray &data){QFile f(name);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())throw std::runtime_error("fixture write");}
static QString fixture(const QString &root,int n,bool crash=false){
    const QString path=QDir(root).filePath(QStringLiteral("session-20260101-%1-000-%2").arg(n,6,10,QLatin1Char('0')).arg(n));
    QDir().mkpath(path);write(QDir(path).filePath(QStringLiteral(".driverpack-session")),QByteArray("DriverPack Modern diagnostic session v1\n"));
    write(QDir(path).filePath(QStringLiteral("session.txt")),QByteArray("fixture\n"));
    if(crash)write(QDir(path).filePath(QStringLiteral("crash.dmp")),QByteArray(8192,'x'));return path;
}
int main(int argc,char **argv){QCoreApplication app(argc,argv);int checks=0;auto check=[&](bool b,const char *text){if(!b)throw std::runtime_error(text);++checks;};try{
    QTemporaryDir temp;const QString root=temp.path();
    for(int i=1;i<=9;++i)fixture(root,i,false);
    for(int i=11;i<=15;++i)fixture(root,i,true);
    const QString latestCrash=QDir(root).filePath(QStringLiteral("session-20260101-000015-000-15"));
    const QString current=DiagnosticSessions::create(root);check(!current.isEmpty(),"current created");
    DiagnosticSessions::prune(root,current);
    check(QDir(root).entryList(QDir::Dirs|QDir::NoDotAndDotDot).size()==8,"five normal plus three crash sessions");
    check(QFile::exists(QDir(latestCrash).filePath(QStringLiteral("crash.dmp"))),"newest dump retained");
    const QString active=fixture(root,1);QLockFile lock(QDir(active).filePath(QStringLiteral("active.lock")));lock.setStaleLockTime(0);check(lock.tryLock(0),"active lock acquired");
    const QString unknown=fixture(root,2);write(QDir(unknown).filePath(QStringLiteral("user-file.txt")),QByteArray("keep"));
    const QString nested=fixture(root,3);QDir().mkpath(QDir(nested).filePath(QStringLiteral("user-folder")));
    const QString unmarked=fixture(root,4);QFile::remove(QDir(unmarked).filePath(QStringLiteral(".driverpack-session")));
    const QString fatal=fixture(root,10);write(QDir(fatal).filePath(QStringLiteral("session.txt")),QByteArray("FATAL exception, dump could not be written\n"));
    DiagnosticSessions::prune(root,current,1,3,10000);
    check(QDir(active).exists(),"live session never deleted");
    check(QFile::exists(QDir(unknown).filePath(QStringLiteral("user-file.txt"))),"unknown file preserved");
    check(QDir(nested).exists(),"nested folder preserved");check(QDir(unmarked).exists(),"unmarked folder preserved");
    check(QDir(current).exists(),"current preserved under budget pressure");check(QDir(latestCrash).exists(),"newest crash retained under budget pressure");
    lock.unlock();DiagnosticSessions::prune(root,current,1,1,0);check(!QDir(active).exists(),"finished session removed after lock release");
    QTemporaryDir fallback;fixture(fallback.path(),1);const QString onlyCrash=fixture(fallback.path(),2);write(QDir(onlyCrash).filePath(QStringLiteral("session.txt")),QByteArray("FATAL no dump\n"));
    const QString active2=DiagnosticSessions::create(fallback.path());DiagnosticSessions::prune(fallback.path(),active2,1,1,0);
    check(QDir(onlyCrash).exists(),"fatal trace retained even without dump");
    const QString extra=fixture(fallback.path(),3);check(DiagnosticSessions::clearFinished(fallback.path(),active2)==1,"manual clear removes finished ordinary session");
    check(!QDir(extra).exists()&&QDir(onlyCrash).exists()&&QDir(active2).exists(),"manual clear preserves current and latest crash");
    check(SoftwareSetupStatus::success(0)&&SoftwareSetupStatus::success(3010),"success and reboot codes");
    check(SoftwareSetupStatus::cancelled(1602)&&SoftwareSetupStatus::cancelled(1223),"documented cancellation codes");
    check(!SoftwareSetupStatus::cancelled(2)&&!SoftwareSetupStatus::success(2),"code 2 cannot prove cancellation or success");
    QTemporaryDir library;const QString cache=QDir(library.path()).filePath(QStringLiteral("drivers/.driverpack-modern-downloads"));const QString hash=QDir(cache).filePath(QString(40,QLatin1Char('a')));QDir().mkpath(hash);
    write(QDir(cache).filePath(QStringLiteral("catalog.torrent")),QByteArray("catalog"));write(QDir(hash).filePath(QStringLiteral("0.block")),QByteArray("cached piece"));
    write(QDir(hash).filePath(QStringLiteral("user-file.txt")),QByteArray("keep"));const QString archive=QDir(library.path()).filePath(QStringLiteral("drivers/DP_Test.7z"));write(archive,QByteArray("keep archive"));
    auto cleared=DownloadCacheCleanup::clear(library.path());check(cleared.files==2&&cleared.failed==0,"known download cache files removed");
    check(QFile::exists(archive)&&QFile::exists(QDir(hash).filePath(QStringLiteral("user-file.txt"))),"driver archive and unknown cache file preserved");
    printf("%d session-retention checks passed on temporary fixtures only.\n",checks);return 0;
}catch(const std::exception &e){fprintf(stderr,"Session retention after %d: %s\n",checks,e.what());return 1;}}
