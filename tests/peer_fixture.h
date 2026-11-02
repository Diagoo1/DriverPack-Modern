#ifndef DRIVERPACK_PEER_TEST_FIXTURE_H
#define DRIVERPACK_PEER_TEST_FIXTURE_H
#include <QByteArray>
#include <QVector>
#include <QMap>
#include <QUrl>
#include <QCryptographicHash>
#include <stdexcept>
static QByteArray string(const QByteArray &s){return QByteArray::number(s.size())+':'+s;}
static QByteArray number(qint64 n){return 'i'+QByteArray::number(n)+'e';}
static QByteArray dictionary(const QMap<QByteArray,QByteArray> &entries){QByteArray out("d");for(auto it=entries.constBegin();it!=entries.constEnd();++it)out+=string(it.key())+it.value();return out+'e';}
struct Fixture {
    QVector<QByteArray> names,data;
    QByteArray torrent;
    bool peerOnly=false;
    Fixture(){names<<QByteArray("docs/readme.txt")<<QByteArray("docs/unused-index.bin")<<QByteArray("drivers/DP_Test.7z")<<QByteArray("tools/ignored.exe");data<<QByteArray(9000,'a')<<(QByteArray::fromHex("53445705020000")+QByteArray(11993,'b'))<<(QByteArray::fromHex("377abcaf271c")+QByteArray(69994,'c'))<<QByteArray(4000,'d');rebuild();}
    void rebuild(){QByteArray files("l"),all;
        for(int i=0;i<names.size();++i){QByteArray path("l");for(const QByteArray &p:names[i].split('/'))path+=string(p);path+='e';QMap<QByteArray,QByteArray> file;file["length"]=number(data[i].size());file["path"]=path;files+=dictionary(file);all+=data[i];}files+='e';
        QByteArray hashes;for(int start=0;start<all.size();start+=16384)hashes+=QCryptographicHash::hash(all.mid(start,16384),QCryptographicHash::Sha1);
        QMap<QByteArray,QByteArray> info;info["files"]=files;info["name"]=string("DriverPack_Test");info["piece length"]=number(16384);info["pieces"]=string(hashes);
        QMap<QByteArray,QByteArray> root;root["info"]=dictionary(info);if(!peerOnly)root["url-list"]=string("http://127.0.0.1:9000/base/");torrent=dictionary(root);
    }
    QByteArray fetch(const QUrl &url,qint64 start,qint64 size,qint64 total){
        const QString prefix=QStringLiteral("/base/DriverPack_Test/");if(!url.path().startsWith(prefix))throw std::runtime_error("wrong URL root");
        const QByteArray path=url.path().mid(prefix.size()).toUtf8();const int i=names.indexOf(path);
        if(i<0 || total!=data[i].size() || start<0 || start+size>total)throw std::runtime_error("wrong range");return data[i].mid(static_cast<int>(start),static_cast<int>(size));
    }
};

#endif
