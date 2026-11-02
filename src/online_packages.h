#ifndef SDIO_ONLINE_PACKAGES_H
#define SDIO_ONLINE_PACKAGES_H
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QUrl>
#include <QMap>
#include <atomic>
#include <functional>

namespace OnlinePackages {
enum Kind { Other, Index, Driver };
QString packageCategory(const QString &name);
struct File { QString path, targetName; qint64 offset=0,size=0; Kind kind=Other; };
struct TransferSettings { int downloadKiB=0,uploadKiB=64,connections=40,port=0,outgoingMin=0,outgoingMax=0; bool keepSharing=false; };
struct Catalog { QString name; QByteArray infoHash,hashes,metadata; int pieceLength=0; qint64 total=0; QVector<File> files; QVector<QUrl> seeds, trackers; TransferSettings transfer; };
struct Progress { QString message,diagnostic; qint64 verified=0,total=0,received=0,downloadRate=-1,uploadRate=0; int published=0,peers=0; QMap<int,qint64> fileVerified; };
struct Availability {QString current,available;bool local=false,missing=true,update=true,needed=false,network=false;};
QVector<Availability> availability(const Catalog&,const QString &drivers,const QString &indexes,const QStringList &needed);
void recordVerifiedPiece(const Catalog&,int piece,Progress&);
typedef std::function<QByteArray(const QUrl&,qint64,qint64,qint64)> RangeFetch;
typedef std::function<void(const Progress&)> Sink;
void fetchPeerBlocks(const Catalog&,const QVector<int>&,const QString&,const std::atomic<bool>&,const Sink&,Progress&,const QString &loopbackPeer=QString());
void shareVerified(const Catalog&,const QString &cache,bool loopbackOnly=false);
void stopSharing();
void setSharingEnabled(bool enabled);
QString sharingStatus();
int sharingPort();
void resumeSharing(const QString &cacheRoot,const TransferSettings&,const std::atomic<bool>&);
struct PeerEndpoint { QString address; quint16 port=0; };
QUrl trackerAnnounceUrl(const QUrl&,const QByteArray &infoHash,const QByteArray &peerId,int port,qint64 left);
QVector<PeerEndpoint> parseTrackerPeers(const QByteArray&);
QVector<PeerEndpoint> fetchTrackerPeers(const QUrl&,const QByteArray &infoHash,const QByteArray &peerId,int port,qint64 left,const std::atomic<bool>&);
QUrl officialMetadataUrl(const QString &family=QString());
QByteArray fetchMetadata(const QUrl &url,const std::atomic<bool> &stop);
Catalog parse(const QByteArray &torrent);
bool containsDatabase(const Catalog&);
qint64 requiredBytes(const Catalog&,const QVector<int>&);
QStringList download(const Catalog&,const QVector<int>&,const QString &drivers,const QString &indexes,
                     const QString &cache,const std::atomic<bool>&,const Sink&,const RangeFetch &fetch=RangeFetch());
int runReport(int argc,char **argv);
}
#endif
