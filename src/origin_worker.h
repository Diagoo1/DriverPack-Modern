// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SDIOMODERN_ORIGIN_WORKER_H
#define SDIOMODERN_ORIGIN_WORKER_H
#include <QThread>
#include <QMetaType>
#include <QString>
#include <atomic>
#include <memory>
#include "../native/origin_api.h"
Q_DECLARE_METATYPE(sdiomodern::Snapshot)
Q_DECLARE_METATYPE(sdiomodern::Event)
Q_DECLARE_METATYPE(sdiomodern::InstallReport)
class OriginWorker : public QThread {
    Q_OBJECT
public:
    enum Job { Scan, Install, SaveSnapshot, Extract, Restore, Backup, DeviceList };
    explicit OriginWorker(QObject* parent=0);
    ~OriginWorker();
    Job job;
    sdiomodern::Paths paths;
    sdiomodern::InstallPlan plan;
    std::wstring snapshotFile;
    sdiomodern::Snapshot output;
    sdiomodern::InstallReport installation;
    QString error;
    void resetRequest();
    void stopAfterCurrent();
signals:
    void progress(const sdiomodern::Event& event);
protected:
    void run() Q_DECL_OVERRIDE;
private:
    std::atomic<bool> m_stop;
    std::unique_ptr<sdiomodern::OriginCore> m_core;
};
#endif
