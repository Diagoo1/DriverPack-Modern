#include "../native/driverpack_catalog.h"
#include <QTemporaryDir>
#include "online_packages.h"
#include "offline_paths.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStorageInfo>
#include <QElapsedTimer>
#include <QThread>
#include <QtEndian>
#include <algorithm>
#include <limits>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wininet.h>
#endif

namespace OnlinePackages {
static void fail(const QString &text){throw std::runtime_error(text.toUtf8().constData());}
static void cancelled(const std::atomic<bool> &stop){if(stop.load())fail(QStringLiteral("Download paused. Verified blocks were kept for resuming."));}
struct Node { QByteArray text; qint64 number=0; char type=0; QMap<QByteArray,Node> dict; QVector<Node> list; int first=0,last=0; };
class Decoder {
    const QByteArray &data;int pos=0,nodes=0;
public:
    explicit Decoder(const QByteArray &bytes):data(bytes){}
    Node read(int depth=0){
        if(depth>24 || ++nodes>200000 || pos>=data.size())fail(QStringLiteral("Invalid or excessive torrent metadata."));
        Node node;node.first=pos;const char c=data[pos++];
        if(c=='d' || c=='l'){
            node.type=c;
            while(pos<data.size() && data[pos]!='e'){
                if(c=='d'){Node key=read(depth+1);if(key.type!='s' || node.dict.contains(key.text))fail(QStringLiteral("Invalid torrent dictionary."));node.dict.insert(key.text,read(depth+1));}
                else node.list.append(read(depth+1));
            }
            if(pos>=data.size() || data[pos++]!='e')fail(QStringLiteral("Unterminated torrent metadata."));
        }else if(c=='i'){
            node.type='i';const int end=data.indexOf('e',pos);bool ok=false;
            if(end<0 || end-pos>20)fail(QStringLiteral("Invalid torrent integer."));
            const QByteArray value=data.mid(pos,end-pos);node.number=value.toLongLong(&ok);
            if(!ok || value.isEmpty() || value.startsWith("-0") || (value.size()>1 && value[0]=='0'))fail(QStringLiteral("Invalid torrent integer."));pos=end+1;
        }else if(c>='0' && c<='9'){
            node.type='s';const int colon=data.indexOf(':',pos-1);bool ok=false;
            if(colon<0 || colon-(pos-1)>9)fail(QStringLiteral("Invalid torrent string."));
            const QByteArray value=data.mid(pos-1,colon-pos+1);const int size=value.toInt(&ok);
            if(!ok || size<0 || size>data.size()-colon-1 || (value.size()>1 && value[0]=='0'))fail(QStringLiteral("Invalid torrent string length."));
            node.text=data.mid(colon+1,size);pos=colon+1+size;
        }else fail(QStringLiteral("Unsupported torrent encoding."));
        node.last=pos;return node;
    }
    bool complete()const{return pos==data.size();}
};
static const Node &field(const Node &node,const QByteArray &name,char type){
    const auto it=node.dict.constFind(name);if(it==node.dict.constEnd() || it->type!=type)fail(QStringLiteral("Missing torrent field: ")+QString::fromLatin1(name));return it.value();
}
static QString component(const QByteArray &raw){
    const QString s=QString::fromUtf8(raw);
    if(s.toUtf8()!=raw || s.isEmpty() || s.size()>180 || s==QStringLiteral(".") || s==QStringLiteral("..") || s.endsWith(QLatin1Char('.')) || s.endsWith(QLatin1Char(' ')) ||
       s.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f<>:\"/\\\\|?*]"))) ||
       s.contains(QRegularExpression(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),QRegularExpression::CaseInsensitiveOption)))fail(QStringLiteral("Unsafe path in torrent metadata."));return s;
}
QString packageCategory(const QString &name){
 const QString n=QFileInfo(name).fileName().toLower();
 if(n==QStringLiteral("db.sqlite"))return QStringLiteral("Indexes");
 if(n.contains(QStringLiteral("video")))return QStringLiteral("Video");if(n.contains(QStringLiteral("chipset")))return QStringLiteral("Chipset");
 if(n.contains(QStringLiteral("wlan"))||n.contains(QStringLiteral("wifi"))||n.contains(QStringLiteral("wi-fi")))return QStringLiteral("Wi-Fi");
 if(n.contains(QStringLiteral("lan")))return QStringLiteral("Network");if(n.contains(QStringLiteral("sound"))||n.contains(QStringLiteral("audio")))return QStringLiteral("Audio");
 if(n.contains(QStringLiteral("massstorage"))||n.contains(QStringLiteral("storage")))return QStringLiteral("Storage");if(n.contains(QStringLiteral("printer")))return QStringLiteral("Printer");
 if(n.contains(QStringLiteral("camera"))||n.contains(QStringLiteral("webcam"))||n.contains(QStringLiteral("biometric")))return QStringLiteral("Camera");
 if(n.contains(QStringLiteral("cardreader")))return QStringLiteral("Card reader");if(n.contains(QStringLiteral("modem")))return QStringLiteral("Modem");
 if(n.contains(QStringLiteral("touchpad"))||n.contains(QStringLiteral("hid"))||n.contains(QStringLiteral("input")))return QStringLiteral("Input");
 if(n.contains(QStringLiteral("monitor")))return QStringLiteral("Monitor");if(n.contains(QStringLiteral("tv_")))return QStringLiteral("TV tuner");return QStringLiteral("Other");
}
QUrl officialMetadataUrl(const QString &family){Q_UNUSED(family);return QUrl(QStringLiteral("https://dl.driverpack.io/DriverPack_Update.torrent"));}
static QPair<QString,QString> packIdentity(QString name){
    name=QFileInfo(QDir::fromNativeSeparators(name)).fileName();if(name.startsWith(QStringLiteral("_P_"),Qt::CaseInsensitive))name.replace(0,3,QStringLiteral("DP_"));
    const QRegularExpression expression(QStringLiteral("^(.*)_([0-9]{4,8})\\.(?:7z|bin)$"),QRegularExpression::CaseInsensitiveOption);const auto match=expression.match(name);
    return match.hasMatch()?qMakePair(match.captured(1).toLower(),match.captured(2)):qMakePair(name.toLower(),QString());
}
QVector<Availability> availability(const Catalog &catalog,const QString &drivers,const QString &indexes,const QStringList &needed){
    QMap<QString,QPair<QString,qint64> > localPacks,localIndexes;
    auto inventory=[](const QString &folder,QMap<QString,QPair<QString,qint64> > &out){for(const QString &name:QDir(folder).entryList(QDir::Files)){const auto id=packIdentity(name);if(!out.contains(id.first) || id.second.toLongLong()>out[id.first].first.toLongLong())out[id.first]=qMakePair(id.second,QFileInfo(QDir(folder).filePath(name)).size());}};
    inventory(drivers,localPacks);inventory(indexes,localIndexes);QStringList families;for(const QString &name:needed)families<<packIdentity(name).first;
    QVector<Availability> result;result.reserve(catalog.files.size());for(const File &file:catalog.files){Availability state;const auto id=packIdentity(file.targetName);state.available=id.second;const auto &inventory=file.kind==Driver?localPacks:localIndexes;state.missing=!inventory.contains(id.first);if(!state.missing)state.current=inventory.value(id.first).first;
        const QFileInfo exact(QDir(file.kind==Driver?drivers:indexes).filePath(file.targetName));state.local=exact.isFile() && exact.size()==file.size;
        state.update=!state.local && (state.missing || state.available.isEmpty() || state.current.isEmpty() || state.available.toLongLong()>=state.current.toLongLong());
        state.needed=file.kind==Driver && families.contains(id.first);state.network=file.kind==Driver && (id.first.contains(QStringLiteral("_lan_")) || id.first.contains(QStringLiteral("_wlan_")) || id.first.contains(QStringLiteral("_wifi_")) || id.first.contains(QStringLiteral("_wwan_")));result.append(state);
    }return result;
}
void recordVerifiedPiece(const Catalog &catalog,int piece,Progress &progress){
    const qint64 begin=qint64(piece)*catalog.pieceLength,end=qMin(catalog.total,begin+catalog.pieceLength);progress.verified+=qMax(qint64(0),end-begin);
    for(auto it=progress.fileVerified.begin();it!=progress.fileVerified.end();++it){const File &file=catalog.files[it.key()];it.value()+=qMax(qint64(0),qMin(end,file.offset+file.size)-qMax(begin,file.offset));}
}
Catalog parse(const QByteArray &bytes){
    if(bytes.isEmpty() || bytes.size()>8*1024*1024)fail(QStringLiteral("Torrent metadata exceeds the size limit."));
    Decoder decoder(bytes);const Node root=decoder.read();if(root.type!='d' || !decoder.complete())fail(QStringLiteral("Invalid torrent document."));
    const Node &info=field(root,"info",'d');Catalog c;c.name=component(field(info,"name",'s').text);
    c.metadata=bytes;
    c.infoHash=QCryptographicHash::hash(bytes.mid(info.first,info.last-info.first),QCryptographicHash::Sha1);
    const qint64 length=field(info,"piece length",'i').number;
    if(length<16384 || length>8*1024*1024 || (length&(length-1)))fail(QStringLiteral("Unsupported torrent block size."));
    c.pieceLength=static_cast<int>(length);c.hashes=field(info,"pieces",'s').text;
    const Node &files=field(info,"files",'l');if(files.list.isEmpty() || files.list.size()>20000)fail(QStringLiteral("Invalid file count."));QSet<QString> paths,targets;
    for(const Node &n:files.list){
        File f;f.offset=c.total;f.size=field(n,"length",'i').number;
        if(f.size<0 || f.size>128LL*1024*1024*1024 || c.total>2LL*1024*1024*1024*1024-f.size)fail(QStringLiteral("Invalid torrent file size."));
        if(n.dict.contains("symlink path") || n.dict.value("attr").text.contains('l'))fail(QStringLiteral("Symbolic links are not accepted."));
        QStringList path;for(const Node &p:field(n,"path",'l').list){if(p.type!='s')fail(QStringLiteral("Invalid file path."));path.append(component(p.text));}
        if(path.isEmpty() || path.size()>16)fail(QStringLiteral("Invalid file path depth."));f.path=path.join(QLatin1Char('/'));
        if(paths.contains(f.path.toCaseFolded()))fail(QStringLiteral("Duplicate torrent file path."));paths.insert(f.path.toCaseFolded());
        if(path.size()==2 && path[0].compare(QStringLiteral("drivers"),Qt::CaseInsensitive)==0 && path.last().startsWith(QStringLiteral("DP_"),Qt::CaseInsensitive) && path.last().endsWith(QStringLiteral(".7z"),Qt::CaseInsensitive))f.kind=Driver;
        if(path.size()==2 && (path[0].compare(QStringLiteral("index"),Qt::CaseInsensitive)==0 || path[0].compare(QStringLiteral("indexes"),Qt::CaseInsensitive)==0) && path.last().compare(QStringLiteral("db.sqlite"),Qt::CaseInsensitive)==0)f.kind=Index;
        if(f.kind!=Other){f.targetName=f.kind==Index?QStringLiteral("db.sqlite"):path.last();const QString target=QString::number(f.kind)+f.targetName.toCaseFolded();if(targets.contains(target))fail(QStringLiteral("Conflicting update targets."));targets.insert(target);}
        c.total+=f.size;c.files.append(f);
    }
    const qint64 pieces=(c.total+length-1)/length;if(pieces<=0 || pieces>400000 || c.hashes.size()!=pieces*20)fail(QStringLiteral("Torrent block hashes do not match its file sizes."));
    const Node seeds=root.dict.value("url-list");QVector<Node> urls;if(seeds.type=='s')urls.append(seeds);else if(seeds.type=='l')urls=seeds.list;
    for(const Node &url:urls){if(url.type!='s')continue;QUrl u(QString::fromUtf8(url.text));if(!u.isValid() || u.host().isEmpty() || !u.userInfo().isEmpty() || !u.query().isEmpty() || !u.fragment().isEmpty() || !u.path().endsWith(QLatin1Char('/')))continue;
        if(u.scheme()!=QStringLiteral("http") && u.scheme()!=QStringLiteral("https"))continue;
        if(u.host()==QStringLiteral("www.glenn.delahoy.com"))u.setScheme(QStringLiteral("https"));if(!c.seeds.contains(u))c.seeds.append(u);
    }
    // Peer-only catalogs remain usable; the production target links the peer
    // transport, and every resulting block is independently hash checked.
    return c;
}
#ifdef Q_OS_WIN
struct InternetHandle { HINTERNET value;explicit InternetHandle(HINTERNET v):value(v){}~InternetHandle(){if(value)InternetCloseHandle(value);} };
static QByteArray get(const QUrl &url,qint64 offset,qint64 length,qint64 fileSize,const std::atomic<bool> &stop,int downloadKiB=0,const std::function<void(qint64)> &received=std::function<void(qint64)>()){
    if(!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || (url.scheme()!=QStringLiteral("https") && url.scheme()!=QStringLiteral("http")))fail(QStringLiteral("Invalid download URL."));
    cancelled(stop);InternetHandle session(InternetOpenW(L"DriverPack Modern / explicit verified package download",INTERNET_OPEN_TYPE_PRECONFIG,0,0,0));
    if(!session.value)fail(QStringLiteral("Windows could not open an Internet session."));
    DWORD timeout=15000;for(DWORD option:{INTERNET_OPTION_CONNECT_TIMEOUT,INTERNET_OPTION_SEND_TIMEOUT,INTERNET_OPTION_RECEIVE_TIMEOUT})InternetSetOptionW(session.value,option,&timeout,sizeof(timeout));
    const QString header=offset>=0?QStringLiteral("Range: bytes=%1-%2\r\nAccept-Encoding: identity\r\n").arg(offset).arg(offset+length-1):QStringLiteral("Accept-Encoding: identity\r\n");
    const QString encoded=QString::fromLatin1(url.toEncoded());
    InternetHandle request(InternetOpenUrlW(session.value,reinterpret_cast<LPCWSTR>(encoded.utf16()),reinterpret_cast<LPCWSTR>(header.utf16()),header.size(),INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_NO_UI|INTERNET_FLAG_NO_COOKIES|INTERNET_FLAG_NO_AUTH|INTERNET_FLAG_PRAGMA_NOCACHE,0));
    if(!request.value)fail(QStringLiteral("Cannot connect to %1 (Windows error %2). Check the connection and Windows TLS support; certificates are never bypassed.").arg(url.host()).arg(GetLastError()));
    DWORD status=0,size=sizeof(status);if(!HttpQueryInfoW(request.value,HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER,&status,&size,0))fail(QStringLiteral("No HTTP status received."));
    if(offset>=0){
        if(status!=206 && !(status==200 && offset==0 && length==fileSize))fail(QStringLiteral("The source cannot provide %1 (HTTP %2). Check for a newer update catalog.").arg(url.path()).arg(status));
        if(status==206){wchar_t value[256]={};size=sizeof(value);if(!HttpQueryInfoW(request.value,HTTP_QUERY_CONTENT_RANGE,value,&size,0))fail(QStringLiteral("Missing HTTP content range."));
            const QString expected=QStringLiteral("bytes %1-%2/%3").arg(offset).arg(offset+length-1).arg(fileSize);
            if(QString::fromWCharArray(value).trimmed()!=expected)fail(QStringLiteral("Unexpected HTTP content range."));}
    }else if(status!=200)fail(QStringLiteral("Cannot download the update catalog (HTTP %1).").arg(status));
    QByteArray output;const qint64 limit=offset>=0?length:8*1024*1024;char buffer[65536];DWORD read=0;
    QElapsedTimer rateClock;rateClock.start();
    for(;;){cancelled(stop);if(!InternetReadFile(request.value,buffer,sizeof(buffer),&read))fail(QStringLiteral("The network transfer failed. Verified blocks can be resumed."));if(!read)break;if(output.size()+static_cast<qint64>(read)>limit)fail(QStringLiteral("Server response exceeds the expected size."));output.append(buffer,static_cast<int>(read));if(received)received(read);if(downloadKiB>0){const qint64 due=qint64(output.size())*1000/(qint64(downloadKiB)*1024);while(rateClock.elapsed()<due){cancelled(stop);QThread::msleep(static_cast<unsigned long>(qMax<qint64>(1,qMin<qint64>(50,due-rateClock.elapsed()))));}}}
    if(offset>=0 && output.size()!=length)fail(QStringLiteral("Incomplete HTTP range response."));return output;
}
#else
static QByteArray get(const QUrl&,qint64,qint64,qint64,const std::atomic<bool>&,int=0,const std::function<void(qint64)> &received=std::function<void(qint64)>()){fail(QStringLiteral("Network package downloads require Windows."));return QByteArray();}
#endif
QByteArray fetchMetadata(const QUrl &url,const std::atomic<bool> &stop){if(url.isEmpty())fail(QStringLiteral("Automatic DriverPack catalog endpoint is not configured. Open a local torrent catalog instead."));if(url.scheme()!=QStringLiteral("https"))fail(QStringLiteral("Update catalogs must be fetched over HTTPS."));return get(url,-1,0,0,stop);}
static QVector<int> blocks(const Catalog &c,const QVector<int> &selection){
    if(selection.isEmpty())fail(QStringLiteral("Select at least one index or driver package."));QSet<int> chosen;
    for(int index:selection){if(index<0 || index>=c.files.size() || c.files[index].kind==Other)fail(QStringLiteral("Only driver packages and indexes may be published."));const File &f=c.files[index];if(!f.size)continue;
        for(qint64 p=f.offset/c.pieceLength;p<=(f.offset+f.size-1)/c.pieceLength;++p)chosen.insert(static_cast<int>(p));}
    QVector<int> sorted=chosen.values().toVector();std::sort(sorted.begin(),sorted.end());return sorted;
}
bool containsDatabase(const Catalog &catalog){for(const auto &file:catalog.files)if(file.kind==Index)return true;return false;}
qint64 requiredBytes(const Catalog &c,const QVector<int> &selection){if(selection.isEmpty())return 0;qint64 size=0;for(int p:blocks(c,selection))size+=qMin<qint64>(c.pieceLength,c.total-static_cast<qint64>(p)*c.pieceLength);return size;}
static QString blockPath(const QString &cache,int p){return QDir(cache).filePath(QString::number(p)+QStringLiteral(".block"));}
static QByteArray cached(const Catalog &c,const QString &cache,int p){QFile f(blockPath(cache,p));if(!f.open(QIODevice::ReadOnly) || f.size()>c.pieceLength)return QByteArray();const QByteArray data=f.readAll();return QCryptographicHash::hash(data,QCryptographicHash::Sha1)==c.hashes.mid(p*20,20)?data:QByteArray();}
QStringList download(const Catalog &c,const QVector<int> &selection,const QString &drivers,const QString &indexes,const QString &cacheRoot,const std::atomic<bool> &stop,const Sink &sink,const RangeFetch &custom){
    if(c.transfer.downloadKiB<0 || c.transfer.downloadKiB>102400 || c.transfer.uploadKiB<1 || c.transfer.uploadKiB>102400 || c.transfer.connections<1 || c.transfer.connections>200 || (c.transfer.port && (c.transfer.port<1024 || c.transfer.port>65535)))fail(QStringLiteral("Invalid transfer settings."));
    const QVector<int> pieces=blocks(c,selection);const qint64 required=requiredBytes(c,selection);if(!required)fail(QStringLiteral("The selected files are empty."));
    const QString cache=QDir(cacheRoot).filePath(QString::fromLatin1(c.infoHash.toHex()));if(!QDir().mkpath(cache))fail(QStringLiteral("Cannot create the download cache."));
    const QString canonicalCache=QFileInfo(cache).canonicalFilePath(),canonicalRoot=QFileInfo(cacheRoot).canonicalFilePath();
    if(canonicalCache!=QDir(canonicalRoot).filePath(QString::fromLatin1(c.infoHash.toHex())))fail(QStringLiteral("The download cache resolves outside its chosen folder."));
    Progress progress;progress.total=required;for(int index:selection)progress.fileVerified[index]=0;QElapsedTimer notice;notice.start();qint64 missing=0;for(int p:pieces){cancelled(stop);if(!cached(c,cache,p).isEmpty())recordVerifiedPiece(c,p,progress);else missing+=qMin<qint64>(c.pieceLength,c.total-static_cast<qint64>(p)*c.pieceLength);}
    QMap<QString,qint64> space;QMap<QString,QStorageInfo> volumes;
    QStorageInfo storage(cache);const QString cacheVolume=storage.rootPath().toCaseFolded();space[cacheVolume]+=missing;volumes[cacheVolume]=storage;
    QSet<int> budgeted;
    for(int index:selection){if(budgeted.contains(index))continue;budgeted.insert(index);const File &file=c.files[index];
        const QString directory=file.kind==Driver?drivers:indexes;if(!QDir().mkpath(directory))fail(QStringLiteral("Cannot create the chosen destination folder."));
        QStorageInfo target(directory);const QString volume=target.rootPath().toCaseFolded();space[volume]+=file.size;volumes[volume]=target;
    }
    for(auto it=space.constBegin();it!=space.constEnd();++it){const QStorageInfo volume=volumes.value(it.key());
        if(volume.isValid() && volume.bytesAvailable()>=0 && volume.bytesAvailable()<it.value()+32*1024*1024)
            fail(QStringLiteral("Not enough free space. About %1 MB is required for verified blocks and selected files on %2.").arg(it.value()/(1024*1024)+33).arg(volume.rootPath()));
    }
    const RangeFetch fetch=[&](const QUrl &u,qint64 first,qint64 count,qint64 total){
        if(custom){const QByteArray data=custom(u,first,count,total);progress.received+=data.size();if(sink)sink(progress);return data;}
        return get(u,first,count,total,stop,c.transfer.downloadKiB,[&](qint64 bytes){progress.received+=bytes;if(notice.elapsed()>200){if(sink)sink(progress);notice.restart();}});
    };
    QSet<int> rejectedSeeds;QVector<int> peerNeeded;QString unavailable;int singleUntil=0;
    // Batch adjacent pieces (up to 16 MiB) so large packages do not generate a request per piece.
    for(int at=0;at<pieces.size();){
        cancelled(stop);const int first=pieces[at];if(!cached(c,cache,first).isEmpty()){++at;continue;}
        int end=at+1;while(at>=singleUntil && end<pieces.size() && pieces[end]==pieces[end-1]+1 && static_cast<qint64>(end-at+1)*c.pieceLength<=16*1024*1024 && cached(c,cache,pieces[end]).isEmpty())++end;
        const qint64 begin=static_cast<qint64>(first)*c.pieceLength,finish=qMin(c.total,(static_cast<qint64>(pieces[end-1])+1)*c.pieceLength);QByteArray batch;QString lastError;
        bool valid=false;
        for(int seed=0;seed<c.seeds.size() && !valid;++seed){if(rejectedSeeds.contains(seed))continue;
            for(int attempt=0;attempt<2 && !valid;++attempt){try{
                batch.clear();for(const File &file:c.files){const qint64 left=qMax(begin,file.offset),right=qMin(finish,file.offset+file.size);if(left>=right)continue;
                    cancelled(stop);progress.message=QStringLiteral("Downloading %1").arg(file.path);if(sink)sink(progress);
                    QUrl relative;relative.setPath(c.name+QLatin1Char('/')+file.path);const QUrl url=c.seeds[seed].resolved(relative);batch+=fetch(url,left-file.offset,right-left,file.size);
                }
                if(batch.size()!=finish-begin)fail(QStringLiteral("Unexpected downloaded block length."));
                for(int n=at;n<end;++n){const qint64 position=static_cast<qint64>(pieces[n])*c.pieceLength-begin;const QByteArray piece=batch.mid(static_cast<int>(position),static_cast<int>(qMin<qint64>(c.pieceLength,c.total-begin-position)));
                    if(QCryptographicHash::hash(piece,QCryptographicHash::Sha1)!=c.hashes.mid(pieces[n]*20,20)){rejectedSeeds.insert(seed);fail(QStringLiteral("The download source returned data that failed its torrent hash check. Nothing was published."));}}
                valid=true;
            }catch(const std::exception &e){cancelled(stop);lastError=QString::fromUtf8(e.what());if(rejectedSeeds.contains(seed))break;}}
        }
        if(!valid){
            if(end>at+1){singleUntil=end;continue;}
            peerNeeded.append(first);unavailable=lastError;at=end;continue;
        }
        for(int n=at;n<end;++n){cancelled(stop);const qint64 position=static_cast<qint64>(pieces[n])*c.pieceLength-begin;const QByteArray piece=batch.mid(static_cast<int>(position),static_cast<int>(qMin<qint64>(c.pieceLength,c.total-begin-position)));
            QSaveFile out(blockPath(cache,pieces[n]));if(!out.open(QIODevice::WriteOnly) || out.write(piece)!=piece.size() || !out.commit())fail(QStringLiteral("Could not save a verified download block."));recordVerifiedPiece(c,pieces[n],progress);if(sink)sink(progress);}
        at=end;
    }
    if(!peerNeeded.isEmpty()){
#ifdef SDIO_TORRENT_LINKED
        progress.message=QStringLiteral("Connecting to torrent peers to complete verified blocks...");if(sink)sink(progress);
        fetchPeerBlocks(c,peerNeeded,cache,stop,sink,progress);
#else
        fail(unavailable.isEmpty()?QStringLiteral("No verified HTTP source is available for these blocks."):unavailable);
#endif
    }
    QStringList published;QSet<int> done;
    for(int index:selection){if(done.contains(index))continue;done.insert(index);cancelled(stop);const File &file=c.files[index];
        const QString directory=QFileInfo(file.kind==Driver?drivers:indexes).absoluteFilePath();if(!QDir().mkpath(directory))fail(QStringLiteral("Cannot create the chosen destination folder."));
        QStorageInfo destination(directory);if(destination.isValid() && destination.bytesAvailable()>=0 && destination.bytesAvailable()<file.size+1024*1024)fail(QStringLiteral("Not enough space to publish %1. Verified blocks were retained.").arg(file.targetName));
        if(component(file.targetName.toUtf8())!=file.targetName)fail(QStringLiteral("Unsafe update target name."));
        const QString target=QDir(directory).filePath(file.targetName);if(QFileInfo(target).isSymLink())fail(QStringLiteral("Refusing to replace a symbolic-link destination."));
        progress.message=QStringLiteral("Saving verified %1").arg(file.targetName);if(sink)sink(progress);
        QByteArray prefix;
        QTemporaryDir stagedIndex(QDir(directory).filePath(QStringLiteral(".driverpack-index-XXXXXX")));QString stagedDestination=target;if(file.kind==Index){if(!stagedIndex.isValid())fail(QStringLiteral("Cannot stage a DriverPack index safely."));stagedDestination=QDir(stagedIndex.path()).filePath(QStringLiteral("db.sqlite"));}QSaveFile out(stagedDestination);if(!out.open(QIODevice::WriteOnly))fail(QStringLiteral("Cannot create %1").arg(target));
        for(qint64 position=file.offset;position<file.offset+file.size;){cancelled(stop);const int p=static_cast<int>(position/c.pieceLength);const QByteArray piece=cached(c,cache,p);if(piece.isEmpty())fail(QStringLiteral("A verified cache block changed before publication."));
            const int within=static_cast<int>(position% c.pieceLength);const int count=static_cast<int>(qMin<qint64>(piece.size()-within,file.offset+file.size-position));if(prefix.size()<16)prefix+=piece.mid(within,qMin(count,16-prefix.size()));if(count<=0 || out.write(piece.constData()+within,count)!=count)fail(QStringLiteral("Could not assemble the verified file."));position+=count;}
        if(file.kind==Index){
            if(prefix!=QByteArray("SQLite format 3\0",16))fail(QStringLiteral("The downloaded DriverPack database has an unsupported header. Existing indexes were preserved."));
        }else if(file.kind==Driver && !prefix.startsWith(QByteArray::fromHex("377abcaf271c")))fail(QStringLiteral("The downloaded package is not a 7-Zip driver archive."));
        if(!out.commit())fail(QStringLiteral("Could not publish the verified file."));if(file.kind==Index){{driverpack::Catalog validate(stagedDestination);}QFile input(stagedDestination);QSaveFile finalIndex(target);if(!input.open(QIODevice::ReadOnly)||!finalIndex.open(QIODevice::WriteOnly))fail(QStringLiteral("Cannot publish the validated DriverPack index."));while(!input.atEnd()){const QByteArray chunk=input.read(1024*1024);if((chunk.isEmpty()&&input.error()!=QFile::NoError)||finalIndex.write(chunk)!=chunk.size())fail(QStringLiteral("Cannot save the validated DriverPack index."));}if(!finalIndex.commit())fail(QStringLiteral("Cannot replace the DriverPack index; the existing file was preserved."));}published.append(target);progress.published=published.size();progress.message=QStringLiteral("Verified and saved %1").arg(file.targetName);if(sink)sink(progress);
    }
    if(c.transfer.keepSharing){try{shareVerified(c,cache);}catch(const std::exception &e){progress.message=QStringLiteral("Downloads were saved. Persistent sharing could not be started.");progress.diagnostic=QString::fromUtf8(e.what());if(sink)sink(progress);}}
    else {for(int p:pieces)QFile::remove(blockPath(cache,p));QDir().rmdir(cache);}
    return published;
}
int runReport(int argc,char **argv){
    QCoreApplication app(argc,argv);const QStringList args=app.arguments();auto value=[&args](const QString &key){int i=args.indexOf(key);return i>=0 && i+1<args.size()?args[i+1]:QString();};
    QJsonObject report;report.insert(QStringLiteral("installationRequested"),false);int code=0;
    try{std::atomic<bool> stop(false);QByteArray bytes;const QString source=value(QStringLiteral("--torrent"));
        if(source.isEmpty())bytes=fetchMetadata(officialMetadataUrl(libraryFamily(value(QStringLiteral("--drivers")),resolveIndexDirectory(value(QStringLiteral("--indexes")),value(QStringLiteral("--drivers"))))),stop);else{QFile input(source);if(!input.open(QIODevice::ReadOnly) || input.size()>8*1024*1024)fail(QStringLiteral("Cannot read bounded torrent metadata."));bytes=input.readAll();}
        Catalog c=parse(bytes);
        auto limit=[&](const QString &key,int low,int high,int fallback){if(!args.contains(key))return fallback;bool ok=false;const int number=value(key).toInt(&ok);if(!ok || number<low || number>high)fail(QStringLiteral("Invalid transfer limit: ")+key);return number;};
        c.transfer.downloadKiB=limit(QStringLiteral("--download-limit-kib"),0,102400,0);c.transfer.uploadKiB=limit(QStringLiteral("--upload-limit-kib"),1,102400,64);c.transfer.connections=limit(QStringLiteral("--peer-connections"),1,200,40);c.transfer.port=limit(QStringLiteral("--peer-port"),0,65535,0);
        QJsonObject transfer;transfer.insert(QStringLiteral("downloadKiB"),c.transfer.downloadKiB);transfer.insert(QStringLiteral("uploadKiB"),c.transfer.uploadKiB);transfer.insert(QStringLiteral("connections"),c.transfer.connections);transfer.insert(QStringLiteral("port"),c.transfer.port);report.insert(QStringLiteral("transferSettings"),transfer);
        report.insert(QStringLiteral("infoHash"),QString::fromLatin1(c.infoHash.toHex()));QJsonArray files;QVector<int> selected;
        for(int i=0;i<c.files.size();++i){const File &f=c.files[i];if(f.kind==Other)continue;QJsonObject item;item.insert(QStringLiteral("path"),f.path);item.insert(QStringLiteral("size"),static_cast<double>(f.size));files.append(item);if(f.path==value(QStringLiteral("--download")))selected.append(i);}
        report.insert(QStringLiteral("files"),files);if(!value(QStringLiteral("--download")).isEmpty()){
            if(selected.isEmpty())fail(QStringLiteral("The requested allowed file was not found."));
            if(value(QStringLiteral("--drivers")).isEmpty() || value(QStringLiteral("--indexes")).isEmpty() || value(QStringLiteral("--cache")).isEmpty())fail(QStringLiteral("Explicit drivers, indexes and cache paths are required."));
            QFile events(value(QStringLiteral("--online-report"))+QStringLiteral(".events.jsonl"));events.open(QIODevice::WriteOnly);
            const Sink log=[&events](const Progress &p){QJsonObject event;event.insert(QStringLiteral("message"),p.message);event.insert(QStringLiteral("diagnostic"),p.diagnostic);event.insert(QStringLiteral("verified"),static_cast<double>(p.verified));event.insert(QStringLiteral("total"),static_cast<double>(p.total));if(events.isOpen()){events.write(QJsonDocument(event).toJson(QJsonDocument::Compact));events.write("\n");events.flush();}};
            const QStringList paths=download(c,selected,value(QStringLiteral("--drivers")),value(QStringLiteral("--indexes")),value(QStringLiteral("--cache")),stop,log);report.insert(QStringLiteral("published"),QJsonArray::fromStringList(paths));}
    }catch(const std::exception &e){report.insert(QStringLiteral("error"),QString::fromUtf8(e.what()));code=1;}
    QSaveFile file(value(QStringLiteral("--online-report")));const QByteArray bytes=QJsonDocument(report).toJson();if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())return 3;return code;
}
}
