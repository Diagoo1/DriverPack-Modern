#include "../src/online_packages.h"
#include "peer_fixture.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
#undef signals
#undef slots
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_handle.hpp>
int main(int argc,char **argv){QCoreApplication app(argc,argv);
    try{namespace lt=libtorrent;Fixture fixture;fixture.peerOnly=true;fixture.rebuild();OnlinePackages::Catalog catalog=OnlinePackages::parse(fixture.torrent);catalog.transfer.downloadKiB=1024;catalog.transfer.uploadKiB=64;catalog.transfer.connections=4;
        QTemporaryDir directory;QDir root(directory.path());const QString seedPath=root.filePath(QStringLiteral("seed")),cache=root.filePath(QStringLiteral("cache"));QDir().mkpath(cache);
        QByteArray all;
        for(int i=0;i<fixture.names.size();++i){const QString path=QDir(seedPath).filePath(QStringLiteral("DriverPack_Test/")+QString::fromUtf8(fixture.names[i]));QDir().mkpath(QFileInfo(path).absolutePath());QFile file(path);
            if(!file.open(QIODevice::WriteOnly) || file.write(fixture.data[i])!=fixture.data[i].size())throw std::runtime_error("Cannot create loopback seed fixture");all+=fixture.data[i];}
        lt::settings_pack settings;settings.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");settings.set_bool(lt::settings_pack::enable_dht,false);settings.set_bool(lt::settings_pack::enable_lsd,false);settings.set_bool(lt::settings_pack::enable_upnp,false);settings.set_bool(lt::settings_pack::enable_natpmp,false);
        lt::session seed(settings);lt::error_code error;lt::add_torrent_params params;params.ti.reset(new lt::torrent_info(fixture.torrent.constData(),fixture.torrent.size(),error));if(error)throw std::runtime_error(error.message());
        params.save_path=seedPath.toStdString();params.flags=lt::torrent_flags::seed_mode|lt::torrent_flags::override_web_seeds;
        seed.add_torrent(params,error);if(error)throw std::runtime_error(error.message());
        QElapsedTimer timer;timer.start();while(seed.listen_port()==0 && timer.elapsed()<5000)QThread::msleep(30);
        if(!seed.listen_port())throw std::runtime_error("No loopback seed port");
        QVector<int> pieces;for(int i=0;i<catalog.hashes.size()/20;++i)pieces.append(i);
        std::atomic<bool> stop(false);OnlinePackages::Progress progress;progress.total=all.size();
        bool paused=false;
        try { OnlinePackages::fetchPeerBlocks(catalog,pieces,cache,stop,[&](const OnlinePackages::Progress &p){if(p.verified>0)stop.store(true);},progress,QStringLiteral("127.0.0.1:%1").arg(seed.listen_port())); }
        catch(const std::exception &error) { paused=QString::fromUtf8(error.what()).contains(QStringLiteral("paused")); }
        if(!paused || progress.verified<=0 || progress.verified>=all.size())throw std::runtime_error("Pause did not preserve a partial verified transfer");
        stop.store(false);QVector<int> remaining;
        for(int i:pieces)if(!QFileInfo(QDir(cache).filePath(QString::number(i)+QStringLiteral(".block"))).isFile())remaining.append(i);
        if(remaining.size()>=pieces.size())throw std::runtime_error("No verified blocks retained for resume");
        std::printf("Paused after a verified block; resume requests only missing blocks.\n");
        OnlinePackages::fetchPeerBlocks(catalog,remaining,cache,stop,[](const OnlinePackages::Progress &p){std::fprintf(stdout,"%lld / %lld verified bytes\n",static_cast<long long>(p.verified),static_cast<long long>(p.total));std::fprintf(stdout,"%s %s\n",p.message.toUtf8().constData(),p.diagnostic.toUtf8().constData());std::fflush(stdout);},progress,QStringLiteral("127.0.0.1:%1").arg(seed.listen_port()));
        for(int i:pieces){QFile file(QDir(cache).filePath(QString::number(i)+QStringLiteral(".block")));if(!file.open(QIODevice::ReadOnly) || file.readAll()!=all.mid(i*catalog.pieceLength,catalog.pieceLength))throw std::runtime_error("Incorrect peer block bytes");}
        if(progress.verified!=all.size())throw std::runtime_error("Incorrect verified byte progress");
        QDirIterator files(cache,QStringList()<<QStringLiteral("*.exe")<<QStringLiteral("*.dll"),QDir::Files,QDirIterator::Subdirectories);if(files.hasNext())throw std::runtime_error("Executable output in peer cache");
        seed.pause();OnlinePackages::setSharingEnabled(true);OnlinePackages::shareVerified(catalog,cache,true);timer.restart();while(!OnlinePackages::sharingPort() && timer.elapsed()<5000)QThread::msleep(30);if(!OnlinePackages::sharingPort())throw std::runtime_error("Persistent sharing did not open a loopback port");
        const QString secondCache=root.filePath(QStringLiteral("second-cache"));QDir().mkpath(secondCache);OnlinePackages::Progress secondProgress;secondProgress.total=all.size();
        OnlinePackages::fetchPeerBlocks(catalog,pieces,secondCache,stop,OnlinePackages::Sink(),secondProgress,QStringLiteral("127.0.0.1:%1").arg(OnlinePackages::sharingPort()));
        if(secondProgress.verified!=all.size())throw std::runtime_error("Persistent sharing could not serve all verified blocks");OnlinePackages::stopSharing();if(OnlinePackages::sharingPort())throw std::runtime_error("Sharing did not stop");
        std::printf("Persistent sharing served %lld verified bytes to a second independent loopback downloader; stop released the session.\n",static_cast<long long>(secondProgress.verified));
        std::printf("Peer protocol test passed: all blocks independently verified; loopback only, no installation.\n");return 0;
    }catch(const std::exception &e){std::fprintf(stderr,"Peer test failed: %s\n",e.what());return 1;}
}
