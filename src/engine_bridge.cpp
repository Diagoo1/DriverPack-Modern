// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine_bridge.h"
#include "offline_paths.h"
#include "crash_diagnostics.h"
EngineBridge::EngineBridge(QObject* p):QObject(p),m_scanner(new DeviceScanner(this)),m_worker(new OriginWorker(this)),m_pending(false)
{
    qRegisterMetaType<sdiomodern::Snapshot>("sdiomodern::Snapshot");
    qRegisterMetaType<sdiomodern::Event>("sdiomodern::Event");
    qRegisterMetaType<sdiomodern::InstallReport>("sdiomodern::InstallReport");
    connect(m_scanner,&DeviceScanner::inventoryReady,this,&EngineBridge::inventoryReady);
    connect(m_worker,&OriginWorker::progress,this,&EngineBridge::nativeProgress,Qt::QueuedConnection);
    connect(m_worker,&QThread::finished,this,&EngineBridge::completed,Qt::QueuedConnection);
}
EngineBridge::~EngineBridge(){m_worker->stopAfterCurrent();m_worker->wait();m_scanner->wait();}
bool EngineBridge::isBusy()const{return m_pending || m_worker->isRunning() || m_scanner->isRunning();}
bool EngineBridge::hasDriverPackCatalog()const {
#ifdef DRIVERPACK_CATALOG_LINKED
    return true;
#else
    return false;
#endif
}
bool EngineBridge::canInstall()const {
    if(m_worker->paths.targetVersion || m_worker->paths.targetArchitecture || m_worker->paths.disableInstall)return false;
#if defined(DRIVERPACK_CATALOG_LINKED) && defined(DRIVERPACK_INSTALL_AVAILABLE)
    return true;
#else
    return false;
#endif
}
void EngineBridge::configure(const QString& d,const QString& i,const QString& l){
    if(isBusy())return;m_worker->paths.drivers=d.toStdWString();m_worker->paths.indexes=resolveIndexDirectory(i,d).toStdWString();m_worker->paths.logDirectory=l.toStdWString();
}
void EngineBridge::requestInventory(bool demo){
    if(isBusy())return;
    if(demo){m_scanner->setDemo(true);m_scanner->start(QThread::LowPriority);return;}
    m_worker->paths.snapshotFile.clear();
    m_worker->resetRequest();m_worker->job=OriginWorker::Scan;m_pending=true;m_worker->start(QThread::NormalPriority);
}
int EngineBridge::previewVersion()const{return m_worker->paths.targetVersion;}
int EngineBridge::previewArchitecture()const{return m_worker->paths.targetArchitecture;}
void EngineBridge::setPreviewTarget(int version,int architecture){if(isBusy())return;m_worker->paths.targetVersion=version;m_worker->paths.targetArchitecture=architecture;}
void EngineBridge::requestLoadSnapshot(const QString &file){
    if(isBusy())return;
    m_worker->paths.snapshotFile=file.toStdWString();
    m_worker->resetRequest();m_worker->job=OriginWorker::Scan;m_pending=true;m_worker->start();
}
bool EngineBridge::requestInstallation(const sdiomodern::InstallPlan& plan){
    CrashDiagnostics::checkpoint(QStringLiteral("install-request count=%1 restore=%2 bypassConsent=%3 busy=%4 canInstall=%5")
        .arg(plan.selections.size()).arg(plan.createRestorePoint).arg(plan.continueOnRestoreFailure).arg(isBusy()).arg(canInstall()));
    if(isBusy() || !canInstall()){emit engineError(QStringLiteral("Native installation is not enabled in this build."));return false;}
    m_worker->resetRequest();m_worker->job=OriginWorker::Install;m_worker->plan=plan;m_pending=true;m_worker->start();return true;
}
void EngineBridge::requestStopAfterCurrent(){m_worker->stopAfterCurrent();}
void EngineBridge::requestSnapshot(const QString& path){
    if(isBusy())return;m_worker->resetRequest();m_worker->job=OriginWorker::SaveSnapshot;
    m_worker->snapshotFile=path.toStdWString();m_pending=true;m_worker->start(QThread::NormalPriority);
}
void EngineBridge::completed(){
    m_pending=false;
    m_worker->paths.forceReindex=false;m_worker->paths.indexTextDirectory.clear();
    if(!m_worker->error.isEmpty()){emit engineError(m_worker->error);return;}
    if(m_worker->job==OriginWorker::Scan)emit nativeSnapshotReady(m_worker->output);
    else if(m_worker->job==OriginWorker::Install){
        // A receiver may start another request (and reset worker-owned output).
        // Keep the emitted result alive and immutable throughout all receivers.
        const sdiomodern::InstallReport report=m_worker->installation;
        CrashDiagnostics::checkpoint(QStringLiteral("install-completed items=%1 restoreFailed=%2 restoreError=%3 cancelled=%4")
            .arg(report.items.size()).arg(report.restoreFailed).arg(report.restoreError).arg(report.cancelled));
        emit nativeInstallationFinished(report);
    }
    else if(m_worker->job==OriginWorker::SaveSnapshot)emit snapshotSaved(QString::fromStdWString(m_worker->snapshotFile));
    else emit toolFinished(m_worker->job==OriginWorker::Restore?QStringLiteral("Windows completed the restore point."):QString::fromStdWString(m_worker->snapshotFile));
}
void EngineBridge::requestIndexMaintenance(bool rebuild,const QString &folder){
    if(isBusy())return;
    m_worker->paths.forceReindex=rebuild;m_worker->paths.indexTextDirectory=folder.toStdWString();
    m_worker->resetRequest();m_worker->job=OriginWorker::Scan;m_pending=true;m_worker->start();
}
void EngineBridge::setSessionOptions(const sdiomodern::Paths &options){if(!isBusy())m_worker->paths=options;}
void EngineBridge::requestExtraction(const sdiomodern::InstallPlan& plan,const QString &folder){
    if(isBusy())return;
    m_worker->resetRequest();m_worker->job=OriginWorker::Extract;m_worker->plan=plan;
    m_worker->snapshotFile=folder.toStdWString();m_pending=true;m_worker->start();
}
void EngineBridge::requestRestorePoint(){
    if(isBusy())return;
    m_worker->resetRequest();m_worker->job=OriginWorker::Restore;m_pending=true;m_worker->start();
}
void EngineBridge::requestBackup(const QString &folder){
    if(isBusy())return;
    m_worker->resetRequest();m_worker->job=OriginWorker::Backup;
    m_worker->snapshotFile=folder.toStdWString();m_pending=true;m_worker->start();
}
