// SPDX-License-Identifier: GPL-3.0-or-later
#include "online_worker.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QElapsedTimer>
#include <stdexcept>
void OnlineWorker::run()
{
    error.clear();
    published.clear();
    QFile journal;
    if (!checking)
    {
        QDir().mkpath(cache);
        journal.setFileName(QDir(cache).filePath(QStringLiteral("update-events.jsonl")));
        journal.open(QIODevice::WriteOnly | QIODevice::Append);
    }
    const auto record = [&journal](const QString &message, const QString &detail = QString())
    {
        if (!journal.isOpen())
            return;
        QJsonObject event;
        event.insert(QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        event.insert(QStringLiteral("message"), message);
        event.insert(QStringLiteral("detail"), detail);
        journal.write(QJsonDocument(event).toJson(QJsonDocument::Compact));
        journal.write("\n");
        journal.flush();
    };
    record(checking ? QStringLiteral("Reading update catalog")
                    : QStringLiteral("Starting verified package download"));
    QElapsedTimer clock;
    clock.start();
    qint64 lastTime = 0, lastReceived = 0, rate = 0;
    QMap<int, qint64> shown;
    QString lastMessage;
    try
    {
        if (stop.load())
            throw std::runtime_error("Download paused. Verified blocks were kept for resuming.");
        if (checking)
        {
            emit activity(QStringLiteral("Reading the selected DriverPack catalog..."), 0, 0, 0);
            const QByteArray bytes =
                imported.isEmpty() ? OnlinePackages::fetchMetadata(metadataUrl, stop) : imported;
            catalog = OnlinePackages::parse(bytes);
        }
        else
            published = OnlinePackages::download(
                catalog, selected, drivers, indexes, cache, stop,
                [&](const OnlinePackages::Progress &p)
                {
                    if (p.message != lastMessage || !p.diagnostic.isEmpty())
                    {
                        record(p.message, p.diagnostic);
                        lastMessage = p.message;
                    }
                    const qint64 now = clock.elapsed();
                    if (now - lastTime >= 200)
                    {
                        const qint64 sample = qMax(qint64(0), p.received - lastReceived) * 1000 /
                                              qMax(qint64(1), now - lastTime);
                        rate = rate ? (rate * 2 + sample) / 3 : sample;
                        lastTime = now;
                        lastReceived = p.received;
                    }
                    emit transferStats(p.received, p.downloadRate >= 0 ? p.downloadRate : rate, p.uploadRate,
                                       p.peers);
                    emit activity(p.message, p.verified, p.total, p.published);
                    for (auto it = p.fileVerified.constBegin(); it != p.fileVerified.constEnd(); ++it)
                        if (shown.value(it.key(), -1) != it.value())
                        {
                            shown[it.key()] = it.value();
                            emit fileProgress(it.key(), it.value(), catalog.files[it.key()].size);
                        }
                });
    }
    catch (const std::exception &e)
    {
        error = QString::fromUtf8(e.what());
    }
    record(error.isEmpty() ? QStringLiteral("Operation completed") : error);
}
