#include "online_packages.h"
#include <QTemporaryDir>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <stdexcept>
#ifdef SDIO_TORRENT_LINKED
#undef signals
#undef slots
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_status.hpp>
#include <set>
#endif

namespace OnlinePackages {
#ifdef SDIO_TORRENT_LINKED
struct SharedTorrent {
    std::shared_ptr<libtorrent::session> session;
    libtorrent::torrent_handle handle;
    QString hash;
};
static QMutex sharingMutex;
static std::atomic<bool> sharingEnabled(true);
static std::vector<SharedTorrent> sharedTorrents;
#endif
void stopSharing(){
#ifdef SDIO_TORRENT_LINKED
    sharingEnabled.store(false);QMutexLocker lock(&sharingMutex);sharedTorrents.clear();
#endif
}
void setSharingEnabled(bool enabled){
#ifdef SDIO_TORRENT_LINKED
    if(!enabled){stopSharing();return;}QMutexLocker lock(&sharingMutex);sharingEnabled.store(true);
#else
    Q_UNUSED(enabled);
#endif
}
int sharingPort(){
#ifdef SDIO_TORRENT_LINKED
    QMutexLocker lock(&sharingMutex);return sharedTorrents.empty()?0:sharedTorrents.back().session->listen_port();
#else
    return 0;
#endif
}
QString sharingStatus(){
    int peers=0; qint64 uploaded=0;
#ifdef SDIO_TORRENT_LINKED
    QMutexLocker lock(&sharingMutex);
    for(const auto &torrent:sharedTorrents){const auto status=torrent.handle.status();peers+=status.num_peers;uploaded+=status.total_upload;}
#endif
    return QStringLiteral("Sharing verified blocks · %1 peers · %2 MiB uploaded").arg(peers).arg(uploaded/1048576.0,0,'f',1);
}
void shareVerified(const Catalog &catalog,const QString &cache,bool loopbackOnly){
#ifdef SDIO_TORRENT_LINKED
    namespace lt=libtorrent;
    const QStringList cachedPieces=QDir(cache).entryList(QStringList()<<QStringLiteral("*.block"),QDir::Files);if(cachedPieces.isEmpty())return;
    QMutexLocker lock(&sharingMutex);const QString hash=QString::fromLatin1(catalog.infoHash.toHex());
    if(!sharingEnabled.load())return;
    SharedTorrent torrent;bool existing=false;for(const auto &item:sharedTorrents)if(item.hash==hash){torrent=item;existing=true;break;}
    if(catalog.transfer.uploadKiB<1 || catalog.transfer.uploadKiB>102400 || catalog.transfer.connections<1 || catalog.transfer.connections>200 || (catalog.transfer.port && (catalog.transfer.port<1024 || catalog.transfer.port>65535)))throw std::runtime_error("Invalid sharing settings.");
    lt::error_code error;std::shared_ptr<lt::torrent_info> info(new lt::torrent_info(catalog.metadata.constData(),catalog.metadata.size(),error));if(error)throw std::runtime_error(error.message());
    lt::settings_pack settings;settings.set_str(lt::settings_pack::listen_interfaces,loopbackOnly?"127.0.0.1:0":QStringLiteral("0.0.0.0:%1").arg(catalog.transfer.port).toStdString());
    settings.set_bool(lt::settings_pack::enable_dht,!loopbackOnly);settings.set_bool(lt::settings_pack::enable_lsd,false);settings.set_bool(lt::settings_pack::enable_upnp,false);settings.set_bool(lt::settings_pack::enable_natpmp,false);
    settings.set_str(lt::settings_pack::dht_bootstrap_nodes,"router.bittorrent.com:6881,router.utorrent.com:6881,router.bitcomet.com:6881");
    settings.set_int(lt::settings_pack::upload_rate_limit,catalog.transfer.uploadKiB*1024);settings.set_int(lt::settings_pack::connections_limit,catalog.transfer.connections);
    lt::add_torrent_params params;params.ti=info;params.save_path=QDir(cache).filePath(QStringLiteral("sharing")).toUtf8().constData();
    params.flags=lt::torrent_flags::default_flags & ~lt::torrent_flags::paused & ~lt::torrent_flags::auto_managed;params.flags|=lt::torrent_flags::override_web_seeds;
    params.file_priorities.assign(info->num_files(),lt::dont_download);for(int i=0;i<info->num_files();++i)params.renamed_files[lt::file_index_t(i)]=QStringLiteral("data/%1.part").arg(i).toStdString();
    if(!existing){torrent.hash=hash;if(sharedTorrents.empty())torrent.session.reset(new lt::session(settings));else{torrent.session=sharedTorrents.front().session;torrent.session->apply_settings(settings);}torrent.handle=torrent.session->add_torrent(params,error);if(error)throw std::runtime_error(error.message());}
    torrent.session->apply_settings(settings);
    torrent.handle.prioritize_pieces(std::vector<lt::download_priority_t>(info->num_pieces(),lt::dont_download));
    if(!existing){if(sharedTorrents.size()>=2){sharedTorrents.front().session->remove_torrent(sharedTorrents.front().handle);sharedTorrents.erase(sharedTorrents.begin());}sharedTorrents.push_back(torrent);}
    lock.unlock(); // Never hold the UI status/stop lock during storage I/O.
    QSaveFile metadata(QDir(cache).filePath(QStringLiteral("sharing.torrent")));if(!metadata.open(QIODevice::WriteOnly) || metadata.write(catalog.metadata)!=catalog.metadata.size() || !metadata.commit())throw std::runtime_error("Cannot save sharing metadata.");
    for(;;){if(!sharingEnabled.load())return;const auto status=torrent.handle.status();if(status.errc)throw std::runtime_error(status.errc.message());if(status.state!=lt::torrent_status::checking_files && status.state!=lt::torrent_status::checking_resume_data)break;QThread::msleep(30);}
    // One independently verified piece in flight at a time. This also bounds
    // memory when the cache is on a slower USB disk. Priorities always stay zero.
    QStorageInfo disk(cache);qint64 budget=disk.isValid()?disk.bytesAvailable():-1;
    for(const QString &name:cachedPieces){
        if(!sharingEnabled.load())return;bool ok=false;const int index=QFileInfo(name).completeBaseName().toInt(&ok);if(!ok || index<0 || index>=info->num_pieces())continue;
        QFile file(QDir(cache).filePath(name));const qint64 expected=qMin<qint64>(catalog.pieceLength,catalog.total-qint64(index)*catalog.pieceLength);
        if(!file.open(QIODevice::ReadOnly) || file.size()!=expected)continue;const QByteArray bytes=file.readAll();
        if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha1)!=catalog.hashes.mid(index*20,20))continue;
        if(torrent.handle.have_piece(lt::piece_index_t(index)))continue;
        if(budget>=0 && budget<expected+32*1024*1024)throw std::runtime_error("Not enough free space for the private sharing copy. Downloaded files were preserved.");if(budget>=0)budget-=expected;
        torrent.handle.add_piece(lt::piece_index_t(index),bytes.constData());QElapsedTimer pending;pending.start();
        while(!torrent.handle.have_piece(lt::piece_index_t(index))){if(!sharingEnabled.load())return;const auto status=torrent.handle.status();if(status.errc)throw std::runtime_error(status.errc.message());if(pending.elapsed()>60000)throw std::runtime_error("Writing verified sharing data timed out.");QThread::msleep(10);}
    }

#else
    Q_UNUSED(catalog);Q_UNUSED(cache);Q_UNUSED(loopbackOnly);throw std::runtime_error("Persistent sharing is not linked in this developer build.");
#endif
}
void resumeSharing(const QString &root,const TransferSettings &settings,const std::atomic<bool> &stop){
    if(!settings.keepSharing)return;
    for(const QString &folder:QDir(root).entryList(QDir::Dirs|QDir::NoDotAndDotDot)){
        if(stop.load())return;if(folder.size()!=40)continue;
        const QString cache=QDir(root).filePath(folder);if(QFileInfo(cache).isSymLink())continue;
        QFile metadata(QDir(cache).filePath(QStringLiteral("sharing.torrent")));if(!metadata.open(QIODevice::ReadOnly) || metadata.size()>8*1024*1024)continue;
        try{Catalog catalog=parse(metadata.readAll());if(QString::fromLatin1(catalog.infoHash.toHex())!=folder)continue;catalog.transfer=settings;shareVerified(catalog,cache);}catch(const std::exception&){}
    }
}
void fetchPeerBlocks(const Catalog &catalog,const QVector<int> &pieces,const QString &cache,const std::atomic<bool> &stop,const Sink &sink,Progress &progress,const QString &loopbackPeer){
#ifdef SDIO_TORRENT_LINKED
    namespace lt=libtorrent;
    if(stop.load())throw std::runtime_error("Download paused. Verified blocks were kept for resuming.");
    if(catalog.metadata.isEmpty() || pieces.isEmpty())throw std::runtime_error("No verified torrent metadata is available for peer download.");
    QTemporaryDir storage(QDir(cache).filePath(QStringLiteral("peer-XXXXXX")));
    if(!storage.isValid())throw std::runtime_error("Cannot create private peer-download storage.");
    lt::error_code error;
    std::shared_ptr<lt::torrent_info> info(new lt::torrent_info(catalog.metadata.constData(),catalog.metadata.size(),error));
    if(error)throw std::runtime_error(error.message());
    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::user_agent,"SDIO Modern verified downloads");
    if(catalog.transfer.downloadKiB<0 || catalog.transfer.downloadKiB>102400 || catalog.transfer.uploadKiB<0 || catalog.transfer.uploadKiB>102400 || catalog.transfer.connections<1 || catalog.transfer.connections>200 || (catalog.transfer.port && (catalog.transfer.port<1024 || catalog.transfer.port>65535)))throw std::runtime_error("Invalid transfer settings.");
    const std::string listen=loopbackPeer.isEmpty()?QStringLiteral("0.0.0.0:%1").arg(catalog.transfer.port).toStdString():"127.0.0.1:0";
    settings.set_str(lt::settings_pack::listen_interfaces,listen);
    settings.set_bool(lt::settings_pack::enable_upnp,false);settings.set_bool(lt::settings_pack::enable_natpmp,false);settings.set_bool(lt::settings_pack::enable_lsd,false);
    settings.set_bool(lt::settings_pack::enable_dht,loopbackPeer.isEmpty());
    settings.set_str(lt::settings_pack::dht_bootstrap_nodes,"router.bittorrent.com:6881,router.utorrent.com:6881,router.bitcomet.com:6881");
    settings.set_int(lt::settings_pack::connections_limit,catalog.transfer.connections);settings.set_int(lt::settings_pack::upload_rate_limit,catalog.transfer.uploadKiB*1024);settings.set_int(lt::settings_pack::download_rate_limit,catalog.transfer.downloadKiB*1024);
    settings.set_int(lt::settings_pack::alert_mask,static_cast<int>(static_cast<std::uint32_t>(lt::alert_category::error|lt::alert_category::storage|lt::alert_category::status|lt::alert_category::piece_progress|lt::alert_category::stats)));
    if(catalog.transfer.outgoingMin){if(catalog.transfer.outgoingMin<1024 || catalog.transfer.outgoingMax<catalog.transfer.outgoingMin || catalog.transfer.outgoingMax>65535)throw std::runtime_error("Invalid outgoing port range.");settings.set_int(lt::settings_pack::outgoing_port,catalog.transfer.outgoingMin);settings.set_int(lt::settings_pack::num_outgoing_ports,catalog.transfer.outgoingMax-catalog.transfer.outgoingMin+1);}
    lt::session session(settings);
    lt::add_torrent_params params;params.ti=info;params.save_path=storage.path().toUtf8().constData();
    params.flags=lt::torrent_flags::default_flags & ~lt::torrent_flags::paused & ~lt::torrent_flags::auto_managed;
    params.flags|=lt::torrent_flags::override_web_seeds;
    params.file_priorities.assign(info->num_files(),lt::dont_download);
    // All torrent paths are remapped to inert private files. Only our separately
    // verified publisher can create .bin or .7z files in the user's directories.
    for(int i=0;i<info->num_files();++i)params.renamed_files[lt::file_index_t(i)]=QStringLiteral("data/%1.part").arg(i).toStdString();
    lt::torrent_handle handle=session.add_torrent(params,error);if(error)throw std::runtime_error(error.message());
    std::vector<lt::download_priority_t> priorities(info->num_pieces(),lt::dont_download);
    std::set<int> wanted,reading;
    for(int p:pieces){if(p<0 || p>=info->num_pieces())throw std::runtime_error("Invalid requested torrent block.");wanted.insert(p);priorities[p]=lt::top_priority;}
    handle.prioritize_pieces(priorities);handle.resume();
    // Deterministic loopback-only component testing; never read from metadata or environment.
    if(!loopbackPeer.isEmpty()){
        bool ok=false;const int port=loopbackPeer.section(QLatin1Char(':'),-1).toInt(&ok);
        if(!ok || !loopbackPeer.startsWith(QStringLiteral("127.0.0.1:")) || port<1 || port>65535)throw std::runtime_error("Invalid loopback test endpoint.");
        handle.connect_peer(lt::tcp::endpoint(lt::address_v4::loopback(),static_cast<unsigned short>(port)));
    }
    int dhtNodes=0;QString networkError;
    QElapsedTimer idle,notice,elapsed;idle.start();notice.start();elapsed.start();
    const qint64 receivedBeforePeers=progress.received;
    while(!wanted.empty()){
        if(stop.load()){handle.pause();throw std::runtime_error("Download paused. Verified blocks were kept for resuming.");}
        for(int p:wanted)if(!reading.count(p) && handle.have_piece(lt::piece_index_t(p))){handle.read_piece(lt::piece_index_t(p));reading.insert(p);}
        std::vector<lt::alert*> alerts;session.pop_alerts(&alerts);
        for(lt::alert *alert:alerts){
            if(const auto *tracker=lt::alert_cast<lt::tracker_error_alert>(alert)){
                networkError=QString::fromStdString(tracker->error.message());
            }
            if(const auto *stats=lt::alert_cast<lt::dht_stats_alert>(alert)){dhtNodes=0;for(const auto &bucket:stats->routing_table)dhtNodes+=bucket.num_nodes;}
            if(const auto *listen=lt::alert_cast<lt::listen_failed_alert>(alert))networkError=QString::fromStdString(listen->error.message());
            const auto *piece=lt::alert_cast<lt::read_piece_alert>(alert);if(!piece)continue;
            const int index=static_cast<int>(piece->piece);if(!wanted.count(index))continue;
            if(piece->error)throw std::runtime_error(piece->error.message());
            const QByteArray bytes(piece->buffer.get(),piece->size);
            if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha1)!=catalog.hashes.mid(index*20,20))throw std::runtime_error("Peer data failed independent block verification.");
            QSaveFile file(QDir(cache).filePath(QString::number(index)+QStringLiteral(".block")));
            if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())throw std::runtime_error("Cannot save a verified peer block.");
            wanted.erase(index);reading.erase(index);recordVerifiedPiece(catalog,index,progress);progress.message=QStringLiteral("Receiving verified blocks from download peers");if(sink)sink(progress);idle.restart();
        }
        if(notice.elapsed()>1000){session.post_dht_stats();progress.diagnostic=QStringLiteral("DHT nodes: %1; network: %2").arg(dhtNodes).arg(networkError);if(!loopbackPeer.isEmpty() && elapsed.elapsed()%5000<1200){const int port=loopbackPeer.section(QLatin1Char(':'),-1).toInt();handle.connect_peer(lt::tcp::endpoint(lt::address_v4::loopback(),static_cast<unsigned short>(port)));}const lt::torrent_status status=handle.status();if(status.errc)throw std::runtime_error(status.errc.message());progress.received=receivedBeforePeers+status.total_payload_download;progress.downloadRate=status.download_payload_rate;progress.uploadRate=status.upload_payload_rate;progress.peers=status.num_peers;
            progress.message=QStringLiteral("Peer-assisted download · %1 peers · %2 blocks remaining · %3 s").arg(status.num_peers).arg(static_cast<qulonglong>(wanted.size())).arg(elapsed.elapsed()/1000);if(sink)sink(progress);notice.restart();}
        const qint64 idleLimit=catalog.transfer.downloadKiB?qMax<qint64>(90000,qint64(catalog.pieceLength)*2000/(catalog.transfer.downloadKiB*1024)+30000):90000;
        if(idle.elapsed()>idleLimit){handle.pause();throw std::runtime_error("No verified peer blocks arrived. Check the network or resume later; existing files were preserved.");}
        session.wait_for_alert(std::chrono::milliseconds(150));
    }
    {const lt::torrent_status status=handle.status();progress.received=receivedBeforePeers+status.total_payload_download;progress.downloadRate=status.download_payload_rate;progress.uploadRate=status.upload_payload_rate;progress.peers=status.num_peers;if(sink)sink(progress);}
    handle.pause();session.pause();
#else
    Q_UNUSED(catalog);Q_UNUSED(pieces);Q_UNUSED(cache);Q_UNUSED(stop);Q_UNUSED(sink);Q_UNUSED(progress);Q_UNUSED(loopbackPeer);
    throw std::runtime_error("Peer-assisted downloads are not linked in this developer target.");
#endif
}
}
