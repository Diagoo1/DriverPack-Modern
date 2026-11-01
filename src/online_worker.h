// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DRIVERPACK_ONLINE_WORKER_H
#define DRIVERPACK_ONLINE_WORKER_H
#include "online_packages.h"
#include <QThread>
class OnlineWorker : public QThread
{
    Q_OBJECT
  public:
    explicit OnlineWorker(QObject *parent = 0) : QThread(parent), checking(true), stop(false) {}
    bool checking;
    std::atomic<bool> stop;
    OnlinePackages::Catalog catalog;
    QByteArray imported;
    QVector<int> selected;
    QString drivers, indexes, cache, error;
    QUrl metadataUrl = OnlinePackages::officialMetadataUrl();
    QStringList published;
  signals:
    void activity(const QString &message, qint64 verified, qint64 total, int published);
    void transferStats(qint64 received, qint64 downloadRate, qint64 uploadRate, int peers);
    void fileProgress(int index, qint64 verified, qint64 total);

  protected:
    void run() Q_DECL_OVERRIDE;
};

#endif
