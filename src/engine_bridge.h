// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SDIOMODERN_ENGINE_BRIDGE_H
#define SDIOMODERN_ENGINE_BRIDGE_H
#include <QObject>
#include "device_scanner.h"
#include "origin_worker.h"
class EngineBridge : public QObject {
    Q_OBJECT
public:
    explicit EngineBridge(QObject* parent=0);
    ~EngineBridge();
    bool isBusy() const;
    bool hasDriverPackCatalog() const;
    bool canInstall() const;
    void configure(const QString& drivers,const QString& indexes,const QString& logs);
    void requestInventory(bool demo);
    void setPreviewTarget(int version,int architecture);
    int previewVersion()const;
    int previewArchitecture()const;
    void requestLoadSnapshot(const QString &file);
    bool requestInstallation(const sdiomodern::InstallPlan& plan);
    void requestStopAfterCurrent();
    void requestSnapshot(const QString& file);
    void requestExtraction(const sdiomodern::InstallPlan&,const QString &folder);
    void requestRestorePoint();
    void requestBackup(const QString &folder);
    void requestIndexMaintenance(bool rebuild,const QString &exportFolder=QString());
    void setSessionOptions(const sdiomodern::Paths&);
signals:
    void inventoryReady(const QVector<DeviceInfo>& devices,const QString& message);
    void nativeSnapshotReady(const sdiomodern::Snapshot& snapshot);
    void nativeProgress(const sdiomodern::Event& event);
    void nativeInstallationFinished(const sdiomodern::InstallReport& report);
    void engineError(const QString& error);
    void snapshotSaved(const QString& path);
    void toolFinished(const QString &message);
private slots:
    void completed();
private:
    DeviceScanner* m_scanner;
    OriginWorker* m_worker;
    bool m_pending;
};
#endif
