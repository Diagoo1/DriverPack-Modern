#include "../src/online_packages.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <cstdio>
#include <stdexcept>
static int checks=0;static void check(bool v,const char*n){if(!v)throw std::runtime_error(n);++checks;}
static QByteArray read(const QString&path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("fixture read");return f.readAll();}
static void write(const QString&path,const QByteArray&data){QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())throw std::runtime_error("fixture write");}
template<class F>static bool rejected(F f){try{f();return false;}catch(const std::exception&){return true;}}
namespace OnlinePackages {void shareVerified(const Catalog&,const QString&,bool){} }
int main(int argc,char**argv){QCoreApplication app(argc,argv);try{
 check(OnlinePackages::packageCategory(QStringLiteral("DP_Video_AMD_20045.7z"))==QStringLiteral("Video"),"video category uses actual package names");
 check(OnlinePackages::packageCategory(QStringLiteral("DP_WLAN_Intel_20045.7z"))==QStringLiteral("Wi-Fi"),"Wi-Fi distinguished from wired network");
 check(OnlinePackages::packageCategory(QStringLiteral("DP_LAN_Realtek_20045.7z"))==QStringLiteral("Network"),"wired network category");
 check(OnlinePackages::packageCategory(QStringLiteral("db.sqlite"))==QStringLiteral("Indexes"),"DriverPack index category");
 const QString fixtures=app.arguments().value(1);QTemporaryDir work;check(work.isValid(),"temporary output");
 const QString drivers=work.path()+QStringLiteral("/drivers"),indexes=work.path()+QStringLiteral("/index"),cache=work.path()+QStringLiteral("/cache");QDir().mkpath(indexes);
 auto c=OnlinePackages::parse(read(fixtures+QStringLiteral("/valid.torrent")));check(c.files.size()==3,"all catalog files classified");check(c.files[0].kind==OnlinePackages::Index,"SQLite index recognized");check(OnlinePackages::containsDatabase(c),"coherent catalog contains a DriverPack SQLite database");auto archiveOnly=c;archiveOnly.files.remove(0);check(!OnlinePackages::containsDatabase(archiveOnly),"archive-only feed must not imply a database update");check(c.files[1].kind==OnlinePackages::Driver,"7z driver recognized");check(c.files[2].kind==OnlinePackages::Other,"bundled software excluded");
 check(OnlinePackages::requiredBytes(c,QVector<int>())==0,"empty selection has zero summary bytes");
 std::atomic<bool> stop(false);auto payload=read(fixtures+QStringLiteral("/valid.payload"));
 auto fetch=[&](const QUrl&url,qint64 start,qint64 count,qint64){for(const auto &file:c.files)if(url.path().endsWith(file.path))return payload.mid(int(file.offset+start),int(count));throw std::runtime_error("unexpected fixture URL");return QByteArray();};
 check(rejected([&]{OnlinePackages::download(c,QVector<int>(),drivers,indexes,cache,stop,OnlinePackages::Sink(),fetch);}),"empty download remains rejected without fetching");
 check(rejected([&]{OnlinePackages::download(c,QVector<int>()<<2,drivers,indexes,cache,stop,OnlinePackages::Sink(),fetch);}),"software cannot be selected for publication");
 const QString target=indexes+QStringLiteral("/db.sqlite");write(target,QByteArray("original-index"));
 auto saved=OnlinePackages::download(c,QVector<int>()<<0,drivers,indexes,cache,stop,OnlinePackages::Sink(),fetch);check(saved.size()==1&&read(target)==read(fixtures+QStringLiteral("/catalog.sqlite")),"verified valid index published atomically");
 check(!QFile::exists(drivers+QStringLiteral("/ad.exe")),"no software output");
 const QByteArray good=read(target);auto bad=OnlinePackages::parse(read(fixtures+QStringLiteral("/invalid.torrent")));auto badPayload=read(fixtures+QStringLiteral("/invalid.payload"));
 auto badFetch=[&](const QUrl&url,qint64 start,qint64 count,qint64){for(const auto &file:bad.files)if(url.path().endsWith(file.path))return badPayload.mid(int(file.offset+start),int(count));throw std::runtime_error("unexpected fixture URL");return QByteArray();};
 check(rejected([&]{OnlinePackages::download(bad,QVector<int>()<<0,drivers,indexes,cache,stop,OnlinePackages::Sink(),badFetch);}),"unsupported SQLite schema rejected after verified transfer");check(read(target)==good,"existing index preserved on invalid schema");
 auto corrupt=[&](const QUrl&,qint64,qint64 count,qint64){return QByteArray(int(count),'X');};
 check(rejected([&]{OnlinePackages::download(c,QVector<int>()<<0,drivers,indexes,cache,stop,OnlinePackages::Sink(),corrupt);}),"corrupt blocks rejected");check(read(target)==good,"existing index preserved on hash failure");
 const QString pauseCache=work.path()+QStringLiteral("/pause-cache");
 stop.store(false);bool stoppedAfterBlock=false;
 auto pauseSink=[&](const OnlinePackages::Progress &progress){if(progress.verified>0){stoppedAfterBlock=true;stop.store(true);}};
 check(rejected([&]{OnlinePackages::download(c,QVector<int>()<<0,drivers,indexes,pauseCache,stop,pauseSink,fetch);})&&stoppedAfterBlock,"pause after a verified block stops before publication");
 check(read(target)==good,"pause preserves the existing index");
 stop.store(false);qint64 fetchedOnResume=0;
 auto resumeFetch=[&](const QUrl&url,qint64 start,qint64 count,qint64 total){fetchedOnResume+=count;return fetch(url,start,count,total);};
 auto resumed=OnlinePackages::download(c,QVector<int>()<<0,drivers,indexes,pauseCache,stop,OnlinePackages::Sink(),resumeFetch);
 check(resumed.size()==1&&read(target)==good&&fetchedOnResume<payload.size(),"resume reuses verified blocks and publishes the same validated data");
 stop.store(true);check(rejected([&]{OnlinePackages::download(c,QVector<int>()<<0,drivers,indexes,cache,stop,OnlinePackages::Sink(),fetch);}),"cancelled transfer cannot publish");
 printf("%d offline transfer checks passed; zero network requests.\n",checks);return 0;
}catch(const std::exception&e){fprintf(stderr,"Transfer test failed: %s\n",e.what());return 1;}}
