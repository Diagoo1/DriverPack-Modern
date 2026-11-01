#include "../src/software_catalog.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <cstdio>
#include <stdexcept>
#include <cstring>
static int count = 0;
static void check(bool v, const char *name)
{
    if (!v)
        throw std::runtime_error(name);
    ++count;
}
template <class F> static bool rejects(F f)
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
static QByteArray record(const char *id, const char *name, const char *url)
{
    return QByteArray("{\n        ID: \"") + id + "\",\n        Name: \"" + name + "\",\n        URL: \"" +
           url +
           "\",\n        Publisher: \"Fixture Publisher\",\n        Version: \"1.2.3\",\n        Category: "
           "\"Utility\",\n        Checked: !0,\n        Keys: \"--make-default --partner test\",\n        "
           "Targeting: function() { throw 'must never execute'; }\n    }";
}
int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--software-report") == 0)
        return DriverPackSoftware::runReport(argc, argv);
    QCoreApplication app(argc, argv);
    try
    {
        auto programs =
            DriverPackSoftware::parse(record("1", "Safe app", "https://fixture.invalid/7-Zip.exe") +
                                      record("7", "Partner", "http://fixture.invalid/downloader.exe") +
                                      record("67", "Cloud", "http://fixture.invalid/cloud.exe") +
                                      record("73", "System Libraries", "http://fixture.invalid/runtime.exe") +
                                      record("999", "Vendor script", "http://fixture.invalid/oem.exe"));
        check(programs.size() == 5, "literal catalog parsed without JS evaluation");
        check(programs[0].name == QStringLiteral("Safe app"), "display name from data");
        check(programs[0].version == QStringLiteral("1.2.3"), "version from data");
        check(programs[0].publisher == QStringLiteral("Fixture Publisher"), "publisher from data");
        check(!programs[0].excluded, "ordinary software remains optional");
        check(programs[1].excluded && programs[2].excluded, "partner bundle and cloud excluded");
        check(programs[3].toolkit, "system libraries classified as driver toolkit");
        check(programs[4].excluded, "vendor-script packages not blindly executed");
        check(rejects([]() { DriverPackSoftware::parse(QByteArray("arbitrary code")); }),
              "noncatalog JS rejected");
        check(rejects([]() { DriverPackSoftware::parse(QByteArray(8 * 1024 * 1024 + 1, 'x')); }),
              "catalog input bounded");
        check(!DriverPackSoftware::allowedUrl(QStringLiteral("file:///C:/evil.exe")), "file URLs rejected");
        check(!DriverPackSoftware::allowedUrl(QStringLiteral("https://user:pass@fixture.invalid/a.exe")),
              "credential URLs rejected");
        check(!DriverPackSoftware::allowedUrl(QStringLiteral("https://fixture.invalid/a.exe?partner=1")),
              "query-based partner endpoint rejected");
        QTemporaryDir library;
        QDir().mkpath(library.path() + QStringLiteral("/programs"));
        const QString file = library.path() + QStringLiteral("/programs/7-Zip.exe");
        {
            QFile out(file);
            check(out.open(QIODevice::WriteOnly) && out.write("MZfixture-only") > 0,
                  "isolated local package fixture");
        }
        check(DriverPackSoftware::localInstaller(library.path(),
                                                 QStringLiteral("https://fixture.invalid/7-Zip.exe")) == file,
              "local installer resolved inside programs only");
        check(DriverPackSoftware::localInstaller(library.path(),
                                                 QStringLiteral("https://fixture.invalid/downloader.exe"))
                  .isEmpty(),
              "partner downloader cannot be launched");
        check(DriverPackSoftware::localInstaller(library.path(),
                                                 QStringLiteral("https://fixture.invalid/evil.cmd"))
                  .isEmpty(),
              "scripts never resolved as installers");
        check(DriverPackSoftware::localInstaller(library.path(),
                                                 QStringLiteral("https://fixture.invalid/missing.exe"))
                  .isEmpty(),
              "missing package remains unavailable");
        {
            QFile bad(library.path() + QStringLiteral("/programs/notpe.exe"));
            bad.open(QIODevice::WriteOnly);
            bad.write("not a PE");
        }
        check(DriverPackSoftware::localInstaller(library.path(),
                                                 QStringLiteral("https://fixture.invalid/notpe.exe"), true)
                  .isEmpty(),
              "launch-time header check rejects non-executable data");
        const QByteArray json(
            R"DP({"format": "DriverPackModernSoftwareCatalog", "schema": 1, "programs": [{"id": "1", "name": "Fixture", "url": "https://fixture.invalid/7-Zip.exe", "Checked": true, "Keys": "never execute"}]})DP");
        check(DriverPackSoftware::parseJsonCatalog(json).size() == 1, "custom data-only catalog supported");
        check(rejects([]() { DriverPackSoftware::parseJsonCatalog(QByteArray(R"DP({"schema":9})DP")); }),
              "unsupported custom schema rejected");
        check(
            rejects(
                []()
                {
                    DriverPackSoftware::parseJsonCatalog(QByteArray(
                        R"DP({"format": "DriverPackModernSoftwareCatalog", "schema": 1, "programs": [{"id": "../script", "name": "Invalid"}]})DP"));
                }),
            "custom IDs cannot carry script paths");
        const auto fallback = DriverPackSoftware::load(library.path());
        check(!fallback.isEmpty(), "standalone executable has an embedded software catalog");
        check(fallback[0].origin == QStringLiteral("unified-catalog"),
              "embedded fallback is explicitly identified");
        DriverPackSoftware::Program local;
        local.fileName = QStringLiteral("7-Zip.exe");
        check(DriverPackSoftware::localProgram(library.path(), local, true) == file,
              "custom local filename requires no remote URL");
        local.fileName = QStringLiteral("../outside.exe");
        check(DriverPackSoftware::localProgram(library.path(), local, true).isEmpty(),
              "custom local filename cannot escape programs folder");
        const auto oem =
            DriverPackSoftware::parse(record("915", "Asus Tools", "https://fixture.invalid/Asus10.exe"));
        check(oem[0].toolkit && !oem[0].excluded,
              "known OEM utility retained for native device and OS targeting");
        const QByteArray details =
            record("1", "Metadata", "https://fixture.invalid/meta.exe")
                .replace("        Checked: !0,",
                         "        Rating: 4,\n        Description: {\n            en: \"Literal "
                         "description\",\n            ru: \"Other\"\n        },\n        policy: "
                         "\"https://fixture.invalid/privacy\",\n        Checked: !0,");
        const auto described = DriverPackSoftware::parse(details);
        check(described[0].description == QStringLiteral("Literal description"),
              "original literal description displayed without evaluating functions");
        check(described[0].rating == 4, "original software catalog rating retained");
        check(described[0].policy == QStringLiteral("https://fixture.invalid/privacy"),
              "original privacy link retained as data");
        const QByteArray overrideSource(R"DP({
    'ID': '0',
    'Name': 'A "quoted" app\'s',
    'URL': 'https://fixture.invalid/7-Zip.exe',
    'Rating': 4,
    'Description': {
      'en': 'Data only'
    },
    'Targeting': function() { throw 'never execute'; }
  }, {
    'ID': '1',
    'Name': 'Opera x64',
    'URL': 'https://fixture.invalid/OperaBlink64.exe'
  })DP");
        const auto quotedCatalog = DriverPackSoftware::parse(overrideSource);
        check(quotedCatalog.size() == 2 && quotedCatalog[0].name == QStringLiteral("A \"quoted\" app's"),
              "single-quoted override safely decodes quotes and apostrophes");
        check(quotedCatalog[0].id == QStringLiteral("1") && quotedCatalog[1].id == QStringLiteral("3"),
              "override IDs do not confuse 7-Zip and Opera targeting");
        const QByteArray duplicates(R"DP({
    'ID': '30',
    'Name': 'Avast',
    'URL': 'https://fixture.invalid/AvastAntivirusWorldwideA.exe'
  }, {
    'ID': '30',
    'Name': '360 Total Security',
    'URL': 'https://fixture.invalid/360tsNew.exe'
  })DP");
        const auto distinct = DriverPackSoftware::parse(duplicates);
        check(distinct.size() == 2 && distinct[0].id != distinct[1].id,
              "same raw ID cannot discard a different software package");
        QDir().mkpath(library.path() + QStringLiteral("/bin/js"));
        {
            QFile source(library.path() + QStringLiteral("/bin/js/soft.js"));
            source.open(QIODevice::WriteOnly);
            source.write(overrideSource);
        }
        {
            QFile fallback(library.path() + QStringLiteral("/bin/drp.js"));
            fallback.open(QIODevice::WriteOnly);
            fallback.write(record("20", "Fallback", "https://fixture.invalid/AIMP.exe"));
        }
        check(DriverPackSoftware::catalogRecords(library.path()).size() >= 2,
              "legacy sources do not replace an established catalog");
        QFile::remove(library.path() + QStringLiteral("/programs/catalog.json"));
        const auto overrideRows = DriverPackSoftware::load(library.path(), true);
        check(overrideRows.size() == 3 && overrideRows[0].origin == QStringLiteral("unified-catalog"),
              "actual local soft.js overrides embedded drp.js metadata without execution");
        const auto variant = DriverPackSoftware::parse(QByteArray(overrideSource)
                                                           .replace("'ID': '0'", "'ID': '6-64en'")
                                                           .replace("7-Zip.exe", "Firefox64en.exe"));
        check(variant.size() == 2 && variant[0].sourceId == QStringLiteral("6-64en"),
              "alphanumeric original variant IDs are retained");
        check(variant[0].id == QStringLiteral("66402") && variant[0].knownPackage,
              "Firefox variant uses stable package identity");

        const QByteArray customData =
            R"DP({"format":"DriverPackModernSoftwareCatalog","schema":1,"programs":[{"id":"custom-av","name":"Custom antivirus","category":"Antivirus","file":"custom-av.exe","icon":"programs/icons/custom.png","description":"User supplied package"}]})DP";
        auto customRows = DriverPackSoftware::parseJsonCatalog(customData);
        check(customRows.size() == 1 && customRows[0].icon == QStringLiteral("programs/icons/custom.png"),
              "custom icon metadata preserved");
        check(!customRows[0].excluded && customRows[0].category == QStringLiteral("Antivirus"),
              "antivirus category extensible beyond original two entries");
        QDir().mkpath(library.path() + QStringLiteral("/programs/icons"));
        {
            QFile icon(library.path() + QStringLiteral("/programs/icons/custom.png"));
            icon.open(QIODevice::WriteOnly);
            icon.write("fixture");
        }
        check(!DriverPackSoftware::iconPath(library.path(), customRows[0]).isEmpty(),
              "local custom icon resolves inside library");
        customRows[0].icon = QStringLiteral("../outside.png");
        check(DriverPackSoftware::iconPath(library.path(), customRows[0]).isEmpty(),
              "custom icon cannot escape library");
        {
            QFile overlay(library.path() + QStringLiteral("/programs/custom.json"));
            overlay.open(QIODevice::WriteOnly);
            overlay.write(customData);
        }
        QFile::remove(library.path() + QStringLiteral("/programs/catalog.json"));
        auto augmented = DriverPackSoftware::load(library.path(), true);
        check(augmented.size() == overrideRows.size() + 1 &&
                  augmented.last().id == QStringLiteral("custom-av"),
              "custom overlay adds a package without replacing original catalog");
        DriverPackSoftware::Program custom;
        custom.id = QStringLiteral("user-fixture");
        custom.name = QStringLiteral("Fixture package");
        custom.url = QStringLiteral("https://fixture.invalid/custom.exe");
        custom.category = QStringLiteral("Utility");
        DriverPackSoftware::saveCustomProgram(library.path(), custom);
        QFile saved(library.path() + QStringLiteral("/programs/catalog.json"));
        saved.open(QIODevice::ReadOnly);
        QByteArray added = saved.readAll();
        saved.close();
        check(DriverPackSoftware::parseJsonCatalog(added).size() == augmented.size() + 1,
              "adding a custom package preserves the existing manifest record");
        custom.version = QStringLiteral("2.0");
        DriverPackSoftware::saveCustomProgram(library.path(), custom);
        saved.open(QIODevice::ReadOnly);
        const QByteArray updated = saved.readAll();
        saved.close();
        const auto updatedRows = DriverPackSoftware::parseJsonCatalog(updated);
        check(updatedRows.size() == augmented.size() + 1 &&
                  updatedRows.last().version == QStringLiteral("2.0"),
              "saving an existing package updates metadata without duplicates");
        custom.name.clear();
        check(rejects([&]() { DriverPackSoftware::saveCustomProgram(library.path(), custom); }),
              "invalid metadata is rejected before writing");
        saved.open(QIODevice::ReadOnly);
        check(saved.readAll() == updated, "invalid metadata does not corrupt the existing user catalog");
        saved.close();
        custom.name = QStringLiteral("Fixture");
        custom.fileName = QStringLiteral("../escape.exe");
        check(rejects([&]() { DriverPackSoftware::saveCustomProgram(library.path(), custom); }),
              "custom installer paths cannot escape the local package folder");
        custom.fileName.clear();
        custom.url = QStringLiteral("https://user:secret@fixture.invalid/custom.exe");
        check(rejects([&]() { DriverPackSoftware::saveCustomProgram(library.path(), custom); }),
              "credential-bearing custom URLs are rejected");
        DriverPackSoftware::Program regular, cis, customOpera;
        regular.id = QStringLiteral("3");
        regular.knownPackage = true;
        regular.localPath = QStringLiteral("OperaBlink64.exe");
        cis.id = QStringLiteral("203");
        cis.knownPackage = true;
        cis.localPath = QStringLiteral("Opera64cis_woGoogle.exe");
        customOpera.id = QStringLiteral("my-opera");
        customOpera.name = QStringLiteral("Opera custom");
        QVector<DriverPackSoftware::Program> originals;
        originals << regular << cis << customOpera;
        auto variants = originals;
        DriverPackSoftware::resolvePackageVariants(variants, true);
        check(variants[0].excluded && !variants[1].excluded, "one Russian Opera variant is chosen");
        check(!variants[2].excluded, "user custom packages are not hidden by a display-name heuristic");
        variants = originals;
        DriverPackSoftware::resolvePackageVariants(variants, false);
        check(!variants[0].excluded && variants[1].excluded, "one regular Opera variant is chosen");
        variants = originals;
        variants[1].localPath.clear();
        DriverPackSoftware::resolvePackageVariants(variants, true);
        check(!variants[0].excluded && variants[1].excluded,
              "available local Opera takes priority over missing regional media");
        variants = originals;
        variants[1].excluded = true;
        DriverPackSoftware::resolvePackageVariants(variants, true);
        check(!variants[0].excluded && variants[1].excluded,
              "a target-incompatible variant is never revived");
        DriverPackSoftware::Program sharedA, sharedB;
        sharedA.id = QStringLiteral("shared-a");
        sharedA.name = QStringLiteral("Shared A");
        sharedA.fileName = QStringLiteral("shared.exe");
        sharedA.category = QStringLiteral("Antivirus");
        QDir().mkpath(library.path() + QStringLiteral("/programs/Security"));
        QFile shared(library.path() + QStringLiteral("/programs/Security/shared.exe"));
        shared.open(QIODevice::WriteOnly);
        shared.write("MZfixture");
        shared.close();
        DriverPackSoftware::saveCustomProgram(library.path(), sharedA);
        sharedB = sharedA;
        sharedB.id = QStringLiteral("shared-b");
        sharedB.name = QStringLiteral("Shared B");
        DriverPackSoftware::saveCustomProgram(library.path(), sharedB);
        check(!DriverPackSoftware::localProgram(library.path(), sharedA, true).isEmpty(),
              "categorized security installer resolves");
        check(rejects([&](){DriverPackSoftware::removeProgram(library.path(), sharedA.id, true);}),
              "installer shared by two records cannot be deleted");
        DriverPackSoftware::removeProgram(library.path(), sharedA.id, false);
        check(shared.exists(), "removing only metadata preserves the installer");
        DriverPackSoftware::removeProgram(library.path(), sharedB.id, true);
        check(!shared.exists(), "explicit installer deletion removes the unshared local file");
        DriverPackSoftware::removeProgram(library.path(), QStringLiteral("user-fixture"));
        check(DriverPackSoftware::catalogRecords(library.path()).size() == augmented.size(),
              "removal persists in unified catalog");
        DriverPackSoftware::writeCatalog(library.path(), {});
        check(DriverPackSoftware::load(library.path(), true).isEmpty(),
              "empty catalog never repopulates deleted legacy records");
        for(const auto &expected : QVector<QPair<QString,QString>>{{QStringLiteral("K-Lite.exe"),QStringLiteral("Media")},{QStringLiteral("FSImage.exe"),QStringLiteral("Tools")},{QStringLiteral("TeamViewer.exe"),QStringLiteral("Support")}}) {
            DriverPackSoftware::Program row;row.id=QStringLiteral("legacy");row.name=QStringLiteral("Legacy");row.fileName=expected.first;row.category=expected.first==QStringLiteral("TeamViewer.exe")?QStringLiteral("Internet"):QStringLiteral("Viewer");
            const auto repaired=DriverPackSoftware::parseJsonCatalog(DriverPackSoftware::catalogBytes({row}));
            check(DriverPackSoftware::categoryFolder(repaired[0].category)==expected.second,"legacy software classification corrected before folder mapping");
        }
        printf("%d software adapter checks passed; no installer, original JS or network executed.\n", count);
        return 0;
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Software test after %d checks: %s\n", count, e.what());
        return 1;
    }
}
