#include "../native/driverpack_catalog.h"
#include "../native/driverpack_devices.h"
#include "../native/origin_api.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QDir>
#include <QElapsedTimer>
#include <cstdio>
int main(int argc,char **argv){QCoreApplication app(argc,argv);const QStringList args=app.arguments();auto value=[&](const QString &name){const int i=args.indexOf(name);return i>=0&&i+1<args.size()?args.at(i+1):QString();};try{sdiomodern::OriginCore core;sdiomodern::Paths paths;paths.drivers=QDir(value(QStringLiteral("--library"))).filePath(QStringLiteral("drivers")).toStdWString();paths.indexes=QDir(value(QStringLiteral("--library"))).filePath(QStringLiteral("index")).toStdWString();QElapsedTimer clock;clock.start();const auto snapshot=core.scan(paths,sdiomodern::EventSink());int matches=0,eligible=0,local=0,recommended=0;for(const auto &device:snapshot.devices)for(const auto &candidate:device.candidates){++matches;if(candidate.valid)++eligible;if(candidate.local)++local;if(candidate.recommended)++recommended;}QJsonObject report;report.insert(QStringLiteral("devices"),int(snapshot.devices.size()));report.insert(QStringLiteral("candidates"),matches);report.insert(QStringLiteral("eligible"),eligible);report.insert(QStringLiteral("local"),local);report.insert(QStringLiteral("recommended"),recommended);report.insert(QStringLiteral("milliseconds"),double(clock.elapsed()));report.insert(QStringLiteral("windowsVersion"),snapshot.matchedWindowsVersion);report.insert(QStringLiteral("architecture"),snapshot.matchedArchitecture);report.insert(QStringLiteral("driverOrProgramExecuted"),false);const QByteArray json=QJsonDocument(report).toJson();const QString output=value(QStringLiteral("--output"));if(!output.isEmpty()){QSaveFile file(output);if(!file.open(QIODevice::WriteOnly)||file.write(json)!=json.size()||!file.commit())return 3;}fwrite(json.constData(),1,json.size(),stdout);return 0;}catch(const std::exception &e){fprintf(stderr,"DriverPack probe: %s\n",e.what());return 1;}}
