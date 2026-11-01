// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DRIVERPACK_MODERN_CATALOG_H
#define DRIVERPACK_MODERN_CATALOG_H
#include <QString>
#include <QVector>
#include <QStringList>
#include <QElapsedTimer>
#include <memory>
struct sqlite3;
namespace driverpack
{
struct Match
{
    QString key, groupKey, hardwareId, manufacturer, name, version, date, pack, directory, inf, section, allow,
        disallow, hooks, dependencies, type, deviceClass;
    int build = 0, signature = 0;
    double rank = 0;
    bool fallback = false;
};
class Catalog
{
  public:
    explicit Catalog(const QString &file);
    ~Catalog();
    QVector<Match> find(const QString &hardwareId, const QString &system, int build, int limit = 256,
                        bool fallback = false, bool includeLowRank = false);
    QString path() const { return m_path; }

  private:
    bool m_supportFallback = false;
    bool m_supportDeviceClass = false;
    sqlite3 *m_db;
    QString m_path;
    QElapsedTimer m_deadline;
    static int interrupt(void *self);
    Catalog(const Catalog &) = delete;
    Catalog &operator=(const Catalog &) = delete;
};
QString normalizedDriverDate(const QString &date);
unsigned driverState(bool installed, const QString &currentVersion, const QString &currentDate,
                     const QString &availableVersion, const QString &availableDate);
QString mainHardwareId(const QString &id);
bool platformApplicable(const Match &match, const QStringList &markers);
bool hasInstallerRules(const Match &match);
bool originalDriverClassVisible(const Match &match);
QString originalGroupKey(const QByteArray &deviceId, const QByteArray &sectionId);
QString normalizedHardwareId(const QString &id);
bool higherPriority(const Match &a, const Match &b);
bool safeRelative(const QString &path, bool archive = false);
bool hasPlatformRestrictions(const Match &match);
int compareVersions(const QString &a, const QString &b, bool *valid = 0);
} // namespace driverpack
#endif
