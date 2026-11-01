#include "../src/library_maintenance.h"
#include "../src/software_catalog.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <cstdio>
#include <stdexcept>
static int count = 0;
static void check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
    ++count;
}
static void put(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).dir().absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        throw std::runtime_error("fixture write");
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        if (app.arguments().size() == 3 && app.arguments()[1] == QStringLiteral("--preview"))
        {
            const QString root = app.arguments()[2];
            const auto plan = LibraryMaintenance::preview(root);
            printf("Read-only preview: %d software records\n",
                   DriverPackSoftware::catalogRecords(root, false).size());
            for (const auto &entry : plan)
                printf("Remove legacy: %s\n", entry.toUtf8().constData());
            return 0;
        }
        QTemporaryDir temp;
        const QString root = temp.path();
        put(root + QStringLiteral("/index/db.sqlite"), QByteArray("SQLite format 3\0fixture", 23));
        put(root + QStringLiteral("/drivers/keep.7z"), "driver archive");
        put(root + QStringLiteral("/drivers/.driverpack-modern-downloads/catalog.torrent"),
            "resume metadata");
        put(root + QStringLiteral("/DriverPackModern.exe"), "MZmodern");
        put(root + QStringLiteral("/DriverPack.exe"), "MZlegacy");
        put(root + QStringLiteral("/Readme.txt"), "packager advertising");
        put(root + QStringLiteral("/bin/img/unused.png"), "unused");
        put(root + QStringLiteral("/bin/img/app.png"), "required icon");
        put(root + QStringLiteral("/catalog/LICENSE.txt"), "original license");
        put(root + QStringLiteral("/bin/programs/extra.exe"), "MZbundle");
        put(root + QStringLiteral("/programs/PotPlayer.exe"), "MZplayer");
        put(root + QStringLiteral("/programs/PotPlayerMini.ini"), "settings");
        put(root + QStringLiteral("/programs/OperaXP.exe"), "MZopera");
        put(root + QStringLiteral("/programs/downloader_browser.exe"), "MZadvertisement");
        put(root + QStringLiteral("/programs/unknown.exe"), "MZcustom");
        put(root + QStringLiteral("/index/advertising.url"), "advertising");
        put(root + QStringLiteral("/index/picture.jpg"), "unused image");
        put(root + QStringLiteral("/unused-folder/page.html"), "unused page");
        put(root + QStringLiteral("/programs/Security/AvastAntivirusA.exe"), "MZfull-offline");
        put(root + QStringLiteral("/programs/Other/AvastAntivirusA_light.exe"), "MZlight");
        put(root + QStringLiteral("/bin/programs/AvastAntivirusA.exe"), "MZlight");
        DriverPackSoftware::Program p;
        p.id = QStringLiteral("player-custom");
        p.name = QStringLiteral("Player");
        p.category = QStringLiteral("Player");
        p.fileName = QStringLiteral("PotPlayer.exe");
        p.icon = QStringLiteral("bin/img/app.png");
        DriverPackSoftware::writeCatalog(root, {p});
        const auto plan = LibraryMaintenance::preview(root);
        check(plan.filter(QStringLiteral("bin/")).size() > 0, "preview includes legacy bin files");
        check(QFileInfo(root + QStringLiteral("/DriverPack.exe")).exists(), "preview never deletes");
        LibraryMaintenance::clean(root);
        check(!QFileInfo(root + QStringLiteral("/bin")).exists(), "legacy UI removed");
        check(!QFileInfo(root + QStringLiteral("/catalog")).exists(), "legacy runtime removed");
        check(QFileInfo(root + QStringLiteral("/drivers/.driverpack-modern-downloads/catalog.torrent"))
                  .exists(),
              "Modern download and resume cache preserved");
        check(!QFileInfo(root + QStringLiteral("/DriverPack.exe")).exists(), "old launcher removed");
        check(!QFileInfo(root + QStringLiteral("/Readme.txt")).exists(), "unused packager readme removed by keep policy");
        check(QFileInfo(root + QStringLiteral("/DriverPackModern.exe")).exists(),
              "modern launcher preserved");
        check(QFileInfo(root + QStringLiteral("/index/db.sqlite")).exists(), "index preserved");
        check(QFileInfo(root + QStringLiteral("/drivers/keep.7z")).exists(), "driver archives preserved");
        check(QFileInfo(root + QStringLiteral("/programs/Media/PotPlayer.exe")).exists(),
              "media categorized");
        check(QFileInfo(root + QStringLiteral("/programs/Media/PotPlayerMini.ini")).exists(),
              "companion settings preserved");
        check(QFileInfo(root + QStringLiteral("/programs/Internet/OperaXP.exe")).exists(),
              "other OS variant preserved");
        check(QFileInfo(root + QStringLiteral("/programs/Other/unknown.exe")).exists(),
              "unknown ordinary installer preserved");
        check(QFileInfo(root + QStringLiteral("/programs/Legacy/extra.exe")).exists(),
              "bundled program preserved");
        check(!QFileInfo(root + QStringLiteral("/programs/downloader_browser.exe")).exists(),
              "known advertising downloader removed");
        check(QFileInfo(root + QStringLiteral("/licenses/OriginalDriverPack-NOTICES.txt")).exists(),
              "legal notices retained");
        const auto rows = DriverPackSoftware::catalogRecords(root);
        check(!DriverPackSoftware::iconPath(root, rows[0]).isEmpty(), "referenced icon survives deletion");
        check(!DriverPackSoftware::localProgram(root, rows[0], true).isEmpty(),
              "catalog installer resolves after migration");
        check(!QFileInfo(root + QStringLiteral("/index/advertising.url")).exists(),
              "index advertising removed by keep policy");
        check(!QFileInfo(root + QStringLiteral("/index/picture.jpg")).exists(), "unused index image removed");
        check(!QFileInfo(root + QStringLiteral("/unused-folder")).exists(),
              "unknown unused folder removed after confirmation");
        check(QFileInfo(root + QStringLiteral("/programs/Security/AvastAntivirusA_light.exe")).exists(),
              "light antivirus classified as Security");
        check(!QFileInfo(root + QStringLiteral("/programs/Legacy/AvastAntivirusA.exe")).exists(),
              "identical stub duplicate removed");
        QFile full(root + QStringLiteral("/programs/Security/AvastAntivirusA.exe"));
        full.open(QIODevice::ReadOnly);
        check(full.readAll() == QByteArray("MZfull-offline"),
              "full offline installer retained beside light variant");
        full.close();
        QFile notices(root + QStringLiteral("/licenses/OriginalDriverPack-NOTICES.txt"));
        notices.open(QIODevice::ReadOnly);
        check(notices.readAll().contains("original license"),
              "consolidated notice retains exact original license bytes");
        notices.close();
        check(!QFileInfo(root + QStringLiteral("/licenses/OriginalDriverPack")).exists(),
              "old individual notice copies removed only after consolidation");
        LibraryMaintenance::clean(root);
        check(true, "cleanup can be repeated safely");
        bool refused = false;
        try
        {
            LibraryMaintenance::preview(QDir(root).rootPath());
        }
        catch (...)
        {
            refused = true;
        }
        check(refused, "drive root refused");
        QTemporaryDir collision;
        const QString other = collision.path();
        put(other + QStringLiteral("/index/db.sqlite"), "SQLite fixture");
        put(other + QStringLiteral("/drivers/keep.7z"), "unchanged");
        put(other + QStringLiteral("/bin/ui.js"), "legacy UI");
        put(other + QStringLiteral("/programs/PotPlayer.exe"), "MZone");
        put(other + QStringLiteral("/programs/Media/PotPlayer.exe"), "MZdifferent");
        p.icon.clear();
        DriverPackSoftware::writeCatalog(other, {p});
        refused = false;
        try
        {
            LibraryMaintenance::clean(other);
        }
        catch (...)
        {
            refused = true;
        }
        check(refused, "installer collision fails closed");
        check(QFileInfo(other + QStringLiteral("/bin/ui.js")).exists(),
              "collision stops before legacy deletion");
#ifdef Q_OS_WIN
        QTemporaryDir linked, outside;
        put(linked.path() + QStringLiteral("/index/db.sqlite"), "SQLite fixture");
        put(linked.path() + QStringLiteral("/drivers/keep.7z"), "unchanged");
        put(outside.path() + QStringLiteral("/sentinel.txt"), "outside must survive");
        const QString junction = linked.path() + QStringLiteral("/bin");
        check(QProcess::execute(QStringLiteral("cmd.exe"),
                                QStringList() << QStringLiteral("/c") << QStringLiteral("mklink")
                                              << QStringLiteral("/J") << QDir::toNativeSeparators(junction)
                                              << QDir::toNativeSeparators(outside.path())) == 0,
              "junction fixture created");
        refused = false;
        try
        {
            LibraryMaintenance::preview(linked.path());
        }
        catch (...)
        {
            refused = true;
        }
        const bool removed =
            RemoveDirectoryW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(junction).utf16()));
        check(removed, "only test junction removed");
        check(refused, "junction in cleanup tree refused");
        check(QFileInfo(outside.path() + QStringLiteral("/sentinel.txt")).exists(),
              "outside junction target preserved");
#endif
        printf("%d cleanup checks passed on temporary fixtures only.\n", count);
        return 0;
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Cleanup test after %d: %s\n", count, e.what());
        return 1;
    }
}
