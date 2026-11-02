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
 const QByteArray hash=QByteArray::fromHex("0080ff26203f4142434445464748494a4b4c4d4e"),peerId("-DM0222-123456789012");
 const QUrl announce=OnlinePackages::trackerAnnounceUrl(QUrl(QStringLiteral("https://tracker.example/announce?key=fixture")),hash,peerId,6881,12345);
 const QByteArray query=announce.query(QUrl::FullyEncoded).toLatin1();const int start=query.indexOf("info_hash=")+10;const int end=query.indexOf('&',start);
 check(QByteArray::fromPercentEncoding(query.mid(start,end-start))==hash,"tracker info hash is byte-exact, including binary and reserved characters");
 check(query.startsWith("key=fixture&") && query.contains("&left=12345") && query.contains("&compact=1"),"tracker request preserves catalog query and correct download parameters");
 check(rejected([&](){OnlinePackages::trackerAnnounceUrl(QUrl(QStringLiteral("https://user:secret@tracker.example/announce")),hash,peerId,6881,1);}),"tracker credentials are rejected");
 check(rejected([&](){OnlinePackages::trackerAnnounceUrl(QUrl(QStringLiteral("https://tracker.example/announce")),hash,peerId,0,1);}),"invalid tracker listen port rejected");
 QByteArray compact=QByteArray::fromHex("c633640f1ae17f0000010050c0a801010050");
 const QByteArray response=QByteArray("d5:peers")+QByteArray::number(compact.size())+":"+compact+"e";
 const auto endpoints=OnlinePackages::parseTrackerPeers(response);
 check(endpoints.size()==1 && endpoints[0].address==QStringLiteral("198.51.100.15") && endpoints[0].port==6881,"compact IPv4 peers decoded; loopback and LAN destinations excluded");
 check(rejected([](){OnlinePackages::parseTrackerPeers(QByteArray("d14:failure reason4:teste"));}),"tracker failure is not treated as a working empty response");
 check(rejected([](){OnlinePackages::parseTrackerPeers(QByteArray("d5:peers1:xe"));}),"misaligned compact peer records rejected");
 check(rejected([](){OnlinePackages::parseTrackerPeers(QByteArray(256*1024+1,'x'));}),"tracker response memory bounded");
 check(OnlinePackages::parseTrackerPeers(QByteArray("d5:peers0:e")).isEmpty(),"valid tracker with no seeds remains an explicit empty peer result");
 const QString fixtures=app.arguments().value(1);QTemporaryDir work;check(work.isValid(),"temporary output");
 const QString drivers=work.path()+QStringLiteral("/drivers"),indexes=work.path()+QStringLiteral("/index"),cache=work.path()+QStringLiteral("/cache");QDir().mkpath(indexes);
 const auto encode=[](const QByteArray &v){return QByteArray::number(v.size())+":"+v;};
 const QByteArray trackerMeta=QByteArray("d8:announce")+encode("https://tracker.example/announce")+"13:announce-listll"+encode("https://tracker.example/announce")+encode("udp://tracker.example:80/announce")+"ee"+read(fixtures+QStringLiteral("/valid.torrent")).mid(1);
 const auto tracked=OnlinePackages::parse(trackerMeta);
 check(tracked.trackers.size()==2,"catalog tracker tiers parsed and deduplicated without losing HTTPS discovery");
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
