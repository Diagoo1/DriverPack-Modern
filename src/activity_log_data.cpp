#include "activity_log.h"
#include <QFile>
#include <QJsonDocument>
namespace ActivityLog
{
QVector<QJsonObject> readRecent(const QString &path, int maxRows, qint64 maxBytes)
{
    QVector<QJsonObject> rows;
    if (maxRows < 1 || maxRows > 10000 || maxBytes < 1 || maxBytes > 16 * 1024 * 1024)
        return rows;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return rows;
    const qint64 start = qMax(qint64(0), file.size() - maxBytes);
    if (start)
    {
        file.seek(start - 1);
        if (file.read(1) != "\n")
            file.readLine(maxBytes);
    }
    const QByteArray tail = file.read(maxBytes);
    for (const auto &line : tail.split('\n'))
    {
        const auto doc = QJsonDocument::fromJson(line);
        if (!doc.isObject() || !doc.object().value(QStringLiteral("message")).isString())
            continue;
        rows << doc.object();
    }
    if (rows.size() > maxRows)
        rows.remove(0, rows.size() - maxRows);
    return rows;
}
} // namespace ActivityLog
