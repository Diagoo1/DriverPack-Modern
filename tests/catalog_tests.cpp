#include "../native/driverpack_catalog.h"
#include "../native/origin_api.h"
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include <QSet>
#include <QThread>
#include <QCryptographicHash>
#include "../native/sqlite/sqlite3.h"
#include <cstdio>
#include <stdexcept>
static int checks = 0;
static void check(bool value, const char *name)
{
    if (!value)
        throw std::runtime_error(name);
    ++checks;
}
template <class F> static bool rejected(F action)
{
    try
    {
        action();
        return false;
    }
    catch (const std::exception &)
    {
        return true;
    }
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        const QString directory = app.arguments().value(1);
        driverpack::Catalog catalog(QDir(directory).filePath(QStringLiteral("catalog.sqlite")));
        const QString id = QStringLiteral("PCI\\VEN_FAKE&DEV_0001");
        auto rows = catalog.find(id, QStringLiteral("10.0x64"), 200);
        check(rows.size() == 2, "build, architecture and positive-rank filtering");
        auto allRanks = catalog.find(id, QStringLiteral("10.0x64"), 200, 256, false, true);
        check(allRanks.size() == 3 && allRanks.last().rank < 0,
              "comparison lookup retains nonrecommended matches without promoting their rank");
        check(catalog.find(id, QStringLiteral("6.1x64"), 200, 256, false, true).isEmpty(),
              "comparison mode preserves target Windows filtering");

        check(rows[0].version == QStringLiteral("1.0.0.0") && rows[1].version == QStringLiteral("2.0.0.0"),
              "section build sorts before catalog rank");
        check(catalog.find(id.toLower(), QStringLiteral("10.0x64"), 200).size() == 2,
              "case-normalized exact hardware IDs");
        check(catalog.find(QStringLiteral("PCI\\VEN_FAKE"), QStringLiteral("10.0x64"), 200).isEmpty(),
              "no substring hardware matches");
        check(catalog.find(QStringLiteral("' OR 1=1 --"), QStringLiteral("10.0x64"), 200).isEmpty(),
              "hardware ID SQL parameter is not executable");
        check(catalog.find(id, QStringLiteral("10.0x86"), 200).size() == 1,
              "x86 and x64 matches remain separate");
        check(catalog.find(id, QStringLiteral("6.1x64"), 200).isEmpty(),
              "different Windows targets are not silently mixed");
        check(rejected([&] { catalog.find(id, QStringLiteral("10.0x64"), 200, 513); }),
              "bounded query limit");
        check(rejected(
                  [&]
                  {
                      driverpack::Catalog broken(
                          QDir(directory).filePath(QStringLiteral("malformed.sqlite")));
                  }),
              "malformed SQLite is rejected without execution");
        check(rejected(
                  [&]
                  {
                      driverpack::Catalog wrong(
                          QDir(directory).filePath(QStringLiteral("unsupported.sqlite")));
                  }),
              "unsupported schemas are rejected");
        QTemporaryDir slowFixture;
        const QString moved = QDir(slowFixture.path()).filePath(QString::fromUtf8("مكتبة منقولة index.sqlite"));
        check(QFile::copy(QDir(directory).filePath(QStringLiteral("catalog.sqlite")),moved), "relocated unicode catalog copied");
        sqlite3 *slowDb = 0;
        check(sqlite3_open(moved.toUtf8().constData(), &slowDb)==SQLITE_OK, "open isolated slow-media fixture");
        for(int i=0;i<12;++i) check(sqlite3_exec(slowDb,"INSERT INTO Usable SELECT * FROM Usable",0,0,0)==SQLITE_OK,"expand isolated query work");
        sqlite3_close(slowDb);
        QFile baseline(moved);baseline.open(QIODevice::ReadOnly);const QByteArray before = QCryptographicHash::hash(baseline.readAll(),QCryptographicHash::Sha256);baseline.close();
        check(QFile::setPermissions(moved,QFileDevice::ReadOwner|QFileDevice::ReadUser|QFileDevice::ReadGroup|QFileDevice::ReadOther),"read-only fixture permissions");
        int checkpoints=0;
        driverpack::Catalog slow(moved,[&](){if(++checkpoints==1)QThread::msleep(5100);});
        check(slow.find(id,QStringLiteral("10.0x64"),200).size()==256 && checkpoints>0,"slow read above old five-second wall limit succeeds");
        driverpack::Catalog cancelled(moved,[](){throw std::runtime_error("cancelled by test");});
        bool cancelledExactly=false;
        try { cancelled.find(id,QStringLiteral("10.0x64"),200); }
        catch(const std::exception &e) { cancelledExactly=QString::fromUtf8(e.what())==QStringLiteral("cancelled by test"); }
        check(cancelledExactly,"cancellation propagates safely outside the SQLite C callback");
        driverpack::Catalog bounded(moved,std::function<void()>(),2000);
        check(rejected([&](){bounded.find(id,QStringLiteral("10.0x64"),200);}),"instruction budget still stops expensive queries");
        baseline.open(QIODevice::ReadOnly);
        check(QCryptographicHash::hash(baseline.readAll(),QCryptographicHash::Sha256)==before,"read-only relocated catalog remains unchanged");
        baseline.close();
        driverpack::Match row = rows[0];
        const QByteArray deviceKey = QByteArray::fromHex("32d59b");
        check(driverpack::originalGroupKey(deviceKey, QByteArray::fromHex("c9fbd1")) ==
                  driverpack::originalGroupKey(deviceKey, QByteArray::fromHex("efbae2")),
              "original legacy Node grouping coalesces invalid binary-key byte sequences");
        check(driverpack::originalGroupKey(deviceKey, QByteArray::fromHex("806a33")) !=
                  driverpack::originalGroupKey(deviceKey, QByteArray::fromHex("c9fbd1")),
              "original grouping retains distinct decoded section keys");
        check(driverpack::originalGroupKey(QByteArray("A"), QByteArray::fromHex("c2a3")) ==
                  QString::fromUtf8("A,\xc2\xa3"),
              "legacy grouping preserves valid multibyte UTF-8");
        row.deviceClass = QStringLiteral("inputdev");
        check(!driverpack::originalDriverClassVisible(row), "original scanner excludes catalog inputdev");
        row.deviceClass = QStringLiteral("TouchPad");
        check(!driverpack::originalDriverClassVisible(row), "original scanner excludes catalog touchpad");
        row.deviceClass = QStringLiteral("chipset");
        check(driverpack::originalDriverClassVisible(row), "real chipset results stay visible");
        row.deviceClass.clear();
        check(driverpack::originalDriverClassVisible(row), "legacy class-free fixtures stay readable");
        QTemporaryDir classFixture;
        const QString classifiedFile = QDir(classFixture.path()).filePath(QStringLiteral("classified.sqlite"));
        check(QFile::copy(QDir(directory).filePath(QStringLiteral("catalog.sqlite")), classifiedFile),
              "isolated original-class catalog fixture copied");
        sqlite3 *classifiedDb = 0;
        check(sqlite3_open(classifiedFile.toUtf8().constData(), &classifiedDb) == SQLITE_OK,
              "isolated class fixture opens");
        const int classResult = sqlite3_exec(
            classifiedDb, "ALTER TABLE Devices ADD COLUMN class TEXT; UPDATE Devices SET class='inputdev';",
            0, 0, 0);
        sqlite3_close(classifiedDb);
        check(classResult == SQLITE_OK, "fixture carries original catalog device classes");
        driverpack::Catalog classified(classifiedFile);
        const auto classifiedRows = classified.find(id, QStringLiteral("10.0x64"), 200);
        check(classifiedRows.size() == 2 && classifiedRows[0].deviceClass == QStringLiteral("inputdev") &&
                  !driverpack::originalDriverClassVisible(classifiedRows[0]),
              "class exclusion uses actual SQLite metadata, not a device-name heuristic");
        check(!driverpack::hasPlatformRestrictions(row), "plain driver metadata has no script dependency");
        row.hooks = QStringLiteral("{\"exec\":\"never-run.exe\"}");
        check(driverpack::hasPlatformRestrictions(row),
              "installation hooks are ineligible data, not executable code");
        row.hooks = QStringLiteral("invalid-json");
        check(driverpack::hasPlatformRestrictions(row), "malformed hook metadata fails closed");
        bool valid = false;
        check(driverpack::compareVersions(QStringLiteral("001.2"), QStringLiteral("1.2.0.0"), &valid) == 0 &&
                  valid,
              "normalized numeric version comparison");
        driverpack::compareVersions(QStringLiteral("1.x"), QStringLiteral("1.0"), &valid);
        check(!valid, "malformed versions do not become upgrades");
        const QStringList bad = QStringList()
                                << QStringLiteral("../escape.inf") << QStringLiteral("C:/escape.inf")
                                << QStringLiteral("/escape.inf") << QStringLiteral("folder/CON.txt")
                                << QStringLiteral("folder/../file.inf")
                                << QStringLiteral("folder/name:stream") << QStringLiteral("folder/name. ");
        for (const QString &path : bad)
            check(!driverpack::safeRelative(path), "unsafe catalog path rejected");
        check(driverpack::safeRelative(QStringLiteral("vendor/version/driver.inf")),
              "normal nested driver paths remain supported");

        check(driverpack::normalizedDriverDate(QStringLiteral("6-25-2024")) == QStringLiteral("2024-06-25"),
              "registry date normalized");
        check(driverpack::driverState(true, QStringLiteral("16.0"), QStringLiteral("2025-01-01"),
                                      QStringLiteral("20.0"), QStringLiteral("2024-01-01")) == 0x80,
              "larger version with older date is rollback");
        check(driverpack::driverState(true, QStringLiteral("20.0"), QStringLiteral("2024-01-01"),
                                      QStringLiteral("16.0"), QStringLiteral("2025-01-01")) == 0x20,
              "date governs update state");
        check(driverpack::driverState(true, QStringLiteral("1.0"), QStringLiteral("2024-01-01"),
                                      QStringLiteral("2.0"), QStringLiteral("2024-01-01")) == 0x40,
              "same date is installed per original");
        check(driverpack::driverState(true, QStringLiteral("1.0"), QStringLiteral("2024-01-01"),
                                      QStringLiteral("1.0"), QStringLiteral("2025-01-01")) == 0x40,
              "same version is installed per original");
        check(driverpack::driverState(false, QString(), QString(), QStringLiteral("1.0"),
                                      QStringLiteral("2024-01-01")) == 0x10,
              "missing driver state");
        check(driverpack::driverState(true, QStringLiteral("1"), QStringLiteral("invalid"),
                                      QStringLiteral("2"), QStringLiteral("2024-01-01")) == 0x8,
              "invalid dates do not invent updates");
        check(driverpack::mainHardwareId(QStringLiteral("PCI\\VEN_8086&DEV_1234&SUBSYS_5678")) ==
                  QStringLiteral("PCI\\VEN_8086&DEV_1234"),
              "main ID strips subsystem");
        check(catalog.find(id, QStringLiteral("10.0x64"), 200, 256, true).isEmpty(),
              "legacy schema without failover remains supported");
        driverpack::Match platform;
        platform.allow = QStringLiteral("[\"asus\",\"notebook\"]");
        check(driverpack::platformApplicable(platform, QStringList() << QStringLiteral("ASUS")
                                                                     << QStringLiteral("notebook")),
              "all required platform markers match case insensitive");
        check(!driverpack::platformApplicable(platform, QStringList() << QStringLiteral("asus")),
              "missing required marker rejected");
        platform.disallow = QStringLiteral("[\"notebook\"]");
        check(!driverpack::platformApplicable(platform, QStringList() << QStringLiteral("asus")
                                                                      << QStringLiteral("notebook")),
              "denied marker takes precedence");
        platform.allow = QStringLiteral("invalid");
        check(!driverpack::platformApplicable(platform, QStringList()),
              "malformed platform markers fail closed");

        QTemporaryDir fallbackFolder;
        const QString fallbackPath = QDir(fallbackFolder.path()).filePath(QStringLiteral("fallback.sqlite"));
        check(QFile::copy(QDir(directory).filePath(QStringLiteral("catalog.sqlite")), fallbackPath),
              "failover fixture copy remains separate from source database");
        sqlite3 *fixture = 0;
        check(sqlite3_open(fallbackPath.toUtf8().constData(), &fixture) == SQLITE_OK,
              "open private failover fixture");
        const int altered = sqlite3_exec(
            fixture,
            "ALTER TABLE Devices ADD COLUMN mainId TEXT;ALTER TABLE Usable ADD COLUMN failoverRank "
            "REAL;UPDATE Devices SET mainId=deviceId;UPDATE Usable SET failoverRank=rank;",
            0, 0, 0);
        sqlite3_close(fixture);
        check(altered == SQLITE_OK, "prepare original failover schema");
        driverpack::Catalog failover(fallbackPath);
        const QString extended = id + QStringLiteral("&SUBSYS_ABCD");
        check(failover.find(extended, QStringLiteral("10.0x64"), 200).isEmpty(),
              "strict query does not match a different subsystem");
        const auto fallbackRows = failover.find(extended, QStringLiteral("10.0x64"), 200, 256, true);
        check(fallbackRows.size() == 2 && fallbackRows[0].fallback &&
                  fallbackRows[0].version == QStringLiteral("1.0.0.0"),
              "main-ID failover returns marked rows in original build/rank order");
        QTemporaryDir distinctFolder;
        const QString distinctPath = QDir(distinctFolder.path()).filePath(QStringLiteral("distinct.sqlite"));
        check(QFile::copy(QDir(directory).filePath(QStringLiteral("catalog.sqlite")), distinctPath),
              "duplicate hardware fixture is isolated");
        sqlite3 *distinctDb = 0;
        sqlite3_open(distinctPath.toUtf8().constData(), &distinctDb);
        const int duplicated = sqlite3_exec(
            distinctDb,
            "INSERT INTO Devices SELECT x'6666',deviceId,manufacturer,name FROM Devices LIMIT 1;"
            "INSERT INTO Usable SELECT x'6666',sectionId,system,rank FROM Usable WHERE deviceId!=(x'6666') "
            "AND system='10.0x64' AND rank>0 AND sectionId IN(SELECT id FROM Sections WHERE build<=200);",
            0, 0, 0);
        sqlite3_close(distinctDb);
        check(duplicated == SQLITE_OK, "duplicate hardware rows prepared");
        driverpack::Catalog distinct(distinctPath);
        auto distinctRows = distinct.find(id, QStringLiteral("10.0x64"), 200);
        QSet<QString> identity;
        for (const auto &match : distinctRows)
            identity.insert(match.key);
        check(distinctRows.size() == 4 && identity.size() == 4,
              "different hardware records sharing an INF section retain distinct identities");

        sdiomodern::Snapshot snapshot;
        snapshot.generation = 9;
        snapshot.capabilities.nativeCore = true;
        snapshot.capabilities.matching = true;
        snapshot.capabilities.localInstall = true;
        snapshot.capabilities.restorePoint = true;
        sdiomodern::Device device;
        device.instanceId = L"TEST-INSTANCE";
        device.hardwareIds.push_back(L"PCI\\FAKE");
        sdiomodern::Candidate candidate;
        candidate.token = 7;
        candidate.valid = true;
        candidate.local = true;
        candidate.recommended = true;
        candidate.inf = L"vendor/driver.inf";
        candidate.hardwareId = L"PCI\\FAKE";
        device.candidates.push_back(candidate);
        snapshot.devices.push_back(device);
        sdiomodern::InstallPlan plan;
        plan.generation = 9;
        plan.confirmed = true;
        plan.createRestorePoint = true;
        plan.selections.push_back(sdiomodern::Selection{L"TEST-INSTANCE", 7});
        check(sdiomodern::validatePlan(snapshot, plan).size() == 1,
              "an explicit unambiguous local plan validates without installing");
        plan.confirmed = false;
        check(rejected([&] { sdiomodern::validatePlan(snapshot, plan); }), "confirmation required");
        plan.confirmed = true;
        plan.generation = 8;
        check(rejected([&] { sdiomodern::validatePlan(snapshot, plan); }), "stale plans rejected");
        plan.generation = 9;
        plan.selections.push_back(plan.selections[0]);
        check(rejected([&] { sdiomodern::validatePlan(snapshot, plan); }),
              "duplicate device selections rejected");
        plan.selections.resize(1);
        auto other = device;
        other.instanceId = L"SECOND";
        snapshot.devices.push_back(other);
        check(rejected([&] { sdiomodern::validatePlan(snapshot, plan); }),
              "broad hardware-ID updates are blocked");
        snapshot.devices.resize(1);
        snapshot.devices[0].candidates[0].local = false;
        check(rejected([&] { sdiomodern::validatePlan(snapshot, plan); }),
              "missing driver archives rejected");
        snapshot.devices[0].candidates[0].local = true;
        snapshot.devices[0].candidates[0].valid = false;
        check(rejected([&] { sdiomodern::validatePlan(snapshot, plan); }),
              "ineligible catalog entries rejected");
        printf("%d DriverPack catalog and selection checks passed; no installation executed.\n", checks);
        return 0;
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "DriverPack test failed: %s\n", e.what());
        return 1;
    }
}
