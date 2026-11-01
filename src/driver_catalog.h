#ifndef SDIOMODERN_DRIVER_CATALOG_H
#define SDIOMODERN_DRIVER_CATALOG_H

#include <QObject>
#include <QVector>
#include <QStringList>
#include "device_types.h"
#include "../native/origin_api.h"

// Presentation record copied from either explicit demo fixtures or native DriverPack.
// Windows PnP enumeration alone never implies an update is available.
struct DriverCandidate
{
    QString version, date, provider, inf, section, hardwareId, packageSource;
    QString status;             // installed, recommended, alternative
    quint64 engineToken;
    quint32 statusBits, rawScore;
    double catalogRank=0;int minimumBuild=0,signature=0;
    int originalOrder;
    bool valid, local, recommended;
    QString matchReason;
    QVector<quint64> filterVisibility;
    int matchStars;             // 1..5 relative DriverPack priority; 0 = installed, invalid or unknown
    DriverCandidate() : engineToken(0), statusBits(0), rawScore(0), originalOrder(0), valid(false), local(false), recommended(false), matchStars(0) {}
};

struct DriverEntry
{
    DeviceInfo device;
    QVector<DriverCandidate> candidates;
    int chosenCandidate;        // -1 = none; positive index always names a package candidate with a native token
    bool checked;
    bool selectable;           // true only when the active backend allows this candidate
    bool catalogVerified;         // metadata originates from the in-process DriverPack scan
    quint32 engineDeviceStatus;
    bool installedDriverPresent;    // DriverPack reported an installed driver
    DriverEntry() : chosenCandidate(-1), checked(false), selectable(false), catalogVerified(false), engineDeviceStatus(0), installedDriverPresent(false) {}
};

int matchPriorityStars(const DriverEntry &entry,int candidateIndex);
bool isReinstallCandidate(const DriverEntry &entry,int candidateIndex);
int reinstallCandidateIndex(const DriverEntry &entry);
QString driverStatus(const DriverEntry &entry,int candidateIndex=-1);

class DriverCatalog : public QObject
{
    Q_OBJECT
public:
    explicit DriverCatalog(QObject *parent = 0);
    void replace(const QVector<DeviceInfo> &devices, bool demo);
    void replaceNative(const sdiomodern::Snapshot& snapshot);
    void invalidateNative();
    bool native() const { return m_native; }
    bool nativeInstallEnabled() const { return m_native && m_snapshot.generation!=0 && m_snapshot.capabilities.localInstall; }
    const sdiomodern::Snapshot& nativeSnapshot() const { return m_snapshot; }
    sdiomodern::InstallPlan buildPlan(bool restorePoint, bool confirmed=false, bool allowAlternatives=false) const;
    const QVector<DriverEntry> &entries() const { return m_entries; }
    DriverEntry entry(int index) const;
    const DriverEntry &entryRef(int index) const { return m_entries.at(index); }
    int size() const { return m_entries.size(); }
    bool demo() const { return m_demo; }
    bool setSelected(int row, bool selected);
    bool chooseCandidate(int row, int candidateIndex);
    void selectAll(bool selected);
    void selectRows(const QVector<int> &rows,bool selected);
    int catalogVerifiedCount() const;
    int selectedDrivers() const;
    int selectedReinstalls() const;
    int problems() const;
    QStringList selectedNames() const;
    bool canQueue(int row) const;
    bool isIgnored(int row)const;
    QStringList ignoredHardwareIds()const{return m_ignored;}
    void setIgnoredHardwareIds(const QStringList &ids);
    void toggleIgnored(int row);

signals:
    void changed();
    void ignoredChanged(const QStringList &ids);

private:
    QVector<DriverEntry> m_entries;
    bool m_demo;
    bool m_native;
    sdiomodern::Snapshot m_snapshot;
    QStringList m_ignored;
};

#endif
