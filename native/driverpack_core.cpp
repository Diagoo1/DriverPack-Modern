#include <QSettings>
// SPDX-License-Identifier: GPL-3.0-or-later
// Independent DriverPack SQLite adapter. No DriverPack executable, JS, ads or software table is used.
#include "origin_api.h"
#include "driverpack_catalog.h"
#include "driverpack_devices.h"
#include "driverpack_archive.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QProcess>
#include <QSet>
#include <QMap>
#include <QUuid>
#include <windows.h>
#include <sddl.h>
#include <setupapi.h>
#include <newdev.h>
#include <SRRestorePtAPI.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace sdiomodern
{
namespace
{
static void fail(const QString &message) { throw std::runtime_error(message.toUtf8().constData()); }
static QString q(const std::wstring &s) { return QString::fromStdWString(s); }
struct Stamp
{
    DWORD volume, high, low, sizeHigh, sizeLow, timeHigh, timeLow;
    bool operator==(const Stamp &s) const
    {
        return volume == s.volume && high == s.high && low == s.low && sizeHigh == s.sizeHigh &&
               sizeLow == s.sizeLow && timeHigh == s.timeHigh && timeLow == s.timeLow;
    }
};
struct ReadLock
{
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit ReadLock(const QString &path)
    {
        value = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ, 0,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
        if (value == INVALID_HANDLE_VALUE)
            fail(QStringLiteral("Cannot lock catalog or archive for read-only use: %1").arg(path));
    }
    ~ReadLock() { CloseHandle(value); }
    Stamp stamp() const
    {
        BY_HANDLE_FILE_INFORMATION i = {};
        if (!GetFileInformationByHandle(value, &i))
            fail(QStringLiteral("Cannot verify source file identity."));
        return Stamp{i.dwVolumeSerialNumber,
                     i.nFileIndexHigh,
                     i.nFileIndexLow,
                     i.nFileSizeHigh,
                     i.nFileSizeLow,
                     i.ftLastWriteTime.dwHighDateTime,
                     i.ftLastWriteTime.dwLowDateTime};
    }
};
static bool admin()
{
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID group = 0;
    BOOL member = FALSE;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0,
                                 0, &group))
    {
        CheckTokenMembership(0, group, &member);
        FreeSid(group);
    }
    return member != FALSE;
}
static void event(const EventSink &sink, Stage stage, const std::wstring &message,
                  Outcome outcome = Outcome::Running, const std::wstring &id = std::wstring(),
                  std::uint64_t done = 0, std::uint64_t total = 0, DWORD error = 0)
{
    if (!sink)
        return;
    Event e;
    e.stage = stage;
    e.message = message;
    e.outcome = outcome;
    e.deviceId = id;
    e.done = done;
    e.total = total;
    e.nativeError = error;
    sink(e);
}
static QJsonObject deviceJson(const Device &d)
{
    QJsonObject row;
    row.insert(QStringLiteral("instanceId"), q(d.instanceId));
    row.insert(QStringLiteral("name"), q(d.name));
    row.insert(QStringLiteral("manufacturer"), q(d.manufacturer));
    row.insert(QStringLiteral("category"), q(d.category));
    row.insert(QStringLiteral("installedVersion"), q(d.installed.version));
    QJsonArray ids;
    for (const auto &id : d.hardwareIds)
        ids.append(q(id));
    row.insert(QStringLiteral("hardwareIds"), ids);
    return row;
}
static bool infMatches(const QString &file, const Candidate &candidate)
{
    if (QFileInfo(file).size() > 16 * 1024 * 1024)
        return false;
    UINT line = 0;
    HINF inf = SetupOpenInfFileW(reinterpret_cast<LPCWSTR>(file.utf16()), 0, INF_STYLE_WIN4, &line);
    if (inf == INVALID_HANDLE_VALUE)
        return false;
    struct Close
    {
        HINF value;
        ~Close() { SetupCloseInfFile(value); }
    } close{inf};
    INFCONTEXT context;
    const QString section = q(candidate.section);
    if (!SetupFindFirstLineW(inf, reinterpret_cast<LPCWSTR>(section.utf16()), 0, &context))
        return false;
    bool matched = false;
    do
    {
        for (DWORD field = 2; field <= SetupGetFieldCount(&context); ++field)
        {
            wchar_t text[4096] = {};
            if (SetupGetStringFieldW(&context, field, text, 4096, 0) &&
                driverpack::normalizedHardwareId(QString::fromWCharArray(text)) ==
                    driverpack::normalizedHardwareId(q(candidate.hardwareId)))
                matched = true;
        }
    } while (SetupFindNextLine(&context, &context));
    if (!matched)
        return false;
    if (!SetupFindFirstLineW(inf, L"Version", L"DriverVer", &context))
        return false;
    wchar_t version[256] = {};
    if (!SetupGetStringFieldW(&context, 2, version, 256, 0))
        return false;
    bool valid = false;
    return driverpack::compareVersions(QString::fromWCharArray(version), q(candidate.version), &valid) == 0 &&
           valid;
}
// A restrictive DACL is applied atomically when this unique directory is created.
struct PrivateWork
{
    QString directory;
    bool keep = false;
    PrivateWork()
    {
        HANDLE token = 0;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            fail(QStringLiteral("Cannot read operation identity."));
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, 0, 0, &size);
        QByteArray data(size, 0);
        if (!GetTokenInformation(token, TokenUser, data.data(), size, &size))
        {
            CloseHandle(token);
            fail(QStringLiteral("Cannot secure operation identity."));
        }
        CloseHandle(token);
        LPWSTR sid = 0;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid, &sid))
            fail(QStringLiteral("Cannot encode operation identity."));
        const QString acl = QStringLiteral("D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;") +
                            QString::fromWCharArray(sid) + QLatin1Char(')');
        LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor = 0;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(reinterpret_cast<LPCWSTR>(acl.utf16()),
                                                                  SDDL_REVISION_1, &descriptor, 0))
            fail(QStringLiteral("Cannot create private operation permissions."));
        SECURITY_ATTRIBUTES attributes = {sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
        directory =
            QDir::temp().filePath(QStringLiteral("DriverPackModern-") + QUuid::createUuid().toString());
        const BOOL made = CreateDirectoryW(reinterpret_cast<LPCWSTR>(directory.utf16()), &attributes);
        LocalFree(descriptor);
        if (!made)
            fail(QStringLiteral("Cannot create private driver workspace."));
    }
    ~PrivateWork()
    {
        if (!keep)
            QDir(directory).removeRecursively();
    }
    QString path() const { return directory; }
};
static DWORD installFile(const QString &file, const QString &hardware, bool &reboot)
{
    const auto target = driverpack::currentTarget();
    if (target.architecture == 64)
    {
        PrivateWork helperWork;
        QFile resource(QStringLiteral(":/driverpack/install64.exe"));
        if (!resource.open(QIODevice::ReadOnly))
            fail(QStringLiteral("The embedded x64 installer is unavailable."));
        const QString helper = QDir(helperWork.path()).filePath(QStringLiteral("install64.exe"));
        const QString resultFile = QDir(helperWork.path()).filePath(QStringLiteral("result.bin"));
        QSaveFile out(helper);
        const QByteArray bytes = resource.readAll();
        if (bytes.size() < 1024 || !out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() ||
            !out.commit())
            fail(QStringLiteral("Cannot prepare the embedded x64 installer."));
        QProcess process;
        process.start(helper, QStringList() << hardware << file << resultFile);
        if (!process.waitForStarted())
            fail(QStringLiteral("Cannot start the x64 installer."));
        if (!process.waitForFinished(-1) || process.exitStatus() != QProcess::NormalExit ||
            process.exitCode() != 0)
            fail(QStringLiteral("The Windows installer helper did not return a verified result."));
        QFile result(resultFile);
        if (!result.open(QIODevice::ReadOnly) || result.size() != 16)
            fail(QStringLiteral("Missing Windows installation result."));
        DWORD fields[4] = {};
        if (result.read(reinterpret_cast<char *>(fields), 16) != 16 || fields[0] != 0x44504D31 ||
            fields[1] > 1 || fields[3] > 1 || (fields[1] && fields[2]) || (!fields[1] && !fields[2]))
            fail(QStringLiteral("Invalid Windows installation result."));
        reboot = fields[3] != 0;
        return fields[1] ? ERROR_SUCCESS : fields[2];
    }
    BOOL restart = FALSE;
    const BOOL ok = UpdateDriverForPlugAndPlayDevicesW(0, reinterpret_cast<LPCWSTR>(hardware.utf16()),
                                                       reinterpret_cast<LPCWSTR>(file.utf16()),
                                                       INSTALLFLAG_FORCE, &restart);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    reboot = restart != FALSE;
    return ok ? ERROR_SUCCESS : (error ? error : ERROR_GEN_FAILURE);
}
} // namespace
struct OriginCore::Impl
{
    Paths paths;
    Snapshot snapshot;
    std::uint64_t generation = 0;
    QMap<quint64, driverpack::Match> metadata;
    QMap<QString, Stamp> stamps;
    QString database;
    Stamp databaseStamp;
};
std::uint32_t initializeRestoreCallbacks() { return 0; }
OriginCore::OriginCore() : m(new Impl) {}
OriginCore::~OriginCore() {}
Capabilities OriginCore::capabilities() const
{
    Capabilities c;
    c.nativeCore = true;
    c.matching = true;
    c.localInstall = true;
    c.restorePoint = true;
    c.snapshots = false;
    return c;
}
Snapshot OriginCore::scan(const Paths &paths, const EventSink &sink)
{
    if (paths.forceReindex)
        throw std::runtime_error(
            "Rebuilding DriverPack SQLite indexes is not supported; obtain an index from its publisher.");
    m->snapshot = Snapshot();
    m->metadata.clear();
    m->stamps.clear();
    m->paths = paths;
    const quint64 generation = ++m->generation;
    event(sink, Stage::Scan, L"Reading present Windows devices without changing drivers.");
    const auto actual = driverpack::currentTarget();
    auto target = actual;
    if (paths.targetVersion)
    {
        target.major = paths.targetVersion / 10;
        target.minor = paths.targetVersion % 10;
    }
    if (paths.targetArchitecture)
        target.architecture = paths.targetArchitecture;
    const QString system =
        QStringLiteral("%1.%2x%3").arg(target.major).arg(target.minor).arg(target.architecture);
    QString database = q(paths.indexes);
    if (QFileInfo(database).isDir())
        database = QDir(database).filePath(QStringLiteral("db.sqlite"));
    m->database = QFileInfo(database).canonicalFilePath();
    ReadLock catalogLock(m->database);
    m->databaseStamp = catalogLock.stamp();
    QElapsedTimer queryHeartbeat; queryHeartbeat.start();
    driverpack::Catalog catalog(m->database, [&]() {
        if (queryHeartbeat.elapsed() >= 250) {
            event(sink, Stage::Match, L"Reading matching driver records from the library.");
            queryHeartbeat.restart();
        }
    });
    Snapshot snapshot;
    snapshot.generation = generation;
    snapshot.capabilities = capabilities();
    snapshot.matchedWindowsVersion = target.major * 10 + target.minor;
    snapshot.matchedArchitecture = target.architecture;
    snapshot.sourceRevision = L"DriverPack SQLite v3 / strict IDs and bounded main-ID fallback / build-rank "
                              L"ordering / date-based state";
    if (paths.targetVersion || paths.targetArchitecture || paths.disableInstall ||
        !paths.snapshotFile.empty())
    {
        snapshot.capabilities.localInstall = false;
        snapshot.capabilities.restorePoint = false;
    }
    if (!paths.snapshotFile.empty())
        fail(QStringLiteral(
            "Saved-machine matching is not implemented for this catalog; use the live computer."));
    snapshot.devices = driverpack::presentDevices();
    const QString root = QFileInfo(q(paths.drivers)).canonicalFilePath();
    if (root.isEmpty())
        fail(QStringLiteral("Choose the DriverPack drivers folder before scanning."));
    QStringList markers;
    markers << QStringLiteral("hooks") << QStringLiteral("dependencies");
    QSettings bios(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS"),
                   QSettings::NativeFormat);
    const QString maker = bios.value(QStringLiteral("SystemManufacturer")).toString().toLower();
    for (const QString &vendor :
         {QStringLiteral("asus"), QStringLiteral("acer"), QStringLiteral("dell"), QStringLiteral("hp"),
          QStringLiteral("lenovo"), QStringLiteral("msi"), QStringLiteral("samsung"),
          QStringLiteral("toshiba"), QStringLiteral("fujitsu")})
        if (maker.contains(vendor) ||
            (vendor == QStringLiteral("hp") && maker.contains(QStringLiteral("hewlett"))))
            markers << vendor;
    SYSTEM_POWER_STATUS power = {};
    if (GetSystemPowerStatus(&power) && power.BatteryFlag != 255)
        markers << (power.BatteryFlag == 128 ? QStringLiteral("desktop") : QStringLiteral("notebook"));
    quint64 token = 0;
    quint64 metadataBytes = 0;
    for (size_t index = 0; index < snapshot.devices.size(); ++index)
    {
        Device &device = snapshot.devices[index];
        QSet<QString> lookedUp, seen;
        std::vector<std::wstring> ids = device.hardwareIds;
        int order = 0;
        bool recommended = false;
        struct Found
        {
            driverpack::Match row;
            std::wstring hardware;
        };
        QVector<Found> found;
        for (const auto &id : ids)
        {
            const QString hardware = driverpack::normalizedHardwareId(q(id));
            if (lookedUp.contains(hardware))
                continue;
            lookedUp.insert(hardware);
            const auto rows = catalog.find(hardware, system,
                                           (target.major == 10 && target.build >= 14310) ? target.build : 0,
                                           256, false, true);
            for (const auto &row : rows)
            {
                if (!driverpack::platformApplicable(row, markers))
                    continue;
                if (seen.contains(row.key))
                    continue;
                seen.insert(row.key);
                if (found.size() >= 4096)
                    throw std::runtime_error("Too many catalog alternatives for one device.");
                found.append(Found{row, id});
            }
        }
        if (found.isEmpty() && !ids.empty())
        {
            const auto rows = catalog.find(q(ids.front()), system,
                                           (target.major == 10 && target.build >= 14310) ? target.build : 0,
                                           256, true, true);
            for (const auto &row : rows)
                if (driverpack::platformApplicable(row, markers))
                    found.append(Found{row, ids.front()});
        }
        std::sort(found.begin(), found.end(),
                  [](const Found &a, const Found &b) { return driverpack::higherPriority(a.row, b.row); });
        QSet<QString> originalGroups;
        for (const auto &match : found)
        {
            if (device.candidates.size() >= 256)
                break;
            const auto &row = match.row;
            if (originalGroups.contains(row.groupKey))
                continue;
            originalGroups.insert(row.groupKey);
            // Keep genuine input/touchpad matches available for explicit repair
            // or downgrade. Automatic selection is restricted by driver state.
            const auto &id = match.hardware;
            Candidate candidate;
            candidate.token = (generation << 32) | (++token);
            candidate.version = row.version.toStdWString();
            candidate.date = row.date.toStdWString();
            candidate.provider = row.manufacturer.toStdWString();
            candidate.hardwareId = id;
            candidate.section = row.section.toStdWString();
            candidate.packageName = (row.pack.endsWith(QStringLiteral(".7z"), Qt::CaseInsensitive)
                                         ? row.pack
                                         : row.pack + QStringLiteral(".7z"))
                                        .toStdWString();
            candidate.inf = (row.directory + QLatin1Char('/') + row.inf).toStdWString();
            candidate.originalOrder = ++order;
            candidate.catalogRank = row.rank;
            candidate.minimumBuild = row.build;
            candidate.signature = row.signature;
            candidate.rawScore = static_cast<quint32>(
                qBound(0.0, std::isfinite(row.rank) ? row.rank * 100 : 0.0, 4294967295.0));
            candidate.filterVisibility.clear();
            candidate.packagePath = QDir(root).filePath(q(candidate.packageName)).toStdWString();
            candidate.local = QFileInfo(q(candidate.packagePath)).isFile();
            bool candidateVersion = false;
            driverpack::compareVersions(row.version, row.version, &candidateVersion);
            bool versions = true;
            driverpack::compareVersions(row.version, q(device.installed.version), &versions);
            candidate.statusBits =
                driverpack::driverState(device.hasInstalledDriver, q(device.installed.version),
                                        q(device.installed.date), row.version, row.date);
            candidate.valid = candidateVersion && driverpack::safeRelative(q(candidate.packageName), true) &&
                              safeRelativeInfPath(candidate.inf) && !driverpack::hasInstallerRules(row) &&
                              !row.fallback && (candidate.statusBits & 0x8) == 0 && row.signature > 0 &&
                              std::isfinite(row.rank) && row.rank > 0 &&
                              (!device.hasInstalledDriver || versions);
            QString reason;
            if (row.fallback)
                reason = QStringLiteral("Main-ID fallback match: a more specific INF applicability check is "
                                        "required before installation.");
            else if (!std::isfinite(row.rank) || row.rank <= 0)
                reason = QStringLiteral(
                    "The original catalog does not recommend this match. Shown for comparison only.");
            else if (driverpack::hasInstallerRules(row))
                reason = QStringLiteral("Dependencies or installation hooks require additional support; not "
                                        "eligible for installation.");
            else if (!candidate.valid)
                reason =
                    QStringLiteral("Catalog metadata or signature applicability could not be validated.");
            if (candidate.local)
            {
                const QString canonical = QFileInfo(q(candidate.packagePath)).canonicalFilePath();
                if (!canonical.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive))
                {
                    candidate.valid = false;
                    reason = QStringLiteral("Archive is outside the selected DriverPack library.");
                }
                else
                {
                    candidate.packagePath = canonical.toStdWString();
                    if (!m->stamps.contains(canonical))
                    {
                        ReadLock file(canonical);
                        m->stamps.insert(canonical, file.stamp());
                    }
                }
            }
            if (!candidate.valid)
                candidate.statusBits |= 0x8;
            candidate.recommended = !recommended && candidate.valid;
            if (candidate.recommended)
                recommended = true;
            candidate.reason = reason.toStdWString();
            metadataBytes +=
                2ull * (row.key.size() + row.hardwareId.size() + row.manufacturer.size() + row.name.size() +
                        row.version.size() + row.date.size() + row.pack.size() + row.directory.size() +
                        row.inf.size() + row.section.size() + row.allow.size() + row.disallow.size() +
                        row.hooks.size() + row.dependencies.size() + row.type.size());
            if (token > 65536 || metadataBytes > 64ull * 1024 * 1024)
                throw std::runtime_error("Catalog results exceed the bounded metadata budget.");
            m->metadata.insert(candidate.token, row);
            device.candidates.push_back(candidate);
        }
        device.matchStatus = !device.hasInstalledDriver ? 0x100 : 0;
        if (index % 16 == 0)
            event(sink, Stage::Match, L"Matching hardware IDs against the DriverPack SQLite catalog.",
                  Outcome::Running, device.instanceId, index, snapshot.devices.size());
    }
    m->snapshot = snapshot;
    event(sink, Stage::Finished,
          L"DriverPack catalog scan completed; no driver or bundled program was executed.", Outcome::Success,
          std::wstring(), snapshot.devices.size(), snapshot.devices.size());
    return snapshot;
}
void OriginCore::setOperationOptions(const Paths &paths) { m->paths = paths; }
void OriginCore::createRestorePoint(const std::wstring &description)
{
    if (!admin())
        throw std::runtime_error("Administrator rights are required to create a restore point.");
    typedef BOOL(WINAPI * Restore)(PRESTOREPOINTINFOW, PSTATEMGRSTATUS);
    HMODULE module = LoadLibraryW(L"srclient.dll");
    if (!module)
        throw std::runtime_error("System Restore is unavailable.");
    struct Free
    {
        HMODULE value;
        ~Free() { FreeLibrary(value); }
    } free{module};
    Restore restore = reinterpret_cast<Restore>(GetProcAddress(module, "SRSetRestorePointW"));
    if (!restore)
        throw std::runtime_error("System Restore API is unavailable.");
    RESTOREPOINTINFOW request = {};
    STATEMGRSTATUS status = {};
    request.dwEventType = BEGIN_SYSTEM_CHANGE;
    request.dwRestorePtType = DEVICE_DRIVER_INSTALL;
    const QString text = description.empty()
                             ? QStringLiteral("DriverPack Modern - before selected driver changes")
                             : q(description);
    text.left(MAX_DESC_W - 1).toWCharArray(request.szDescription);
    if (!restore(&request, &status) || status.nStatus != ERROR_SUCCESS)
    {
        SetLastError(status.nStatus ? status.nStatus : GetLastError());
        throw std::runtime_error(
            "Windows could not create the required restore point. No driver was installed.");
    }
    request.dwEventType = END_SYSTEM_CHANGE;
    request.llSequenceNumber = status.llSequenceNumber;
    if (!restore(&request, &status) || status.nStatus != ERROR_SUCCESS)
        throw std::runtime_error("Windows could not finalize the restore point. No driver was installed.");
}
static const Candidate *candidateFor(const Snapshot &snapshot, const Selection &selection)
{
    for (const auto &device : snapshot.devices)
        if (canonicalId(device.instanceId) == canonicalId(selection.deviceInstanceId))
            for (const auto &candidate : device.candidates)
                if (candidate.token == selection.candidateToken)
                    return &candidate;
    return 0;
}
std::wstring OriginCore::extract(const InstallPlan &plan, const std::wstring &folder, const EventSink &sink,
                                 const std::atomic<bool> &stop)
{
    if (plan.generation != m->snapshot.generation || plan.selections.empty())
        throw std::runtime_error("The extraction selection is stale or empty.");
    ReadLock catalog(m->database);
    if (!(catalog.stamp() == m->databaseStamp))
        throw std::runtime_error("The DriverPack index changed; rescan before extraction.");
    const QString output =
        QDir(q(folder)).filePath(QStringLiteral("DriverPack-export-") + QUuid::createUuid().toString());
    if (!QDir().mkpath(output))
        throw std::runtime_error("Cannot create export folder.");
    size_t count = 0;
    for (const auto &selection : plan.selections)
    {
        const Candidate *candidate = candidateFor(m->snapshot, selection);
        if (!candidate || !candidate->valid || !candidate->local || !m->metadata.contains(candidate->token))
            throw std::runtime_error("Selected driver extraction is unavailable.");
        ReadLock archive(q(candidate->packagePath));
        if (!(archive.stamp() == m->stamps.value(q(candidate->packagePath))))
            throw std::runtime_error("The selected archive changed; rescan.");
        const auto row = m->metadata.value(candidate->token);
        const QString target = QDir(output).filePath(QString::number(++count));
        if (!QDir().mkpath(target))
            throw std::runtime_error("Cannot create private extraction output.");
        event(sink, Stage::Extract, L"Extracting the selected driver folder; no installer is running.",
              Outcome::Running, selection.deviceInstanceId, count - 1, plan.selections.size());
        driverpack::extractDirectory(q(candidate->packagePath), row.directory, target, stop);
        const QString inf = QDir(target).filePath(row.inf);
        if (!infMatches(inf, *candidate))
            throw std::runtime_error("Extracted INF does not match catalog version, section or hardware ID.");
        event(sink, Stage::Extract, L"Driver files extracted and INF metadata verified.", Outcome::Success,
              selection.deviceInstanceId, count, plan.selections.size());
    }
    return output.toStdWString();
}
InstallReport OriginCore::install(const InstallPlan &plan, const EventSink &sink,
                                  const std::atomic<bool> &stop)
{
    InstallReport report;
    if (m->paths.targetVersion || m->paths.targetArchitecture || m->paths.disableInstall)
        throw std::runtime_error("Installation is blocked in a compatibility preview.");
    if (!admin())
        throw std::runtime_error("Restart DriverPack Modern as Administrator, rescan and review the plan.");
    const auto selections = validatePlan(m->snapshot, plan);
    ReadLock database(m->database);
    if (!(database.stamp() == m->databaseStamp))
        throw std::runtime_error("The catalog changed after the scan; review a new scan.");
    const auto present = driverpack::presentDevices();
    for (const auto &selected : selections)
    {
        bool found = false;
        for (const auto &now : present)
            if (canonicalId(now.instanceId) == canonicalId(selected.device->instanceId))
            {
                found = true;
                if (now.installed.version != selected.device->installed.version)
                    throw std::runtime_error(
                        "Installed driver changed after scanning; rescan and review again.");
            }
        if (!found)
            throw std::runtime_error("A selected device is no longer present.");
    }
    Snapshot current = m->snapshot;
    current.devices = present;
    for (auto &device : current.devices)
        for (const auto &old : m->snapshot.devices)
            if (canonicalId(device.instanceId) == canonicalId(old.instanceId))
                device.candidates = old.candidates;
    validatePlan(current, plan);
    if (plan.createRestorePoint)
    {
        event(sink, Stage::RestorePoint, L"Creating the required Windows restore point.");
        try
        {
            createRestorePoint();
            report.restoreCreated = true;
        }
        catch (const std::exception &e)
        {
            report.restoreFailed = true;
            report.restoreError = GetLastError();
            report.error = QString::fromUtf8(e.what()).toStdWString();
            return report;
        }
    }
    size_t completed = 0;
    for (const auto &selection : selections)
    {
        Result result;
        result.deviceId = selection.device->instanceId;
        result.name = selection.device->name;
        if (stop.load())
        {
            result.outcome = Outcome::Cancelled;
            result.message = L"Not started: stop was requested.";
            report.cancelled = true;
            report.items.push_back(result);
            continue;
        }
        const Candidate &candidate = *selection.candidate;
        event(sink, Stage::Install, L"Preparing and verifying the selected DriverPack driver.",
              Outcome::Running, result.deviceId, completed, selections.size());
        try
        {
            ReadLock archive(q(candidate.packagePath));
            if (!(archive.stamp() == m->stamps.value(q(candidate.packagePath))))
                throw std::runtime_error("The driver archive changed after scanning.");
            PrivateWork work;
            const auto metadata = m->metadata.value(candidate.token);
            driverpack::extractDirectory(q(candidate.packagePath), metadata.directory, work.path(), stop);
            const QString inf = QDir(work.path()).filePath(metadata.inf);
            if (!infMatches(inf, candidate))
                throw std::runtime_error("Extracted INF does not match the selected catalog driver.");
            if (stop.load())
            {
                result.outcome = Outcome::Cancelled;
                report.cancelled = true;
                result.message = L"Stopped before invoking Windows installation.";
            }
            else
            {
                const DWORD error = installFile(inf, q(candidate.hardwareId), result.rebootRequired);
                result.nativeError = error;
                result.outcome = error == ERROR_SUCCESS ? Outcome::Success : Outcome::Failure;
                result.message = error == ERROR_SUCCESS ? L"Windows confirmed successful driver installation."
                                                        : L"Windows rejected driver installation.";
                report.rebootRequired = report.rebootRequired || result.rebootRequired;
            }
            if (m->paths.keepTempFiles)
                work.keep = true;
        }
        catch (const std::exception &e)
        {
            result.outcome = Outcome::Failure;
            result.message = QString::fromUtf8(e.what()).toStdWString();
        }
        report.items.push_back(result);
        ++completed;
        event(sink, Stage::Install, result.message, result.outcome, result.deviceId, completed,
              selections.size(), result.nativeError);
    }
    m->snapshot.generation = ++m->generation;
    m->snapshot.capabilities.localInstall = false;
    event(sink, Stage::Finished, L"DriverPack operation ended. Rescan the actual Windows state.",
          Outcome::Success, std::wstring(), completed, selections.size());
    return report;
}
void OriginCore::saveSnapshot(const std::wstring &file)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("DriverPackModern-device-report"));
    root.insert(QStringLiteral("generation"), static_cast<double>(m->snapshot.generation));
    QJsonArray devices;
    for (const auto &device : m->snapshot.devices)
        devices.append(deviceJson(device));
    root.insert(QStringLiteral("devices"), devices);
    QSaveFile out(q(file));
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(root).toJson()) < 0 || !out.commit())
        throw std::runtime_error("Cannot save the local device report.");
}
void OriginCore::writeDeviceList(const std::wstring &file) { saveSnapshot(file); }
void OriginCore::exportIndexText(const std::wstring &folder)
{
    if (!QDir().mkpath(q(folder)))
        throw std::runtime_error("Cannot create report folder.");
    saveSnapshot(QDir(q(folder)).filePath(QStringLiteral("DriverPack-devices.json")).toStdWString());
}
InstallReport OriginCore::installInf(const std::wstring &, const std::wstring &, const EventSink &)
{
    throw std::runtime_error(
        "Direct INF installation is disabled; choose a validated DriverPack catalog entry.");
}
int OriginCore::archiveCommand(const std::wstring &)
{
    throw std::runtime_error("Arbitrary archive commands are disabled; use selected-driver extraction.");
}
void OriginCore::setInstalledIdLog(const std::wstring &)
{
    throw std::runtime_error("Legacy installed-ID logging is not a DriverPack operation.");
}
} // namespace sdiomodern
