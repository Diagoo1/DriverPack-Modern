// SPDX-License-Identifier: GPL-3.0-or-later
// Reads data only. Never loads or evaluates DriverPack JavaScript, programs or hooks.
#include "driverpack_catalog.h"
#include "sqlite/sqlite3.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <stdexcept>
#include <cmath>
#include <QDate>
namespace driverpack
{
static void fail(const QString &message) { throw std::runtime_error(message.toUtf8().constData()); }
static QString text(sqlite3_stmt *stmt, int col)
{
    const unsigned char *value = sqlite3_column_text(stmt, col);
    return value ? QString::fromUtf8(reinterpret_cast<const char *>(value)) : QString();
}
struct Statement
{
    sqlite3_stmt *value = 0;
    ~Statement()
    {
        if (value)
            sqlite3_finalize(value);
    }
};
int Catalog::interrupt(void *self)
{
    return static_cast<Catalog *>(self)->m_deadline.elapsed() > 5000 ? 1 : 0;
}
Catalog::Catalog(const QString &file) : m_db(0), m_path(QFileInfo(file).canonicalFilePath())
{
    QFile header(m_path);
    if (!header.open(QIODevice::ReadOnly) || header.read(16) != QByteArray("SQLite format 3\0", 16))
        fail(QStringLiteral("DriverPack index is not a SQLite database."));
    header.close();
    if (sqlite3_open_v2(m_path.toUtf8().constData(), &m_db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, 0) !=
        SQLITE_OK)
    {
        QString error =
            m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QStringLiteral("Unknown SQLite failure");
        if (m_db)
            sqlite3_close(m_db);
        m_db = 0;
        fail(error);
    }
    sqlite3_busy_timeout(m_db, 1000);
    sqlite3_limit(m_db, SQLITE_LIMIT_LENGTH, 256 * 1024);
    sqlite3_limit(m_db, SQLITE_LIMIT_SQL_LENGTH, 65536);
    sqlite3_exec(m_db, "PRAGMA query_only=ON; PRAGMA cache_size=-8192; PRAGMA mmap_size=0;", 0, 0, 0);
    Statement schema;
    const char *sql = "SELECT "
                      "d.deviceId,u.system,u.rank,s.build,s.sign,r.pack,r.directory,r.inf,r.allow,r.disallow,"
                      "r.installationHooks,r.dependencies FROM Devices d JOIN Usable u ON u.deviceId=d.id "
                      "JOIN Sections s ON s.id=u.sectionId JOIN Drivers r ON r.id=s.driverId LIMIT 0";
    if (sqlite3_prepare_v2(m_db, sql, -1, &schema.value, 0) != SQLITE_OK)
    {
        const QString error =
            QStringLiteral("Unsupported DriverPack index schema: ") + QString::fromUtf8(sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = 0;
        fail(error);
    }
    Statement fallbackSchema;
    Statement classSchema;
    m_supportDeviceClass =
        sqlite3_prepare_v2(m_db, "SELECT class FROM Devices LIMIT 0", -1, &classSchema.value, 0) == SQLITE_OK;
    m_supportFallback =
        sqlite3_prepare_v2(
            m_db, "SELECT d.mainId,u.failoverRank FROM Devices d JOIN Usable u ON u.deviceId=d.id LIMIT 0",
            -1, &fallbackSchema.value, 0) == SQLITE_OK;
    sqlite3_progress_handler(m_db, 2000, &Catalog::interrupt, this);
}
Catalog::~Catalog()
{
    if (m_db)
        sqlite3_close(m_db);
}
QVector<Match> Catalog::find(const QString &hardwareId, const QString &system, int build, int limit,
                             bool fallback, bool includeLowRank)
{
    if (hardwareId.isEmpty() || hardwareId.size() > 2048 || system.size() > 32 || limit < 1 || limit > 512)
        fail(QStringLiteral("Invalid catalog lookup."));
    if (fallback && !m_supportFallback)
        return QVector<Match>();
    m_deadline.restart();
    Statement statement;
    const QString rank = fallback ? QStringLiteral("u.failoverRank") : QStringLiteral("u.rank");
    const QString sql =
        QStringLiteral(
            "SELECT "
            "hex(d.id)||':'||hex(r.id)||':'||hex(s.id),d.deviceId,d.manufacturer,d.name,r.version,r.date,r."
            "pack,r.directory,"
            "r.inf,s.name,s.build,s.sign,%1,r.allow,r.disallow,r.installationHooks,r.dependencies,r.type,%4,"
            "d.id,s.id "
            "FROM Devices d JOIN Usable u ON u.deviceId=d.id JOIN Sections s ON s.id=u.sectionId JOIN "
            "Drivers r ON r.id=s.driverId WHERE %2=?1 AND u.system=?2 AND s.build<=?3 AND (%1>0 OR ?5=1) "
            "ORDER BY "
            "s.build DESC,%1 DESC,d.deviceId %3,r.date DESC,hex(r.id),hex(s.id) LIMIT ?4")
            .arg(rank, fallback ? QStringLiteral("d.mainId") : QStringLiteral("d.deviceId"),
                 fallback ? QStringLiteral("ASC") : QStringLiteral("DESC"),
                 m_supportDeviceClass ? QStringLiteral("d.class") : QStringLiteral("''"));
    if (sqlite3_prepare_v2(m_db, sql.toUtf8().constData(), -1, &statement.value, 0) != SQLITE_OK)
        fail(QString::fromUtf8(sqlite3_errmsg(m_db)));
    const QByteArray id = (fallback ? mainHardwareId(hardwareId) : normalizedHardwareId(hardwareId)).toUtf8(),
                     os = system.toUtf8();
    sqlite3_bind_text(statement.value, 1, id.constData(), id.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(statement.value, 2, os.constData(), os.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int(statement.value, 3, build);
    sqlite3_bind_int(statement.value, 4, limit);
    sqlite3_bind_int(statement.value, 5, includeLowRank ? 1 : 0);
    QVector<Match> matches;
    int result;
    while ((result = sqlite3_step(statement.value)) == SQLITE_ROW)
    {
        Match m;
        m.fallback = fallback;
        m.key = text(statement.value, 0);
        m.hardwareId = text(statement.value, 1);
        m.manufacturer = text(statement.value, 2);
        m.name = text(statement.value, 3);
        m.version = text(statement.value, 4);
        m.date = text(statement.value, 5).left(10);
        m.pack = text(statement.value, 6);
        m.directory = text(statement.value, 7);
        m.inf = text(statement.value, 8);
        m.section = text(statement.value, 9);
        m.build = sqlite3_column_int(statement.value, 10);
        m.signature = sqlite3_column_int(statement.value, 11);
        m.rank = sqlite3_column_double(statement.value, 12);
        m.allow = text(statement.value, 13);
        m.disallow = text(statement.value, 14);
        m.hooks = text(statement.value, 15);
        m.dependencies = text(statement.value, 16);
        m.type = text(statement.value, 17);
        m.deviceClass = text(statement.value, 18);
        const auto binary = [&](int column) {
            return QByteArray(static_cast<const char *>(sqlite3_column_blob(statement.value, column)),
                              sqlite3_column_bytes(statement.value, column));
        };
        m.groupKey = originalGroupKey(binary(19), binary(20));
        matches.append(m);
    }
    if (result != SQLITE_DONE)
        fail(QStringLiteral("Catalog query failed or exceeded its time limit: ") +
             QString::fromUtf8(sqlite3_errmsg(m_db)));
    return matches;
}
bool originalDriverClassVisible(const Match &match)
{
    // SystemScanner.queryAPI in DriverPack 17 excludes these catalog classes
    // before prepareData. This is catalog metadata, not the Windows class name.
    return match.deviceClass.compare(QStringLiteral("inputdev"), Qt::CaseInsensitive) != 0 &&
           match.deviceClass.compare(QStringLiteral("touchpad"), Qt::CaseInsensitive) != 0;
}
static QString legacyBufferText(const QByteArray &bytes)
{
    // The supplied Node runtime replaces each invalid UTF-8 byte separately.
    // Qt/newer decoders instead consume an invalid prefix as one replacement.
    // DriverSelector.group uses String([deviceId, sectionId]), so this decoding
    // affects which alternatives the original actually keeps.
    QString result;
    for (int at = 0; at < bytes.size();)
    {
        const unsigned char first = static_cast<unsigned char>(bytes.at(at));
        int length = first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2
                                    : first >= 0xe0 && first <= 0xef ? 3
                                    : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        bool valid = length && at + length <= bytes.size();
        for (int i = 1; valid && i < length; ++i)
        {
            const unsigned char next = static_cast<unsigned char>(bytes.at(at + i));
            valid = next >= 0x80 && next <= 0xbf;
            if (i == 1)
                valid = valid && !(first == 0xe0 && next < 0xa0) &&
                        !(first == 0xed && next > 0x9f) && !(first == 0xf0 && next < 0x90) &&
                        !(first == 0xf4 && next > 0x8f);
        }
        if (valid)
        {
            result += QString::fromUtf8(bytes.constData() + at, length);
            at += length;
        }
        else
        {
            result += QChar(0xfffd);
            ++at;
        }
    }
    return result;
}
QString originalGroupKey(const QByteArray &deviceId, const QByteArray &sectionId)
{
    return legacyBufferText(deviceId) + QLatin1Char(',') + legacyBufferText(sectionId);
}
QString normalizedDriverDate(const QString &value)
{
    for (const QString &format : {QStringLiteral("yyyy-MM-dd"), QStringLiteral("M-d-yyyy"),
                                  QStringLiteral("M/d/yyyy"), QStringLiteral("yyyyMMdd")})
    {
        QDate date = QDate::fromString(value.trimmed().left(10), format);
        if (date.isValid())
            return date.toString(QStringLiteral("yyyy-MM-dd"));
    }
    return QString();
}
unsigned driverState(bool installed, const QString &currentVersion, const QString &currentDate,
                     const QString &availableVersion, const QString &availableDate)
{
    if (!installed)
        return 0x10;
    const QString oldDate = normalizedDriverDate(currentDate), newDate = normalizedDriverDate(availableDate);
    if ((!currentVersion.isEmpty() && currentVersion == availableVersion) ||
        (!oldDate.isEmpty() && oldDate == newDate))
        return 0x40;
    if (oldDate.isEmpty() || newDate.isEmpty())
        return 0x8;
    return oldDate < newDate ? 0x20 : 0x80;
}
QString mainHardwareId(const QString &input)
{
    QString id = normalizedHardwareId(input);
    QRegularExpression pattern(QStringLiteral(R"DP(^(.+(?:VEN|VID).*(?:DEV|PID)[^&\\]*))DP"));
    const auto match = pattern.match(id);
    if (match.hasMatch())
        return match.captured(1);
    return id.section(QLatin1Char('\\'), 0, 1);
}
QString normalizedHardwareId(const QString &id)
{
    QString value = id.trimmed().toUpper();
    if (value.startsWith(QLatin1Char('*')))
    {
        value.remove(0, 1);
        value = QStringLiteral("ACPI\\") + value.trimmed();
    }
    const QRegularExpression alias(QStringLiteral("^ACPI\\\\VEN_([0-9A-Z]+)&DEV_([0-9A-Z]+)"));
    const auto match = alias.match(value);
    if (match.hasMatch())
        value.replace(0, match.capturedLength(),
                      QStringLiteral("ACPI\\") + match.captured(1) + match.captured(2));
    return value;
}
bool higherPriority(const Match &a, const Match &b)
{
    if (a.build != b.build)
        return a.build > b.build;
    const double x = std::isfinite(a.rank) ? a.rank : -1, y = std::isfinite(b.rank) ? b.rank : -1;
    if (x != y)
        return x > y;
    if (a.hardwareId != b.hardwareId)
        return a.hardwareId > b.hardwareId;
    if (a.date != b.date)
        return a.date > b.date;
    return a.key < b.key;
}
bool safeRelative(const QString &path, bool archive)
{
    if (path.isEmpty() || path.size() > 2048 || QDir::isAbsolutePath(path) ||
        path.contains(QRegularExpression(QStringLiteral("[:\\\"\r\n\t*?<>|]"))))
        return false;
    for (const QChar c : path)
        if (c.unicode() < 32)
            return false;
    const QString normalized = QDir::fromNativeSeparators(path);
    const QStringList parts = normalized.split(QLatin1Char('/'));
    for (const QString &part : parts)
    {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..") ||
            part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' ')) || part.contains(QChar(0)) ||
            part.contains(QRegularExpression(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
                                             QRegularExpression::CaseInsensitiveOption)))
            return false;
    }
    return !archive || parts.size() == 1;
}
static bool emptyData(QString value)
{
    value = value.trimmed();
    if (value.isEmpty() || value == QStringLiteral("null"))
        return true;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(value.toUtf8(), &error);
    return error.error == QJsonParseError::NoError &&
           ((doc.isArray() && doc.array().isEmpty()) || (doc.isObject() && doc.object().isEmpty()));
}
bool hasInstallerRules(const Match &m) { return !emptyData(m.hooks) || !emptyData(m.dependencies); }
bool platformApplicable(const Match &m, const QStringList &markers)
{
    auto list = [](const QString &raw, QStringList *values)
    {
        if (emptyData(raw))
            return true;
        QJsonParseError error;
        auto data = QJsonDocument::fromJson(raw.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !data.isArray())
            return false;
        for (const auto &value : data.array())
        {
            if (!value.isString())
                return false;
            values->append(value.toString().toLower());
        }
        return true;
    };
    QStringList allow, deny;
    if (!list(m.allow, &allow) || !list(m.disallow, &deny))
        return false;
    QStringList normalized;
    for (const auto &marker : markers)
        normalized << marker.toLower();
    for (const auto &required : allow)
        if (!normalized.contains(required))
            return false;
    for (const auto &blocked : deny)
        if (normalized.contains(blocked))
            return false;
    return true;
}
bool hasPlatformRestrictions(const Match &m)
{
    return !emptyData(m.allow) || !emptyData(m.disallow) || !emptyData(m.hooks) || !emptyData(m.dependencies);
}
int compareVersions(const QString &a, const QString &b, bool *valid)
{
    const QStringList left = a.split(QLatin1Char('.')), right = b.split(QLatin1Char('.'));
    bool ok = !a.isEmpty() && !b.isEmpty() && left.size() <= 4 && right.size() <= 4;
    quint32 x[4] = {}, y[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        bool first = true, second = true;
        if (i < left.size())
            x[i] = left[i].toUInt(&first);
        if (i < right.size())
            y[i] = right[i].toUInt(&second);
        ok = ok && first && second && x[i] <= 65535 && y[i] <= 65535;
    }
    if (valid)
        *valid = ok;
    if (!ok)
        return 0;
    for (int i = 0; i < 4; ++i)
        if (x[i] != y[i])
            return x[i] > y[i] ? 1 : -1;
    return 0;
}
} // namespace driverpack
