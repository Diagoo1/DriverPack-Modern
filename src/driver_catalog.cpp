#include "driver_catalog.h"
#include <QMap>
#include "ui_language.h"

// DriverPack's original displayed rank: 1 + round(4 * rank / 3), bounded to five.
// The numeric catalog rank is presentation data, never a safety certificate.
int matchPriorityStars(const DriverEntry &entry, int index)
{
    if (index <= 0 || index >= entry.candidates.size())
        return 0;
    const auto &candidate = entry.candidates[index];
    if (!candidate.valid)
        return 0;
    return qBound(1, 1 + qRound(4 * candidate.catalogRank / 3), 5);
}

DriverCatalog::DriverCatalog(QObject *parent) : QObject(parent), m_demo(false), m_native(false) {}
static QString normalizedVersion(QString value)
{
    QStringList parts = value.trimmed().split(QLatin1Char('.'));
    if (parts.isEmpty() || parts.size() > 4)
        return QString();
    for (QString &part : parts)
    {
        bool valid = false;
        const uint number = part.toUInt(&valid);
        if (!valid || number > 65535)
            return QString();
        part = QString::number(number);
    }
    while (parts.size() < 4)
        parts.append(QStringLiteral("0"));
    return parts.join(QLatin1Char('.'));
}
bool isReinstallCandidate(const DriverEntry &entry, int index)
{
    if (index <= 0 || index >= entry.candidates.size() || !entry.installedDriverPresent)
        return false;
    const DriverCandidate &candidate = entry.candidates[index];
    const QString installed = normalizedVersion(entry.device.driverVersion),
                  available = normalizedVersion(candidate.version);
    return candidate.valid &&
           ((!installed.isEmpty() && installed == available) ||
            ((installed.isEmpty() || available.isEmpty()) && (candidate.statusBits & 0x040u) != 0));
}
int reinstallCandidateIndex(const DriverEntry &entry)
{
    for (int i = 1; i < entry.candidates.size(); ++i)
        if (isReinstallCandidate(entry, i) && entry.candidates[i].local)
            return i;
    return -1;
}
int DriverCatalog::selectedReinstalls() const
{
    int count = 0;
    for (const DriverEntry &entry : m_entries)
        if (entry.checked && isReinstallCandidate(entry, entry.chosenCandidate))
            ++count;
    return count;
}
QString driverStatus(const DriverEntry &entry, int candidateIndex)
{
    if (candidateIndex == 0)
        return entry.checked && isReinstallCandidate(entry, entry.chosenCandidate)
                   ? QStringLiteral("Reinstall")
                   : QStringLiteral("Installed");
    const bool installed =
        entry.catalogVerified ? entry.installedDriverPresent : !entry.device.driverVersion.isEmpty();
    if (!installed)
        return QStringLiteral("Missing");
    int index = candidateIndex > 0 ? candidateIndex : entry.chosenCandidate;
    if (index <= 0 || index >= entry.candidates.size())
    {
        index = -1;
        for (int i = 1; i < entry.candidates.size(); ++i)
            if (entry.candidates[i].valid && entry.candidates[i].recommended)
            {
                index = i;
                break;
            }
    }
    if (index > 0 && index < entry.candidates.size())
    {
        const DriverCandidate &c = entry.candidates[index];
        if (!c.valid && entry.catalogVerified)
            return QStringLiteral("Invalid");
        if (entry.checked && index == entry.chosenCandidate && isReinstallCandidate(entry, index))
            return QStringLiteral("Reinstall");
        if (c.statusBits & 0x020)
            return QStringLiteral("Upgrade");
        if (c.statusBits & 0x080)
            return QStringLiteral("Downgrade");
        if (!entry.catalogVerified && c.status == QStringLiteral("recommended"))
            return QStringLiteral("Upgrade");
    }
    return QStringLiteral("Installed");
}

DriverEntry DriverCatalog::entry(int index) const
{
    return index >= 0 && index < m_entries.size() ? m_entries.at(index) : DriverEntry();
}

void DriverCatalog::replace(const QVector<DeviceInfo> &devices, bool demo)
{
    m_demo = demo;
    m_native = false;
    m_snapshot = sdiomodern::Snapshot();
    m_entries.clear();
    m_entries.reserve(devices.size());
    for (int i = 0; i < devices.size(); ++i)
    {
        DriverEntry entry;
        entry.device = devices.at(i);
        DriverCandidate installed;
        installed.status = QStringLiteral("installed");
        installed.version = entry.device.driverVersion;
        installed.provider = entry.device.manufacturer;
        installed.hardwareId = entry.device.hardwareId;
        entry.candidates.append(installed);
        if (demo)
        {
            // Illustrative values only; NEVER present these as matches for real devices.
            DriverCandidate recommended;
            recommended.status = QStringLiteral("recommended");
            recommended.version = QStringLiteral("10.1.%1.2026").arg(200 + i);
            recommended.date = QStringLiteral("10/10/2026");
            recommended.provider = entry.device.manufacturer;
            recommended.hardwareId = entry.device.hardwareId;
            recommended.inf = QStringLiteral("demo_driver_%1.inf").arg(i);
            recommended.section = QStringLiteral("DemoInstall.NT");
            recommended.packageSource = QStringLiteral("DEMO_LOCAL_PACKAGE");
            recommended.matchStars = 5;
            entry.candidates.append(recommended);
            DriverCandidate alternative = recommended;
            alternative.status = QStringLiteral("alternative");
            alternative.version = QStringLiteral("9.8.%1.2025").arg(100 + i);
            alternative.date = QStringLiteral("08/07/2025");
            alternative.matchStars = 3;
            entry.candidates.append(alternative);
            entry.chosenCandidate = 1;
            entry.selectable = true;
        }
        m_entries.append(entry);
    }
    emit changed();
}

bool DriverCatalog::canQueue(int row) const
{
    return row >= 0 && row < m_entries.size() && !isIgnored(row) && (m_demo || nativeInstallEnabled()) &&
           m_entries.at(row).selectable;
}
bool DriverCatalog::isIgnored(int row) const
{
    return row >= 0 && row < m_entries.size() && !m_entries[row].device.hardwareId.isEmpty() &&
           m_ignored.contains(m_entries[row].device.hardwareId, Qt::CaseInsensitive);
}
void DriverCatalog::setIgnoredHardwareIds(const QStringList &ids)
{
    m_ignored = ids;
    m_ignored.removeAll(QString());
    m_ignored.removeDuplicates();
    for (int row = 0; row < m_entries.size(); ++row)
        if (isIgnored(row))
            m_entries[row].checked = false;
    emit ignoredChanged(m_ignored);
    emit changed();
}
void DriverCatalog::toggleIgnored(int row)
{
    if (row < 0 || row >= m_entries.size() || m_entries[row].device.hardwareId.isEmpty())
        return;
    QStringList ids = m_ignored;
    const QString id = m_entries[row].device.hardwareId;
    if (isIgnored(row))
    {
        for (int i = ids.size() - 1; i >= 0; --i)
            if (ids[i].compare(id, Qt::CaseInsensitive) == 0)
                ids.removeAt(i);
    }
    else
        ids.append(id);
    setIgnoredHardwareIds(ids);
}

bool DriverCatalog::setSelected(int row, bool selected)
{
    if (row < 0 || row >= m_entries.size() || (selected && !canQueue(row)))
        return false;
    DriverEntry &d = m_entries[row];
    if (selected && (d.chosenCandidate <= 0 || d.chosenCandidate >= d.candidates.size()))
        return false;
    if (d.checked != selected)
    {
        d.checked = selected;
        emit changed();
    }
    return true;
}

bool DriverCatalog::chooseCandidate(int row, int candidateIndex)
{
    if (!canQueue(row))
        return false;
    DriverEntry &d = m_entries[row];
    if (candidateIndex == 0)
        candidateIndex = reinstallCandidateIndex(d);
    if (candidateIndex <= 0 || candidateIndex >= d.candidates.size())
        return false;
    if (m_native && (!d.candidates.at(candidateIndex).valid || !d.candidates.at(candidateIndex).local))
        return false;
    d.chosenCandidate = candidateIndex;
    d.checked = true;
    emit changed();
    return true;
}
void DriverCatalog::selectAll(bool selected)
{
    QVector<int> rows;
    for (int i = 0; i < m_entries.size(); ++i)
        rows.append(i);
    selectRows(rows, selected);
}
int DriverCatalog::selectedDrivers() const
{
    int n = 0;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).selectable && m_entries.at(i).checked)
            ++n;
    return n;
}
int DriverCatalog::problems() const
{
    int n = 0;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).device.hasProblem)
            ++n;
    return n;
}
QStringList DriverCatalog::selectedNames() const
{
    QStringList names;
    for (int i = 0; i < m_entries.size(); ++i)
    {
        const DriverEntry &d = m_entries.at(i);
        if (d.selectable && d.checked && d.chosenCandidate > 0 && d.chosenCandidate < d.candidates.size())
            names.append(
                (isReinstallCandidate(d, d.chosenCandidate)
                     ? UiLanguage::text(QStringLiteral("Reinstall"), QString::fromUtf8("إعادة تثبيت")) +
                           QStringLiteral(" · ")
                 : (d.candidates.at(d.chosenCandidate).statusBits & 0x80u)
                     ? UiLanguage::text(QStringLiteral("Downgrade"), QString::fromUtf8("إصدار أقدم")) +
                           QStringLiteral(" · ")
                     : QString()) +
                d.device.name + QStringLiteral("  ·  ") + d.candidates.at(d.chosenCandidate).version);
    }
    return names;
}

int DriverCatalog::catalogVerifiedCount() const
{
    int n = 0;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).catalogVerified)
            ++n;
    return n;
}

void DriverCatalog::replaceNative(const sdiomodern::Snapshot &snapshot)
{
    m_demo = false;
    m_native = true;
    m_snapshot = snapshot;
    m_entries.clear();
    // Offline driver packs can expose many matches: allocate once instead of
    // repeatedly copying completed entries/candidate vectors during growth.
    m_entries.reserve(static_cast<int>(snapshot.devices.size()));
    for (const sdiomodern::Device &source : snapshot.devices)
    {
        DriverEntry entry;
        entry.candidates.reserve(static_cast<int>(source.candidates.size()) + 1);
        entry.device.name = QString::fromStdWString(source.name);
        entry.device.instanceId = QString::fromStdWString(source.instanceId);
        entry.device.manufacturer = QString::fromStdWString(source.manufacturer);
        entry.device.deviceClass = QString::fromStdWString(source.category);
        entry.device.hardwareId =
            source.hardwareIds.empty() ? QString() : QString::fromStdWString(source.hardwareIds[0]);
        entry.device.driverVersion = QString::fromStdWString(source.installed.version);
        entry.device.problemCode = source.problemCode;
        entry.device.hasProblem = source.problemCode != 0;
        entry.catalogVerified = true;
        entry.engineDeviceStatus = source.matchStatus;
        entry.installedDriverPresent = source.hasInstalledDriver;
        DriverCandidate installed;
        installed.status = QStringLiteral("installed");
        installed.version = QString::fromStdWString(source.installed.version);
        installed.date = QString::fromStdWString(source.installed.date);
        installed.provider = QString::fromStdWString(source.installed.provider);
        installed.inf = QString::fromStdWString(source.installed.inf);
        installed.section = QString::fromStdWString(source.installed.section);
        installed.hardwareId = QString::fromStdWString(source.installed.hardwareId);
        entry.candidates.append(installed);
        for (const sdiomodern::Candidate &candidate : source.candidates)
        {
            DriverCandidate c;
            c.engineToken = candidate.token;
            c.statusBits = candidate.statusBits;
            c.catalogRank = candidate.catalogRank;
            c.minimumBuild = candidate.minimumBuild;
            c.signature = candidate.signature;
            c.rawScore = candidate.rawScore;
            c.originalOrder = candidate.originalOrder;
            c.valid = candidate.valid;
            c.local = candidate.local;
            c.recommended = candidate.recommended;
            for (std::uint64_t word : candidate.filterVisibility)
                c.filterVisibility.append(word);
            c.version = QString::fromStdWString(candidate.version);
            c.date = QString::fromStdWString(candidate.date);
            c.provider = QString::fromStdWString(candidate.provider);
            c.inf = QString::fromStdWString(candidate.inf);
            c.section = QString::fromStdWString(candidate.section);
            c.hardwareId = QString::fromStdWString(candidate.hardwareId);
            c.packageSource = QString::fromStdWString(candidate.packageName);
            c.matchReason = QString::fromStdWString(candidate.reason);
            c.status = c.recommended ? QStringLiteral("recommended") : QStringLiteral("alternative");
            c.matchStars = 0; // Original rank is not a five-star quality/safety score.
            entry.candidates.append(c);
            if (c.valid && c.local && snapshot.capabilities.localInstall)
            {
                entry.selectable = true;
                if (entry.chosenCandidate < 0 || c.recommended)
                    entry.chosenCandidate = entry.candidates.size() - 1;
            }
        }
        for (int i = 1; i < entry.candidates.size(); ++i)
            entry.candidates[i].matchStars = matchPriorityStars(entry, i);
        m_entries.append(entry);
    }
    emit changed();
}
void DriverCatalog::invalidateNative()
{
    if (!m_native)
        return;
    m_snapshot.generation = 0;
    for (int i = 0; i < m_entries.size(); ++i)
    {
        m_entries[i].checked = false;
        m_entries[i].selectable = false;
    }
    emit changed();
}
sdiomodern::InstallPlan DriverCatalog::buildPlan(bool restore, bool confirmed, bool alternatives) const
{
    sdiomodern::InstallPlan plan;
    plan.generation = m_snapshot.generation;
    plan.createRestorePoint = restore;
    plan.confirmed = confirmed;
    plan.allowAlternatives = alternatives;
    if (!m_native)
        return plan;
    for (const DriverEntry &entry : m_entries)
    {
        if (!entry.checked || entry.chosenCandidate <= 0 || entry.chosenCandidate >= entry.candidates.size())
            continue;
        sdiomodern::Selection selected;
        selected.deviceInstanceId = entry.device.instanceId.toStdWString();
        selected.candidateToken = entry.candidates.at(entry.chosenCandidate).engineToken;
        plan.selections.push_back(selected);
    }
    return plan;
}

void DriverCatalog::selectRows(const QVector<int> &rows, bool selected)
{
    bool dirty = false;
    for (int row : rows)
    {
        if (row < 0 || row >= m_entries.size())
            continue;
        DriverEntry &entry = m_entries[row];
        int recommended = -1;
        if (selected && canQueue(row))
        {
            for (int c = 1; c < entry.candidates.size(); ++c)
            {
                const DriverCandidate &candidate = entry.candidates[c];
                if ((candidate.recommended ||
                     (m_demo && candidate.status == QStringLiteral("recommended"))) &&
                    (m_demo ||
                     (candidate.valid && candidate.local && (candidate.statusBits & (0x10 | 0x20)))))
                {
                    recommended = c;
                    break;
                }
            }
        }
        const bool checked = selected && recommended > 0;
        if (checked && entry.chosenCandidate != recommended)
        {
            entry.chosenCandidate = recommended;
            dirty = true;
        }
        if (entry.checked != checked)
        {
            entry.checked = checked;
            dirty = true;
        }
    }
    if (dirty)
        emit changed();
}
