// SPDX-License-Identifier: GPL-3.0-or-later
// Literal data adapter only: never evaluates DriverPack JS, Targeting, Keys or hooks.
#include "software_catalog.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSettings>
#include <QSet>
#include <QMap>
#include <QSysInfo>
#include <QTextCodec>
#include <QLocale>
#include <stdexcept>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QSaveFile>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace DriverPackSoftware
{
static bool oemId(int id)
{
    switch (id)
    {
    case 910:
    case 912:
    case 913:
    case 914:
    case 915:
    case 930:
    case 931:
    case 932:
    case 950:
    case 960:
    case 965:
    case 966:
    case 980:
    case 990:
        return true;
    default:
        return false;
    }
}
static QString quoted(const QString &token)
{
    if (token.size() < 2)
        return QString();
    QString json;
    if (token[0] == QLatin1Char('"'))
        json = token;
    else if (token[0] == QLatin1Char('\''))
    {
        json = QStringLiteral("\"");
        for (int i = 1; i < token.size() - 1; ++i)
        {
            const QChar c = token[i];
            if (c == QLatin1Char('\\'))
            {
                if (++i >= token.size() - 1)
                    return QString();
                if (token[i] == QLatin1Char('\''))
                    json += QLatin1Char('\'');
                else
                {
                    json += QLatin1Char('\\');
                    json += token[i];
                }
            }
            else if (c == QLatin1Char('"'))
                json += QStringLiteral("\\\"");
            else
                json += c;
        }
        json += QLatin1Char('"');
    }
    else
        return QString();
    const auto value = QJsonDocument::fromJson((QLatin1Char('[') + json + QLatin1Char(']')).toUtf8());
    return value.isArray() && value.array().size() == 1 ? value.array().at(0).toString() : QString();
}
static QString literal(const QString &block, const QString &key, int indent)
{
    const QString token = QString::fromLatin1(R"DP(("(?:[^"\\\r\n]|\\.)*"|'(?:[^'\\\r\n]|\\.)*'))DP");
    const QString escaped = QRegularExpression::escape(key);
    const QString pattern =
        QStringLiteral("(?m)^[ \\t]{%1}(?:%2|\"%2\"|'%2')[ \\t]*:[ \\t]*").arg(indent).arg(escaped) + token +
        QStringLiteral("[ \\t]*,?[ \\t]*$");
    const auto found = QRegularExpression(pattern).match(block);
    return found.hasMatch() ? quoted(found.captured(1)) : QString();
}
static QString canonicalId(const QString &source, const QString &url, bool *known)
{
    const QString name = QFileInfo(QUrl(url).path()).fileName().toLower();
    const QMap<QString, int> ids = {{QStringLiteral("7-zip.exe"), 1},
                                    {QStringLiteral("7-zip64.exe"), 1},
                                    {QStringLiteral("operablink64.exe"), 3},
                                    {QStringLiteral("operablink.exe"), 4},
                                    {QStringLiteral("opera64cis_wogoogle.exe"), 203},
                                    {QStringLiteral("opera86cis_wogoogle.exe"), 204},
                                    {QStringLiteral("operaxp.exe"), 5},
                                    {QStringLiteral("firefox.exe"), 6},
                                    {QStringLiteral("firefox64ru.exe"), 66401},
                                    {QStringLiteral("firefox64en.exe"), 66402},
                                    {QStringLiteral("firefox86ru.exe"), 68601},
                                    {QStringLiteral("firefox86en.exe"), 68602},
                                    {QStringLiteral("pdfviewer.exe"), 10},
                                    {QStringLiteral("k-lite.exe"), 11},
                                    {QStringLiteral("skypexp.exe"), 12},
                                    {QStringLiteral("skype.exe"), 13},
                                    {QStringLiteral("utorrent.exe"), 16},
                                    {QStringLiteral("potplayer.exe"), 18},
                                    {QStringLiteral("aimp.exe"), 20},
                                    {QStringLiteral("fsimage.exe"), 21},
                                    {QStringLiteral("teamviewer.exe"), 22},
                                    {QStringLiteral("backupper.exe"), 23},
                                    {QStringLiteral("avastantivirusa.exe"), 28},
                                    {QStringLiteral("avastantivirus.exe"), 28},
                                    {QStringLiteral("avastantivirusworldwidea.exe"), 29},
                                    {QStringLiteral("avastantivirusworldwide.exe"), 29},
                                    {QStringLiteral("360tsnew.exe"), 30},
                                    {QStringLiteral("winrarx86eng.exe"), 40},
                                    {QStringLiteral("winrarx86rus.exe"), 41},
                                    {QStringLiteral("winrarx86br.exe"), 42},
                                    {QStringLiteral("directx.exe"), 71},
                                    {QStringLiteral("visualcplus.exe"), 72},
                                    {QStringLiteral("runtimepack.exe"), 73},
                                    {QStringLiteral("dotnet.exe"), 74},
                                    {QStringLiteral("dotnetxp.exe"), 75},
                                    {QStringLiteral("asusxp.exe"), 910},
                                    {QStringLiteral("asus7.exe"), 912},
                                    {QStringLiteral("asus8.exe"), 913},
                                    {QStringLiteral("asus81.exe"), 914},
                                    {QStringLiteral("asus10.exe"), 915},
                                    {QStringLiteral("dell7.exe"), 930},
                                    {QStringLiteral("dell81.exe"), 931},
                                    {QStringLiteral("dell10.exe"), 932},
                                    {QStringLiteral("fujitsunt.exe"), 950},
                                    {QStringLiteral("hpnt.exe"), 960},
                                    {QStringLiteral("msi86.exe"), 965},
                                    {QStringLiteral("msix86.exe"), 965},
                                    {QStringLiteral("msi64.exe"), 966},
                                    {QStringLiteral("msix64.exe"), 966},
                                    {QStringLiteral("toshibant.exe"), 980},
                                    {QStringLiteral("viziont.exe"), 990}};
    *known = ids.contains(name);
    return *known ? QString::number(ids.value(name)) : source;
}
static bool blockedFile(const QString &url)
{
    const QString name = QFileInfo(QUrl(url).path()).fileName().toLower();
    return name.startsWith(QStringLiteral("downloader")) || name.contains(QStringLiteral("yandex")) ||
           name == QStringLiteral("chrone.exe") || name == QStringLiteral("empty.cmd") ||
           (name.contains(QStringLiteral("driverpack")) &&
            (name.contains(QStringLiteral("cloud")) || name.contains(QStringLiteral("alice")) ||
             name.contains(QStringLiteral("notifier"))));
}
bool allowedUrl(const QString &url)
{
    QUrl value(url);
    return value.isValid() &&
           (value.scheme() == QStringLiteral("https") || value.scheme() == QStringLiteral("http")) &&
           value.userInfo().isEmpty() && !value.host().isEmpty() && value.query().isEmpty() &&
           value.fragment().isEmpty();
}
static void repairLegacyCategory(Program &row) {
    if(!row.knownPackage)return;
    if(row.id==QStringLiteral("11")&&row.category==QStringLiteral("Viewer"))row.category=QStringLiteral("Codec");
    if(row.id==QStringLiteral("21")&&row.category==QStringLiteral("Viewer"))row.category=QStringLiteral("ImageViewer");
    if(row.id==QStringLiteral("22")&&row.category==QStringLiteral("Internet"))row.category=QStringLiteral("Support");
}
QVector<Program> parse(const QByteArray &bytes)
{
    if (bytes.isEmpty() || bytes.size() > 8 * 1024 * 1024)
        throw std::runtime_error("Missing or oversized DriverPack software catalog.");
    QTextCodec::ConverterState state;
    QString source = QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
    if (state.invalidChars)
        source = QTextCodec::codecForName("Windows-1251")->toUnicode(bytes);
    const QRegularExpression start(QString::fromLatin1(
        R"DP(\{\s*(?:ID|"ID"|'ID')\s*:\s*["']([A-Za-z0-9_-]{1,64})["']\s*,\s*(?:Name|"Name"|'Name')\s*:\s*("(?:[^"\\\r\n]|\\.)+"|'(?:[^'\\\r\n]|\\.)+'))DP"));
    if (!start.isValid())
        throw std::runtime_error(start.errorString().toStdString());
    QVector<QRegularExpressionMatch> records;
    auto matches = start.globalMatch(source);
    while (matches.hasNext())
    {
        if (records.size() >= 512)
            throw std::runtime_error("Too many software records.");
        records.append(matches.next());
    }
    QVector<Program> result;
    QSet<QString> seen;
    const QSet<QString> promotional = QSet<QString>()
                                      << QStringLiteral("7") << QStringLiteral("8") << QStringLiteral("9")
                                      << QStringLiteral("26") << QStringLiteral("27") << QStringLiteral("67")
                                      << QStringLiteral("68") << QStringLiteral("69") << QStringLiteral("101")
                                      << QStringLiteral("102") << QStringLiteral("1000");
    for (int i = 0; i < records.size(); ++i)
    {
        const auto match = records[i];
        const int end = i + 1 < records.size() ? records[i + 1].capturedStart() : source.size();
        const QString block = source.mid(match.capturedStart(), qMin(65536, end - match.capturedStart()));
        const auto spaces =
            QRegularExpression(QString::fromLatin1(R"DP((?m)^([ \t]*)(?:ID|"ID"|'ID')\s*:)DP")).match(block);
        const int indent = spaces.hasMatch() ? spaces.captured(1).size() : 8;
        Program program;
        program.sourceId = match.captured(1);
        program.name = quoted(match.captured(2));
        if (program.name.isEmpty() || program.name.size() > 512)
            continue;
        program.url = literal(block, QStringLiteral("URL"), indent);
        program.id = canonicalId(program.sourceId, program.url, &program.knownPackage);
        if (seen.contains(program.id))
            continue;
        seen.insert(program.id);
        program.publisher = literal(block, QStringLiteral("Publisher"), indent);
        program.version = literal(block, QStringLiteral("Version"), indent);
        program.category = literal(block, QStringLiteral("Category"), indent);
        program.license = literal(block, QStringLiteral("LicenseAgreement"), indent);
        program.registry = literal(block, QStringLiteral("Registry_32"), indent);
        program.icon = literal(block, QStringLiteral("Icon"), indent);
        program.policy = literal(block, QStringLiteral("policy"), indent);
        if (program.license.isEmpty())
            program.license = literal(block, QStringLiteral("eula"), indent);
        const QString token = QString::fromLatin1(R"DP(("(?:[^"\\\r\n]|\\.)*"|'(?:[^'\\\r\n]|\\.)*'))DP");
        const auto desc = QRegularExpression(
                              QStringLiteral("(?m)^[ \\t]{%1}(?:Description|\"Description\"|'Description'):[ "
                                             "\\t]*\\{\\s*(?:en|\"en\"|'en'):[ \\t]*")
                                  .arg(indent) +
                              token)
                              .match(block);
        if (desc.hasMatch())
            program.description = quoted(desc.captured(1));
        const auto localized =
            QRegularExpression(
                QStringLiteral("(?m)^[ \\t]{%1}(?:localizedName|\"localizedName\"|'localizedName'):[ "
                               "\\t]*\\{\\s*(?:en|\"en\"|'en'):[ \\t]*")
                    .arg(indent) +
                token)
                .match(block);
        if (localized.hasMatch() && !quoted(localized.captured(1)).isEmpty())
            program.name = quoted(localized.captured(1));
        const auto rating =
            QRegularExpression(
                QStringLiteral("(?m)^[ \\t]{%1}(?:Rating|\"Rating\"|'Rating'):[ \\t]*([0-5])[,\\s]")
                    .arg(indent))
                .match(block);
        if (rating.hasMatch())
            program.rating = rating.captured(1).toInt();
        const int id = program.id.toInt(), raw = program.sourceId.toInt();
        program.toolkit = (id >= 71 && id <= 75) || (oemId(id) && program.knownPackage);
        program.excluded = blockedFile(program.url) || promotional.contains(program.sourceId) || raw == 24 ||
                           (raw >= 900 && !program.knownPackage);
        if (program.excluded)
            program.reason = QStringLiteral(
                "Promotional, cloud, obsolete patch or unsupported scripted package is excluded.");
        if (!allowedUrl(program.url))
            program.reason =
                QStringLiteral("No supported static package URL; original expressions are never evaluated.");
        repairLegacyCategory(program);
        result.append(program);
    }
    if (result.isEmpty())
        throw std::runtime_error("This DriverPack build has no supported literal software catalog.");
    return result;
}
QVector<Program> parseJsonCatalog(const QByteArray &bytes)
{
    if (bytes.size() > 8 * 1024 * 1024)
        throw std::runtime_error("Software catalog is too large.");
    const auto document = QJsonDocument::fromJson(bytes);
    const auto root = document.object();
    if (!document.isObject() ||
        root.value(QStringLiteral("format")).toString() !=
            QStringLiteral("DriverPackModernSoftwareCatalog") ||
        root.value(QStringLiteral("schema")).toInt() != 1)
        throw std::runtime_error("Unsupported software catalog format.");
    const auto items = root.value(QStringLiteral("programs")).toArray();
    if (!root.value(QStringLiteral("programs")).isArray() || items.size() > 512)
        throw std::runtime_error("Invalid software catalog count.");
    QVector<Program> result;
    QSet<QString> ids;
    for (const auto &value : items)
    {
        const auto item = value.toObject();
        Program row;
        row.id = item.value(QStringLiteral("id")).toString();
        row.name = item.value(QStringLiteral("name")).toString();
        row.url = item.value(QStringLiteral("url")).toString();
        row.fileName = item.value(QStringLiteral("file")).toString();
        row.publisher = item.value(QStringLiteral("publisher")).toString();
        row.version = item.value(QStringLiteral("version")).toString();
        row.category = item.value(QStringLiteral("category")).toString();
        row.registry = item.value(QStringLiteral("registry")).toString();
        row.description = item.value(QStringLiteral("description")).toString().left(16384);
        row.icon = item.value(QStringLiteral("icon")).toString().left(2048);
        row.license = item.value(QStringLiteral("license")).toString().left(2048);
        row.policy = item.value(QStringLiteral("policy")).toString().left(2048);
        row.rating = qBound(0, item.value(QStringLiteral("rating")).toInt(), 5);
        row.sourceId = row.id;
        const QString identity =
            row.fileName.isEmpty() ? row.url : QStringLiteral("https://catalog.invalid/") + row.fileName;
        row.id = canonicalId(row.id, identity, &row.knownPackage);
        if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,64}$")).match(row.id).hasMatch() ||
            row.name.isEmpty() || row.name.size() > 512 || row.url.size() > 2048 ||
            row.publisher.size() > 512 || ids.contains(row.id))
            throw std::runtime_error("Invalid or duplicate software record.");
        ids.insert(row.id);
        const int id = row.id.toInt();
        row.toolkit = (id >= 71 && id <= 75) || oemId(id);
        row.excluded = blockedFile(row.url) || (id >= 900 && !row.knownPackage) ||
                       QStringList({QStringLiteral("7"), QStringLiteral("8"), QStringLiteral("9"),
                                    QStringLiteral("24"), QStringLiteral("26"), QStringLiteral("27"),
                                    QStringLiteral("67"), QStringLiteral("68"), QStringLiteral("69")})
                           .contains(row.id);
        repairLegacyCategory(row);
        result.append(row);
    }
    return result;
}
QString categoryFolder(const QString &category)
{
    if (category == QStringLiteral("Browser") || category == QStringLiteral("Internet"))
        return QStringLiteral("Internet");
    if (category == QStringLiteral("Antivirus") || category == QStringLiteral("Security"))
        return QStringLiteral("Security");
    if (category == QStringLiteral("Player") || category == QStringLiteral("Codec") ||
        category == QStringLiteral("Media"))
        return QStringLiteral("Media");
    if (category == QStringLiteral("Messenger"))
        return QStringLiteral("Communication");
    if (category == QStringLiteral("Backup"))
        return QStringLiteral("Backup");
    if(category==QStringLiteral("Support"))return QStringLiteral("Support");
    if(category==QStringLiteral("ImageViewer"))return QStringLiteral("Tools");
    if (category == QStringLiteral("Viewer") || category == QStringLiteral("Office"))
        return QStringLiteral("Office");
    if (category == QStringLiteral("System") || category == QStringLiteral("Driver"))
        return QStringLiteral("System");
    return QStringLiteral("Tools");
}
static QString localByName(const QString &library, const QString &name, bool verifyHeader)
{
    if (name.isEmpty() || name.size() > 180 || name.contains(QLatin1Char('/')) ||
        name.contains(QLatin1Char('\\')) || name.contains(QStringLiteral("..")) ||
        !name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) || name.contains(QLatin1Char('%')) ||
        name.contains(QLatin1Char(':')) || blockedFile(name))
        return QString();
    const QString root = QFileInfo(library).canonicalFilePath();
    QStringList folders;
    folders << QDir(library).filePath(QStringLiteral("programs"));
    for (const auto &folder : {"Internet", "Security", "Media", "Communication", "Backup", "Office", "Support", "System",
                               "Tools", "Other", "Legacy"})
        folders << QDir(library).filePath(QStringLiteral("programs/") + QString::fromLatin1(folder));
    folders << QDir(library).filePath(QStringLiteral("bin/programs"));
    for (const auto &folder : folders)
    {
        const QFileInfo dir(folder), file(QDir(folder).filePath(name));
        const QString path = file.canonicalFilePath();
        if (!root.isEmpty() && !dir.isSymLink() && file.isFile() && !file.isSymLink() &&
            path.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive) && file.size() >= 2)
        {
            if (!verifyHeader)
                return path;
            QFile input(path);
            if (input.open(QIODevice::ReadOnly) && input.read(2) == QByteArray("MZ"))
                return path;
        }
    }
    return QString();
}

QString iconPath(const QString &library, const Program &program)
{
    const QString icon = program.icon;
    const QString name = QUrl(icon).fileName();
    if (!icon.contains(QStringLiteral("://")) && !QDir::isAbsolutePath(icon) &&
        !icon.contains(QStringLiteral("..")) && !icon.isEmpty())
    {
        const QFileInfo file(QDir(library).filePath(icon));
        const QString base = QFileInfo(library).canonicalFilePath();
        if (file.isFile() && file.size() <= 524288 && !file.isSymLink() &&
            file.canonicalFilePath().startsWith(base + QLatin1Char('/'), Qt::CaseInsensitive))
            return file.canonicalFilePath();
    }
    const QString builtin = QStringLiteral(":/driverpack/program-icons/") + name;
    if (!name.isEmpty() && QFile::exists(builtin))
        return builtin;
    return QString();
}
QString localInstaller(const QString &library, const QString &source, bool verifyHeader)
{
    if (!allowedUrl(source))
        return QString();
    return localByName(library, QFileInfo(QUrl(source).path()).fileName(), verifyHeader);
}
QString localProgram(const QString &library, const Program &program, bool verifyHeader)
{
    return program.fileName.isEmpty() ? localInstaller(library, program.url, verifyHeader)
                                      : localByName(library, program.fileName, verifyHeader);
}
static bool registered(const QString &path)
{
    if (path.size() > 1024)
        return false;
    QString full = path;
    if (full.startsWith(QStringLiteral("HKLM\\")))
        full.replace(0, 5, QStringLiteral("HKEY_LOCAL_MACHINE\\"));
    else if (full.startsWith(QStringLiteral("HKCU\\")))
        full.replace(0, 5, QStringLiteral("HKEY_CURRENT_USER\\"));
    else
        return false;
    const int split = full.lastIndexOf(QLatin1Char('\\'));
    if (split < 0)
        return false;
    QSettings registry(full.left(split), QSettings::NativeFormat);
    return registry.contains(full.mid(split + 1)) &&
           !registry.value(full.mid(split + 1)).toString().isEmpty();
}
QByteArray catalogBytes(const QVector<Program> &programs)
{
    QJsonArray rows;
    for (const auto &p : programs)
    {
        QJsonObject row;
        row.insert(QStringLiteral("id"), p.id);
        row.insert(QStringLiteral("name"), p.name);
        row.insert(QStringLiteral("publisher"), p.publisher);
        row.insert(QStringLiteral("version"), p.version);
        row.insert(QStringLiteral("category"), p.category);
        row.insert(QStringLiteral("file"), p.fileName);
        row.insert(QStringLiteral("url"), p.url);
        row.insert(QStringLiteral("icon"), p.icon);
        row.insert(QStringLiteral("description"), p.description);
        row.insert(QStringLiteral("registry"), p.registry);
        row.insert(QStringLiteral("license"), p.license);
        row.insert(QStringLiteral("policy"), p.policy);
        row.insert(QStringLiteral("rating"), p.rating);
        rows.append(row);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("DriverPackModernSoftwareCatalog"));
    root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("programs"), rows);
    return QJsonDocument(root).toJson();
}
void writeCatalog(const QString &library, const QVector<Program> &programs)
{
    const QString root = QFileInfo(library).canonicalFilePath();
    if (root.isEmpty() || QFileInfo(QDir(root).filePath(QStringLiteral("programs"))).isSymLink())
        throw std::runtime_error("Choose a writable local library with an ordinary programs folder.");
    const QByteArray bytes = catalogBytes(programs);
    parseJsonCatalog(bytes);
    const QString folder = QDir(root).filePath(QStringLiteral("programs"));
    if (!QDir().mkpath(folder))
        throw std::runtime_error("Cannot create programs folder.");
    const QString path = QDir(folder).filePath(QStringLiteral("catalog.json"));
    if (QFileInfo(path).isSymLink())
        throw std::runtime_error("Catalog cannot be a symbolic link.");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Cannot save the unified programs/catalog.json.");
}
QVector<Program> catalogRecords(const QString &library, bool create)
{
    const QString target = QDir(library).filePath(QStringLiteral("programs/catalog.json"));
    auto read = [](const QString &path, bool json)
    {
        QFile file(path);
        if (QFileInfo(path).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
            throw std::runtime_error("Cannot read software catalog source.");
        return json ? parseJsonCatalog(file.readAll()) : parse(file.readAll());
    };
    if (QFileInfo(target).exists())
        return read(target, true);
    QVector<Program> result;
    bool legacy = false;
    for (const auto &source : {QStringLiteral("bin/drp.js"), QStringLiteral("bin/js/soft.js"),
                               QStringLiteral("programs/custom.json")})
    {
        const QString path = QDir(library).filePath(source);
        if (!QFileInfo(path).exists())
            continue;
        legacy = true;
        const auto items = read(path, source.endsWith(QStringLiteral(".json")));
        for (const auto &item : items)
        {
            if (item.excluded)
                continue;
            int found = -1;
            for (int i = 0; i < result.size(); ++i)
                if (result[i].id == item.id)
                {
                    found = i;
                    break;
                }
            if (found < 0)
                result << item;
            else
                result[found] = item;
        }
    }
    if (!legacy)
        result = read(QStringLiteral(":/driverpack/software-base.json"), true);
    QVector<Program> ordinary;
    for (auto item : result)
        if (!item.excluded)
        {
            if (item.fileName.isEmpty())
                item.fileName = QUrl(item.url).fileName();
            ordinary << item;
        }
    if (create)
        writeCatalog(library, ordinary);
    return ordinary;
}
void saveCustomProgram(const QString &library, const Program &program)
{
    const auto normalized = parseJsonCatalog(catalogBytes(QVector<Program>() << program));
    if (normalized.isEmpty() || normalized[0].excluded)
        throw std::runtime_error("Invalid software record.");
    auto rows = catalogRecords(library);
    int old = -1;
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i].id == program.id)
            old = i;
    if (old < 0 && program.fileName.isEmpty() && !allowedUrl(program.url))
        throw std::runtime_error("Provide a local installer or a supported publisher URL.");
    if (!program.fileName.isEmpty() && (old < 0 || rows[old].fileName != program.fileName) &&
        localProgram(library, program, true).isEmpty())
        throw std::runtime_error("Place a valid EXE in programs or its category folder first.");
    if (!program.url.isEmpty() && !allowedUrl(program.url))
        throw std::runtime_error("Invalid publisher URL.");
    for (int i = 0; i < rows.size(); ++i)
        if (i != old && rows[i].id == normalized[0].id)
            throw std::runtime_error("This package is already in the unified catalog.");
    if (old < 0)
        rows << normalized[0];
    else
        rows[old] = normalized[0];
    writeCatalog(library, rows);
}
QString removeProgram(const QString &library, const QString &id, bool removeInstaller)
{
    auto rows = catalogRecords(library);
    QString file;
    bool found = false;
    for (int i = rows.size() - 1; i >= 0; --i)
        if (rows[i].id == id)
        {
            file = localProgram(library, rows[i], false);
            rows.remove(i);
            found = true;
        }
    if (!found)
        throw std::runtime_error("Program is no longer in the catalog.");
    if (removeInstaller && !file.isEmpty())
        for (const auto &row : rows)
            if (localProgram(library, row, false) == file)
                throw std::runtime_error("Another catalog entry uses this installer. Remove the entry only.");
    writeCatalog(library, rows);
    if (removeInstaller && !file.isEmpty() && !QFile::remove(file))
        return QStringLiteral(
            "The catalog entry was removed, but Windows could not delete the installer file.");
    return QString();
}
bool promotionalPackage(const QString &file)
{
    // Cleanup only removes identified bundle filenames, never an unknown executable by a substring.
    static const QStringList names = {
        QStringLiteral("chrone.exe"), QStringLiteral("yandexliteusa.exe"),
        QStringLiteral("driverpack-alice.exe"), QStringLiteral("empty.cmd"),
        QStringLiteral("downloader_browser.exe"), QStringLiteral("downloader_browser_light.exe"),
        QStringLiteral("downloader_browser_tr.exe"), QStringLiteral("downloader_browser_tr_light.exe"),
        QStringLiteral("downloader_elements.exe"), QStringLiteral("downloader_elements_light.exe")};
    return names.contains(QFileInfo(file).fileName().toLower());
}
QVector<Program> load(const QString &library, bool includeOtherWindows, bool persistCatalog)
{
    const bool trace = qgetenv("DRIVERPACK_SOFTWARE_TRACE") == "1";
    auto rows = catalogRecords(library, persistCatalog);
    const QString origin = QStringLiteral("unified-catalog");
    const auto inventory = installedPrograms();

    int at = 0;
    for (Program &row : rows)
    {
        ++at;
        row.origin = origin;
        if (row.excluded)
            continue;
        if (trace)
        {
            fprintf(stderr, "software: local file %d\n", at);
            fflush(stderr);
        }
        row.localPath = localProgram(library, row);
        if (trace)
        {
            fprintf(stderr, "software: registration %d\n", at);
            fflush(stderr);
        }
        row.installed = registered(row.registry);
        if (!row.knownPackage && !row.installed)
            for (const auto &entry : inventory)
                if (entry.name.trimmed().compare(row.name.trimmed(), Qt::CaseInsensitive) == 0 &&
                    (row.publisher.isEmpty() ||
                     entry.publisher.trimmed().compare(row.publisher.trimmed(), Qt::CaseInsensitive) == 0))
                {
                    row.installed = true;
                    break;
                }
        const QMap<QString, QStringList> aliases = {
            {QStringLiteral("1"), QStringList() << QStringLiteral("7-Zip")},
            {QStringLiteral("6"), QStringList()
                                      << QStringLiteral("Mozilla Firefox") << QStringLiteral("Firefox")},
            {QStringLiteral("3"), QStringList() << QStringLiteral("Opera")},
            {QStringLiteral("4"), QStringList() << QStringLiteral("Opera")},
            {QStringLiteral("5"), QStringList() << QStringLiteral("Opera")},
            {QStringLiteral("203"), QStringList() << QStringLiteral("Opera")},
            {QStringLiteral("204"), QStringList() << QStringLiteral("Opera")},
            {QStringLiteral("10"), QStringList() << QStringLiteral("PDF-XChange")},
            {QStringLiteral("11"), QStringList() << QStringLiteral("K-Lite")},
            {QStringLiteral("30"), QStringList() << QStringLiteral("360 Total Security")},
            {QStringLiteral("20"), QStringList() << QStringLiteral("AIMP")},
            {QStringLiteral("21"), QStringList() << QStringLiteral("FastStone Image Viewer")},
            {QStringLiteral("22"), QStringList() << QStringLiteral("TeamViewer")},
            {QStringLiteral("23"), QStringList() << QStringLiteral("AOMEI Backupper")},
            {QStringLiteral("40"), QStringList() << QStringLiteral("WinRAR")},
            {QStringLiteral("41"), QStringList() << QStringLiteral("WinRAR")},
            {QStringLiteral("42"), QStringList() << QStringLiteral("WinRAR")}};
        for (const auto &installed : inventory)
            for (const QString &alias : row.knownPackage ? aliases.value(row.id) : QStringList())
                if (installed.name.startsWith(alias, Qt::CaseInsensitive))
                    row.installed = true;
        if (row.knownPackage &&
            QUrl(row.url).fileName().startsWith(QStringLiteral("Firefox"), Qt::CaseInsensitive))
            for (const auto &installed : inventory)
                if (installed.name.startsWith(QStringLiteral("Mozilla Firefox"), Qt::CaseInsensitive) ||
                    installed.name.startsWith(QStringLiteral("Firefox"), Qt::CaseInsensitive))
                    row.installed = true;
#ifdef Q_OS_WIN
        if (!includeOtherWindows && row.knownPackage)
        {
            const int id = row.id.toInt();
            const auto os = QSysInfo::windowsVersion();
            const bool is64 = QSysInfo::currentCpuArchitecture().contains(QStringLiteral("64"));
            bool target = true;
            if (id == 203 || id == 204)
                target = os >= QSysInfo::WV_VISTA && QLocale::system().language() == QLocale::Russian &&
                         is64 == (id == 203);
            if (id == 66401 || id == 68601)
                target = false;
            if (id == 66402)
                target = is64 && os >= QSysInfo::WV_VISTA;
            if (id == 68602)
                target = !is64 && os >= QSysInfo::WV_VISTA;
            if (id == 3)
                target = os >= QSysInfo::WV_VISTA && is64;
            if (id == 4)
                target = os >= QSysInfo::WV_VISTA && !is64;
            if (id == 5 || id == 12 || id == 75)
                target = os < QSysInfo::WV_VISTA;
            if (id == 13 || id == 74)
                target = os >= QSysInfo::WV_VISTA;
            if (id == 28)
                target = QLocale::system().country() == QLocale::Russia;
            if (id == 29)
                target = QLocale::system().country() != QLocale::Russia;
            if (id == 41)
                target = QLocale::system().language() == QLocale::Russian;
            if (id == 42)
                target = QLocale::system().language() == QLocale::Portuguese;
            if (id == 40)
                target = QLocale::system().language() != QLocale::Russian &&
                         QLocale::system().language() != QLocale::Portuguese;
            if (oemId(id))
            {
                QSettings bios(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS"),
                               QSettings::NativeFormat);
                const QString maker = bios.value(QStringLiteral("SystemManufacturer")).toString().toLower();
                SYSTEM_POWER_STATUS power = {};
                const bool notebook =
                    GetSystemPowerStatus(&power) && power.BatteryFlag != 128 && power.BatteryFlag != 255;
                QString vendor;
                if (id >= 910 && id <= 915)
                    vendor = QStringLiteral("asus");
                else if (id >= 930 && id <= 932)
                    vendor = QStringLiteral("dell");
                else if (id == 950)
                    vendor = QStringLiteral("fujitsu");
                else if (id == 960)
                    vendor = QStringLiteral("hp");
                else if (id == 965 || id == 966)
                    vendor = QStringLiteral("msi");
                else if (id == 980)
                    vendor = QStringLiteral("toshiba");
                else if (id == 990)
                    vendor = QStringLiteral("vizio");
                target = notebook && maker.contains(vendor);
                if (id == 910)
                    target = target && os < QSysInfo::WV_VISTA;
                if (id == 912 || id == 930)
                    target = target && os == QSysInfo::WV_WINDOWS7;
                if (id == 913)
                    target = target && os == QSysInfo::WV_WINDOWS8;
                if (id == 914 || id == 931)
                    target = target && os == QSysInfo::WV_WINDOWS8_1;
                if (id == 915 || id == 932)
                    target = target && os >= QSysInfo::WV_WINDOWS10;
                if (id == 950 || id == 960 || id == 980 || id == 990)
                    target = target && os >= QSysInfo::WV_VISTA;
                if (id == 965)
                    target = target && !is64;
                if (id == 966)
                    target = target && is64;
            }
            if (!target)
            {
                row.excluded = true;
                row.localPath.clear();
                row.reason = QStringLiteral("Does not target this Windows version, architecture, locale or "
                                            "portable-device manufacturer.");
            }
            if (id == 74)
            {
                QSettings net(
                    QStringLiteral(
                        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full"),
                    QSettings::NativeFormat);
                if (net.value(QStringLiteral("Release")).toInt() >= 461308)
                    row.installed = true;
            }
        }
        if (!includeOtherWindows && QSysInfo::windowsVersion() >= QSysInfo::WV_VISTA &&
            (row.name.contains(QStringLiteral(" XP"), Qt::CaseInsensitive) ||
             QUrl(row.url).fileName().contains(QStringLiteral("XP"), Qt::CaseInsensitive)))
        {
            row.excluded = true;
            row.localPath.clear();
            row.reason = QStringLiteral("Windows XP-specific package does not target this Windows version.");
        }
#endif
    }
    if (!includeOtherWindows)
        resolvePackageVariants(rows, QLocale::system().language() == QLocale::Russian);
    if (trace)
    {
        fprintf(stderr, "software: done\n");
        fflush(stderr);
    }
    return rows;
}
void resolvePackageVariants(QVector<Program> &programs, bool russian)
{
    int chosen = -1, best = -1;
    QVector<int> variants;
    for (int i = 0; i < programs.size(); ++i)
    {
        const auto &program = programs[i];
        const int id = program.id.toInt();
        if (!program.knownPackage || program.excluded ||
            (id != 3 && id != 4 && id != 5 && id != 203 && id != 204))
            continue;
        variants << i;
        const bool cis = id == 203 || id == 204;
        const int score = (!program.localPath.isEmpty() ? 100
                           : allowedUrl(program.url)    ? 10
                                                        : 0) +
                          (cis == russian ? 2 : 0);
        if (score > best)
        {
            best = score;
            chosen = i;
        }
    }
    for (int index : variants)
        if (index != chosen)
        {
            programs[index].excluded = true;
            programs[index].localPath.clear();
            programs[index].reason = QStringLiteral("Another compatible Opera package variant was selected.");
        }
}
QVector<Installed> installedPrograms()
{
    QVector<Installed> result;
#ifdef Q_OS_WIN
    QSet<QString> seen;
    const HKEY hives[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
    const REGSAM views[] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
    for (HKEY hive : hives)
        for (REGSAM view : views)
        {
            HKEY root = 0;
            if (RegOpenKeyExW(hive, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0,
                              KEY_READ | view, &root) != ERROR_SUCCESS)
                continue;
            for (DWORD i = 0; i < 6000; ++i)
            {
                WCHAR name[512];
                DWORD length = 512;
                const LONG code = RegEnumKeyExW(root, i, name, &length, 0, 0, 0, 0);
                if (code == ERROR_NO_MORE_ITEMS)
                    break;
                if (code != ERROR_SUCCESS)
                    continue;
                HKEY item = 0;
                if (RegOpenKeyExW(root, name, 0, KEY_QUERY_VALUE | view, &item) != ERROR_SUCCESS)
                    continue;
                auto value = [item](LPCWSTR key)
                {
                    WCHAR text[4096] = {};
                    DWORD bytes = sizeof(text), type = 0;
                    if (RegQueryValueExW(item, key, 0, &type, reinterpret_cast<LPBYTE>(text), &bytes) !=
                            ERROR_SUCCESS ||
                        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes >= sizeof(text))
                        return QString();
                    return QString::fromWCharArray(text);
                };
                Installed row;
                row.name = value(L"DisplayName");
                row.publisher = value(L"Publisher");
                row.version = value(L"DisplayVersion");
                RegCloseKey(item);
                const QString id =
                    (row.name + QLatin1Char('|') + row.version + QLatin1Char('|') + row.publisher)
                        .toCaseFolded();
                if (!row.name.isEmpty() && !seen.contains(id))
                {
                    seen.insert(id);
                    result.append(row);
                }
            }
            RegCloseKey(root);
        }
#endif
    return result;
}
int runReport(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        const auto args = app.arguments();
        auto value = [&](const QString &key)
        {
            const int at = args.indexOf(key);
            return at >= 0 ? args.value(at + 1) : QString();
        };
        const QString library = value(QStringLiteral("--library")),
                      output = value(QStringLiteral("--software-report"));
        if (library.isEmpty() || output.isEmpty())
            throw std::runtime_error("Required: --software-report output.json --library DriverPack-root");
        const auto programs = load(library, args.contains(QStringLiteral("--all-targets")), false);
        QJsonArray rows;
        int excluded = 0, local = 0;
        for (const auto &program : programs)
        {
            QJsonObject row;
            row.insert(QStringLiteral("source"), program.origin);
            row.insert(QStringLiteral("id"), program.id);
            row.insert(QStringLiteral("sourceId"), program.sourceId);
            row.insert(QStringLiteral("url"), program.url);
            row.insert(QStringLiteral("category"), program.category);
            row.insert(QStringLiteral("rating"), program.rating);
            row.insert(QStringLiteral("registry"), program.registry);
            row.insert(QStringLiteral("name"), program.name);
            row.insert(QStringLiteral("version"), program.version);
            row.insert(QStringLiteral("publisher"), program.publisher);
            row.insert(QStringLiteral("toolkit"), program.toolkit);
            row.insert(QStringLiteral("excluded"), program.excluded);
            row.insert(QStringLiteral("installed"), program.installed);
            row.insert(QStringLiteral("localPath"), program.localPath);
            row.insert(QStringLiteral("reason"), program.reason);
            rows.append(row);
            if (program.excluded)
                ++excluded;
            if (!program.localPath.isEmpty())
                ++local;
        }
        QJsonObject report;
        report.insert(QStringLiteral("programs"), rows);
        report.insert(QStringLiteral("sourceRecords"), programs.size());
        report.insert(QStringLiteral("excludedRecords"), excluded);
        report.insert(QStringLiteral("localPackages"), local);
        report.insert(QStringLiteral("javascriptEvaluated"), false);
        report.insert(QStringLiteral("installationRequested"), false);
        QSaveFile file(output);
        const QByteArray bytes = QJsonDocument(report).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            throw std::runtime_error("Cannot save the software report.");
        return 0;
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "Software catalog: %s\n", e.what());
        return 1;
    }
}

} // namespace DriverPackSoftware
