#include "../native/driverpack_catalog.h"
#include "../src/driverpack_view.h"
#include "../src/offline_paths.h"
#include "../src/drop_target.h"
#include "../src/portable_library.h"
#include "../src/workflow_queue.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <cstdio>
#include <stdexcept>
static int count = 0;
static void check(bool value, const char *name)
{
    if (!value)
        throw std::runtime_error(name);
    ++count;
}
static QByteArray read(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error("read fixture");
    return f.readAll();
}
static void write(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
        throw std::runtime_error("write fixture");
}
template <class F> static bool rejected(F f)
{
    try
    {
        f();
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
        check(driverpack::normalizedHardwareId(QStringLiteral("* pnp0c14")) ==
                  QStringLiteral("ACPI\\PNP0C14"),
              "legacy ACPI alias normalization");
        check(driverpack::normalizedHardwareId(QStringLiteral("ACPI\\VEN_INT&DEV_33A0")) ==
                  QStringLiteral("ACPI\\INT33A0"),
              "ACPI vendor-device normalization");
        check(driverpack::normalizedHardwareId(QStringLiteral("pci\\ven_1234&dev_5678&subsys_0010")) ==
                  QStringLiteral("PCI\\VEN_1234&DEV_5678&SUBSYS_0010"),
              "PCI subsystem specificity retained");
        driverpack::Match a, b;
        a.build = 100;
        b.build = 99;
        a.rank = 1;
        b.rank = 9;
        check(driverpack::higherPriority(a, b), "global section build before rank");
        a.build = b.build;
        check(driverpack::higherPriority(b, a), "global catalog rank ordering");
        a.rank = b.rank;
        a.hardwareId = QStringLiteral("PCI\\VEN_1234&DEV_5678&SUBSYS_0010");
        b.hardwareId = QStringLiteral("PCI\\VEN_1234&DEV_5678");
        check(driverpack::higherPriority(a, b), "strict hardware ID ordering across lookup batches");
        QVector<bool> filters(7, false);
        for (int i = 0; i < 4; ++i)
            filters[i] = true;
        check(DriverPackView::accept(0x20, true, true, filters), "eligible local upgrade visible");
        check(DriverPackView::accept(0x40, true, true, filters), "reinstall version visible");
        check(!DriverPackView::accept(0x20, false, true, filters), "unsupported rules hidden by default");
        check(!DriverPackView::accept(0x20, true, false, filters), "missing package hidden by default");
        filters[4] = filters[5] = true;
        check(DriverPackView::accept(0x20, false, false, filters),
              "explicit unsupported and missing-package filters work");
        filters[1] = false;
        check(!DriverPackView::accept(0x20, true, true, filters), "upgrade filter changes results");
        QTemporaryDir work;
        check(work.isValid(), "isolated library workspace");
        const QString root = work.path() + QStringLiteral("/source"),
                      drivers = root + QStringLiteral("/drivers"), index = root + QStringLiteral("/index");
        QDir().mkpath(drivers);
        QDir().mkpath(index);
        const QByteArray database = read(app.arguments().value(1) + QStringLiteral("/catalog.sqlite"));
        write(index + QStringLiteral("/db.sqlite"), database);
        write(index + QStringLiteral("/old.bin"), QByteArray("SDI index"));
        write(root + QStringLiteral("/device.snp"), QByteArray("old snapshot"));
        write(root + QStringLiteral("/app.exe"), QByteArray("fixture, never execute"));
        write(drivers + QStringLiteral("/DP_One_20045.7z"), QByteArray("fixture-one"));
        write(drivers + QStringLiteral("/DP_Two_20045.7z"), QByteArray("fixture-two"));
        write(drivers + QStringLiteral("/ads.exe"), QByteArray("excluded"));
        write(drivers + QStringLiteral("/unrelated.7z"), QByteArray("excluded"));
        check(DriverPackLibrary::locate(root).valid(), "library root pairs paths");
        check(DriverPackLibrary::locate(drivers).database.endsWith(QStringLiteral("/index/db.sqlite")),
              "drivers choice pairs database");
        check(DriverPackLibrary::locate(index + QStringLiteral("/db.sqlite")).drivers == drivers,
              "database choice pairs drivers");
        if (!QDir(root).rename(QStringLiteral("index"), QStringLiteral("indexes")))
            throw std::runtime_error("prepare alternate index fixture");
        check(DriverPackLibrary::locate(drivers).database.endsWith(QStringLiteral("/indexes/db.sqlite")),
              "parent discovery supports the alternate indexes folder without a dangling temporary list");
        if (!QDir(root).rename(QStringLiteral("indexes"), QStringLiteral("index")))
            throw std::runtime_error("restore index fixture");
        const QString empty = work.path() + QStringLiteral("/empty");
        QDir().mkpath(empty);
        check(!DriverPackLibrary::locate(empty).valid(),
              "missing library returns no match after visiting every alias");
        check(DropTarget::classify(QList<QUrl>() << QUrl::fromLocalFile(root)) == DropTarget::DriverFolder,
              "DriverPack folder drag accepted");
        check(DropTarget::classify(QList<QUrl>() << QUrl::fromLocalFile(
                                       index + QStringLiteral("/db.sqlite"))) == DropTarget::DatabaseFile,
              "SQLite drag accepted");
        check(DropTarget::classify(QList<QUrl>() << QUrl::fromLocalFile(
                                       root + QStringLiteral("/device.snp"))) == DropTarget::Rejected,
              "SDI snapshots rejected");
        check(DropTarget::classify(QList<QUrl>() << QUrl::fromLocalFile(
                                       index + QStringLiteral("/old.bin"))) == DropTarget::Rejected,
              "SDI binary indexes rejected");
        auto files =
            PortableLibrary::entries(root + QStringLiteral("/app.exe"), drivers, index, QStringList(), false);
        check(files.size() == 4, "portable manifest has executable database and only DP archives");
        check(files[1].relative == QStringLiteral("index/db.sqlite"),
              "portable database destination is DriverPack format");
        const QString destination = work.path() + QStringLiteral("/output");
        std::atomic<bool> stop(false);
        PortableLibrary::copy(files, destination, stop, {});
        check(read(destination + QStringLiteral("/index/db.sqlite")) == database,
              "portable database copied byte-for-byte");
        check(!QFile::exists(destination + QStringLiteral("/index/old.bin")) &&
                  !QFile::exists(destination + QStringLiteral("/drivers/ads.exe")),
              "no legacy indexes or programs copied");
        files = PortableLibrary::entries(root + QStringLiteral("/app.exe"), drivers, index,
                                         QStringList() << QStringLiteral("DP_One_20045.7z"), true);
        check(files.size() == 3, "recommended-only portable selection");
        check(read(index + QStringLiteral("/db.sqlite")) == database, "source catalog unchanged");
        check(rejected(
                  [&]
                  {
                      PortableLibrary::entries(root + QStringLiteral("/app.exe"), drivers, index,
                                               QStringList() << QStringLiteral("missing.7z"), true);
                  }),
              "empty required package selection rejected");
        DriverPackSoftware::Program localSoftware;
        localSoftware.id = QStringLiteral("1");
        localSoftware.name = QStringLiteral("Fixture application");
        localSoftware.localPath = root + QStringLiteral("/app.exe");
        localSoftware.version = QStringLiteral("1.0");
        auto ad = localSoftware;
        ad.id = QStringLiteral("67");
        ad.excluded = true;
        const QString softwareCatalog = work.path() + QStringLiteral("/catalog.json");
        const auto softwareFiles = PortableLibrary::softwareEntries(
            QVector<DriverPackSoftware::Program>() << localSoftware << ad, softwareCatalog);
        check(softwareFiles.size() == 2, "portable software includes only allowed package and data catalog");
        check(softwareFiles[1].relative == QStringLiteral("programs/catalog.json"),
              "portable software metadata needs no original launcher or JS");
        check(!read(softwareCatalog).contains("Keys") && !read(softwareCatalog).contains("Targeting"),
              "portable software catalog has no execution scripts");
        sdiomodern::InstallReport report;
        check(!DriverPackWorkflow::allowSoftwareAfterDrivers(report),
              "empty driver queue cannot auto-start software");
        sdiomodern::Result result;
        result.outcome = sdiomodern::Outcome::Success;
        report.items.push_back(result);
        check(DriverPackWorkflow::allowSoftwareAfterDrivers(report),
              "successful drivers permit the approved software stage");
        report.restoreFailed = true;
        check(!DriverPackWorkflow::allowSoftwareAfterDrivers(report),
              "restore-point failure stops software stage");
        report.restoreFailed = false;
        report.cancelled = true;
        check(!DriverPackWorkflow::allowSoftwareAfterDrivers(report),
              "driver cancellation stops software stage");
        report.cancelled = false;
        report.items[0].outcome = sdiomodern::Outcome::Failure;
        check(!DriverPackWorkflow::allowSoftwareAfterDrivers(report), "failed driver stops software stage");
        printf("%d DriverPack independence checks passed; no installation or network requests.\n", count);
        return 0;
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Independence test failed: %s\n", e.what());
        return 1;
    }
}
