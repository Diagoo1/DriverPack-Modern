#include "scan_report.h"
#include "origin_worker.h"
#include "offline_paths.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDir>
#include <QFileInfo>
#include <cstdio>

// A repeatable, non-installing diagnostic entry point for matching comparison.
// DriverPack SQLite catalogs are opened read-only and never rebuilt here.
int runScanReport(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    const QStringList args=app.arguments();
    const auto value=[&args](const QString &key)->QString {
        const int i=args.indexOf(key);
        return i>=0 && i+1<args.size()?args.at(i+1):QString();
    };
    const QString drivers=value(QStringLiteral("--drivers"));
    const QString indexes=value(QStringLiteral("--indexes"));
    const QString file=value(QStringLiteral("--scan-report"));
    if(drivers.isEmpty() || indexes.isEmpty() || file.isEmpty()){
        fprintf(stderr,"Required: --scan-report output.json --drivers folder --indexes folder\n");return 2;
    }
    OriginWorker worker;
    worker.paths.targetVersion=value(QStringLiteral("--target-windows")).toInt();worker.paths.targetArchitecture=value(QStringLiteral("--target-arch")).toInt();
    worker.paths.drivers=drivers.toStdWString();
    worker.paths.indexes=resolveIndexDirectory(indexes,drivers).toStdWString();
    worker.paths.logDirectory=QDir(QFileInfo(file).absolutePath()).filePath(QStringLiteral("logs")).toStdWString();
    worker.paths.snapshotFile=value(QStringLiteral("--load-snapshot")).toStdWString();
    worker.paths.forceReindex=args.contains(QStringLiteral("--reindex"));
    worker.paths.indexTextDirectory=value(QStringLiteral("--export-index-text")).toStdWString();
    worker.resetRequest();worker.start();worker.wait();
    if(worker.error.isEmpty() && args.contains(QStringLiteral("--repeat-scan"))){
        worker.resetRequest();worker.start();worker.wait();
    }
    const sdiomodern::Snapshot snapshot=worker.output;
    QString scanError=worker.error;
    const QString saved=value(QStringLiteral("--save-snapshot"));
    if(scanError.isEmpty() && !saved.isEmpty()){
        worker.resetRequest();worker.job=OriginWorker::SaveSnapshot;worker.snapshotFile=saved.toStdWString();
        worker.start();worker.wait();scanError=worker.error;
    }
    const QString extraction=value(QStringLiteral("--extract-first-to"));
    if(scanError.isEmpty() && !extraction.isEmpty()){
        sdiomodern::InstallPlan plan;plan.generation=snapshot.generation;plan.confirmed=true;plan.createRestorePoint=false;
        for(const sdiomodern::Device &d:snapshot.devices){
            for(const sdiomodern::Candidate &c:d.candidates)if(c.local && c.valid && c.recommended){
                sdiomodern::Selection selection;selection.deviceInstanceId=d.instanceId;selection.candidateToken=c.token;
                plan.selections.push_back(selection);break;
            }
            if(!plan.selections.empty())break;
        }
        worker.resetRequest();worker.job=OriginWorker::Extract;worker.plan=plan;worker.snapshotFile=extraction.toStdWString();
        worker.start();worker.wait();scanError=worker.error;
    }
    QJsonObject report;
    report.insert(QStringLiteral("sourceRevision"),QString::fromStdWString(snapshot.sourceRevision));
    report.insert(QStringLiteral("libraryFamily"),libraryFamily(drivers,resolveIndexDirectory(indexes,drivers)));report.insert(QStringLiteral("indexPolicy"),QStringLiteral("read-only-driverpack-sqlite"));
    report.insert(QStringLiteral("error"),scanError);
    report.insert(QStringLiteral("installationRequested"),false);
    report.insert(QStringLiteral("localInstallCapability"),snapshot.capabilities.localInstall);
    if(!extraction.isEmpty())report.insert(QStringLiteral("extractionOutput"),QString::fromStdWString(worker.snapshotFile));
    QJsonArray devices;
    for(const sdiomodern::Device &d:snapshot.devices){
        QJsonObject device;
        device.insert(QStringLiteral("instanceId"),QString::fromStdWString(d.instanceId));
        device.insert(QStringLiteral("name"),QString::fromStdWString(d.name));
        device.insert(QStringLiteral("installedVersion"),QString::fromStdWString(d.installed.version));
        device.insert(QStringLiteral("hasInstalledDriver"),d.hasInstalledDriver);
        device.insert(QStringLiteral("installedDate"),QString::fromStdWString(d.installed.date));
        device.insert(QStringLiteral("problemCode"),static_cast<double>(d.problemCode));
        device.insert(QStringLiteral("matchStatus"),static_cast<double>(d.matchStatus));
        QJsonArray candidates;
        for(const sdiomodern::Candidate &c:d.candidates){
            QJsonObject candidate;
            candidate.insert(QStringLiteral("catalogRank"),c.catalogRank);candidate.insert(QStringLiteral("minimumBuild"),c.minimumBuild);candidate.insert(QStringLiteral("signature"),c.signature);candidate.insert(QStringLiteral("eligibilityReason"),QString::fromStdWString(c.reason));
            candidate.insert(QStringLiteral("version"),QString::fromStdWString(c.version));
            candidate.insert(QStringLiteral("date"),QString::fromStdWString(c.date));
            candidate.insert(QStringLiteral("inf"),QString::fromStdWString(c.inf));
            candidate.insert(QStringLiteral("hardwareId"),QString::fromStdWString(c.hardwareId));
            candidate.insert(QStringLiteral("package"),QString::fromStdWString(c.packageName));
            candidate.insert(QStringLiteral("statusBits"),static_cast<double>(c.statusBits));
            candidate.insert(QStringLiteral("rawScore"),static_cast<double>(c.rawScore));
            candidate.insert(QStringLiteral("originalOrder"),c.originalOrder);
            candidate.insert(QStringLiteral("valid"),c.valid);
            candidate.insert(QStringLiteral("local"),c.local);
            candidate.insert(QStringLiteral("recommended"),c.recommended);
            if(args.contains(QStringLiteral("--filter-audit"))){QJsonArray words;for(std::uint64_t word:c.filterVisibility)words.append(QString::number(static_cast<qulonglong>(word),16));candidate.insert(QStringLiteral("filterVisibility"),words);}
            candidates.append(candidate);
        }
        device.insert(QStringLiteral("candidates"),candidates);devices.append(device);
    }
    report.insert(QStringLiteral("devices"),devices);
    if(args.contains(QStringLiteral("--target-windows")) || args.contains(QStringLiteral("--target-arch"))){report.insert(QStringLiteral("matchedWindowsVersion"),snapshot.matchedWindowsVersion);report.insert(QStringLiteral("matchedArchitecture"),snapshot.matchedArchitecture);}
    QSaveFile out(file);
    const QByteArray data=QJsonDocument(report).toJson();
    if(!out.open(QIODevice::WriteOnly) || out.write(data)!=data.size() || !out.commit())return 3;
    return scanError.isEmpty()?0:1;
}
