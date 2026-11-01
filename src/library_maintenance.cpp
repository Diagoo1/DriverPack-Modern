#include "library_maintenance.h"
#include "software_catalog.h"
#ifndef MAINTENANCE_CORE_ONLY
#include "appearance.h"
#include "theme.h"
#include "app_settings.h"
#include "ui_language.h"
#include "ui_font.h"
#include <QLocale>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QScopedPointer>
#include <QTimer>
#include <QThread>
#include <QCloseEvent>
#include <QVBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QApplication>
#endif
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QRegularExpression>
#include <stdexcept>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif
namespace LibraryMaintenance
{
static void ordinary(const QString &path)
{
    const QFileInfo info(path);
    if (info.isSymLink())
        throw std::runtime_error("Links are not allowed in cleanup paths.");
#ifdef Q_OS_WIN
    const DWORD flags = GetFileAttributesW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()));
    if (flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Junctions and reparse points are not allowed in cleanup paths.");
#endif
}
static QString rootPath(const QString &library)
{
    const QString absolute = QFileInfo(library).absoluteFilePath();
    QString ancestor = absolute;
    while (!ancestor.isEmpty())
    {
        ordinary(ancestor);
        const QString up = QFileInfo(ancestor).dir().absolutePath();
        if (up == ancestor)
            break;
        ancestor = up;
    }
    const QString root = QFileInfo(absolute).canonicalFilePath();
    if (root.isEmpty() || QDir(root).isRoot() ||
        !QFileInfo(QDir(root).filePath(QStringLiteral("index/db.sqlite"))).isFile() ||
        !QFileInfo(QDir(root).filePath(QStringLiteral("drivers"))).isDir())
        throw std::runtime_error(
            "Choose a DriverPack library containing drivers and index/db.sqlite; drive roots are refused.");
    return root;
}
static QStringList files(const QString &path)
{
    ordinary(path);
    QStringList result;
    const QFileInfo info(path);
    if (!info.exists())
        return result;
    if (info.isFile())
    {
        result << path;
        return result;
    }
    for (const auto &entry :
         QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        result << files(entry.absoluteFilePath());
    return result;
}
static QByteArray digest(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Cannot verify a preserved file.");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd())
    {
        const QByteArray block = file.read(256 * 1024);
        if (block.isEmpty() && file.error() != QFile::NoError)
            throw std::runtime_error("File verification read failed.");
        hash.addData(block);
    }
    return hash.result();
}
static void preserve(const QString &source, const QString &target)
{
    ordinary(source);
    ordinary(target);
    ordinary(QFileInfo(target).dir().absolutePath());
    if (!QDir().mkpath(QFileInfo(target).dir().absolutePath()) ||
        (!QFileInfo(target).exists() && !QFile::copy(source, target)) || digest(source) != digest(target))
        throw std::runtime_error("A required icon or legal notice could not be preserved. Cleanup stopped.");
}
static bool retained(const QString &relative)
{
    const QString path = relative.toLower(), name = QFileInfo(path).fileName(),
                  suffix = QFileInfo(path).suffix();
    if (path == QStringLiteral("driverpackmodern.exe") || path == QStringLiteral("clean driverpack.cmd"))
        return true;
    if (path.startsWith(QStringLiteral("licenses/")))
        return path == QStringLiteral("licenses/originaldriverpack-notices.txt");
    if (path.startsWith(QStringLiteral("logs/")))
        return QStringList({QStringLiteral("log"), QStringLiteral("jsonl"), QStringLiteral("txt"),
                            QStringLiteral("dmp")})
            .contains(suffix);
    if (path.startsWith(QStringLiteral("index/")))
        return path == QStringLiteral("index/db.sqlite") || path == QStringLiteral("index/db.sqlite-wal") ||
               path == QStringLiteral("index/db.sqlite-shm") ||
               path == QStringLiteral("index/db.sqlite-journal");
    if (path.startsWith(QStringLiteral("drivers/.driverpack-modern-downloads/")))
        return true;
    if (path.startsWith(QStringLiteral("drivers/")))
        return QStringList({QStringLiteral("7z"), QStringLiteral("zip"), QStringLiteral("cab"),
                            QStringLiteral("bin")})
            .contains(suffix);
    if (path.startsWith(QStringLiteral("programs/")))
    {
        if (path == QStringLiteral("programs/catalog.json"))
            return true;
        if (DriverPackSoftware::promotionalPackage(name))
            return false;
        if (path.startsWith(QStringLiteral("programs/icons/")))
            return QStringList({QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                QStringLiteral("ico"), QStringLiteral("svg")})
                .contains(suffix);
        return QStringList({QStringLiteral("exe"), QStringLiteral("msi"), QStringLiteral("msp"),
                            QStringLiteral("dll"), QStringLiteral("dat"), QStringLiteral("bin"),
                            QStringLiteral("ini"), QStringLiteral("cab"), QStringLiteral("7z"),
                            QStringLiteral("zip")})
            .contains(suffix);
    }
    return !path.contains(QLatin1Char('/')) &&
           (name.startsWith(QStringLiteral("license")) || name.startsWith(QStringLiteral("licence")) ||
            name.startsWith(QStringLiteral("copyright")) || name.startsWith(QStringLiteral("copying")) ||
            name.startsWith(QStringLiteral("notice")));
}
static QStringList removals(const QString &root)
{
    QStringList result;
    QSet<QString> requiredIcons;
    for (const auto &row : DriverPackSoftware::catalogRecords(root, false))
    {
        const QString icon = DriverPackSoftware::iconPath(root, row);
        if (icon.startsWith(root + QLatin1Char('/')))
            requiredIcons.insert(QDir(root).relativeFilePath(icon).toLower());
    }
    for (const QString &path : files(root))
    {
        const QString relative = QDir(root).relativeFilePath(path);
        if (!retained(relative) && !requiredIcons.contains(relative.toLower()))
            result << relative;
    }
    return result;
}
static void emptyFolders(const QString &path, const QString &root)
{
    ordinary(path);
    for (const auto &entry :
         QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        emptyFolders(entry.absoluteFilePath(), root);
    if (path != root &&
        QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty())
        QDir().rmdir(path);
}
QStringList preview(const QString &library)
{
    const QString root = rootPath(library);
    const auto catalog = DriverPackSoftware::catalogRecords(root, false);
    Q_UNUSED(catalog);
    return removals(root);
}
static void moveFile(const QString &source, const QString &target)
{
    ordinary(source);
    ordinary(QFileInfo(target).dir().absolutePath());
    ordinary(target);
    if (QFileInfo(target).exists())
        throw std::runtime_error("Destination already exists; no installer will be overwritten.");
    if (!QDir().mkpath(QFileInfo(target).dir().absolutePath()) || !QFile::rename(source, target))
        throw std::runtime_error(
            "Could not move a library file. Cleanup stopped before removing the legacy interface.");
}
static void erase(const QString &path)
{
    ordinary(path);
    const QFileInfo info(path);
    if (!info.exists())
        return;
    if (info.isDir())
    {
        for (const auto &entry :
             QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
            erase(entry.absoluteFilePath());
        if (!QDir().rmdir(path))
            throw std::runtime_error(
                QStringLiteral("Windows could not remove folder: %1").arg(path).toUtf8().constData());
    }
    else if (!QFile::remove(path))
        throw std::runtime_error(
            QStringLiteral("Windows could not remove file: %1").arg(path).toUtf8().constData());
}
void clean(const QString &library)
{
    const QString root = rootPath(library);
    preview(root);
#ifdef Q_OS_WIN
#ifndef MAINTENANCE_CORE_ONLY
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot check whether the original DriverPack is closed.");
    PROCESSENTRY32W process = {};
    process.dwSize = sizeof(process);
    bool open = false;
    if (Process32FirstW(snapshot, &process))
        do
        {
            if (QString::fromWCharArray(process.szExeFile)
                    .compare(QStringLiteral("DriverPack.exe"), Qt::CaseInsensitive) == 0)
                open = true;
        } while (Process32NextW(snapshot, &process));
    CloseHandle(snapshot);
    if (open)
        throw std::runtime_error("Close the original DriverPack.exe before cleaning its library.");
#endif
#endif
    auto rows = DriverPackSoftware::catalogRecords(root, false);
    QVector<QByteArray> available;
    for (const auto &row : rows)
    {
        const QString path = DriverPackSoftware::localProgram(root, row, true);
        available << (path.isEmpty() ? QByteArray() : digest(path));
    }
    // Save legal notices and referenced icons before deleting their legacy containers.
    for (const QString &folder : {QStringLiteral("bin"), QStringLiteral("catalog")})
        for (const QString &source : files(QDir(root).filePath(folder)))
        {
            const QString name = QFileInfo(source).fileName().toLower();
            if (!(name.startsWith(QStringLiteral("license")) || name.startsWith(QStringLiteral("licence")) ||
                  name.startsWith(QStringLiteral("copying")) ||
                  name.startsWith(QStringLiteral("copyright")) || name.startsWith(QStringLiteral("notice")) ||
                  name == QStringLiteral("authors")))
                continue;
            const QString key = QString::fromLatin1(
                QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
            const QString target = QDir(root).filePath(QStringLiteral("licenses/OriginalDriverPack/") + key +
                                                       QLatin1Char('-') + QFileInfo(source).fileName());
            preserve(source, target);
        }
    const QString notices = QDir(root).filePath(QStringLiteral("licenses/OriginalDriverPack-NOTICES.txt"));
    QByteArray legalBytes;
    QFile previous(notices);
    if (previous.exists())
    {
        if (previous.size() > 32 * 1024 * 1024 || !previous.open(QIODevice::ReadOnly))
            throw std::runtime_error("Cannot preserve existing legal notices.");
        legalBytes = previous.readAll();
        if (previous.error() != QFile::NoError)
            throw std::runtime_error("Cannot read existing legal notices.");
        previous.close();
    }
    QSet<QByteArray> recorded;
    auto hashes = QRegularExpression(QStringLiteral("SHA256 ([0-9a-f]{64})"))
                      .globalMatch(QString::fromLatin1(legalBytes));
    while (hashes.hasNext())
        recorded.insert(hashes.next().captured(1).toLatin1());
    for (const QString &source : files(QDir(root).filePath(QStringLiteral("licenses"))))
    {
        if (QFileInfo(source).absoluteFilePath() == QFileInfo(notices).absoluteFilePath())
            continue;
        QFile input(source);
        if (input.size() > 8 * 1024 * 1024 || legalBytes.size() + input.size() > 32 * 1024 * 1024 ||
            !input.open(QIODevice::ReadOnly))
            throw std::runtime_error("Cannot consolidate legal notices.");
        const QByteArray bytes = input.readAll();
        if (input.error() != QFile::NoError)
            throw std::runtime_error("Legal notice read failed.");
        const QByteArray hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
        if (recorded.contains(hash))
            continue;
        recorded.insert(hash);
        legalBytes += QByteArray("\n----- ") + QDir(root).relativeFilePath(source).toUtf8() +
                      QByteArray(" | SHA256 ") + hash + QByteArray(" -----\n") + bytes + QByteArray("\n");
    }
    if (!legalBytes.isEmpty())
    {
        ordinary(notices);
        QDir().mkpath(QFileInfo(notices).dir().absolutePath());
        QSaveFile output(notices);
        if (!output.open(QIODevice::WriteOnly) || output.write(legalBytes) != legalBytes.size() ||
            !output.commit() ||
            digest(notices) != QCryptographicHash::hash(legalBytes, QCryptographicHash::Sha256))
            throw std::runtime_error("Legal notice consolidation failed; originals were not removed.");
    }
    for (auto &row : rows)
    {
        const QString icon = DriverPackSoftware::iconPath(root, row);
        if (icon.startsWith(root + QLatin1Char('/')) &&
            (icon.contains(QStringLiteral("/bin/")) || icon.contains(QStringLiteral("/catalog/"))))
        {
            const QString name =
                QString::fromLatin1(
                    QCryptographicHash::hash(icon.toUtf8(), QCryptographicHash::Sha256).toHex().left(16)) +
                QLatin1Char('-') + QFileInfo(icon).fileName();
            row.icon = QStringLiteral("programs/icons/") + name;
            const QString target = QDir(root).filePath(row.icon);
            preserve(icon, target);
        }
    }
    const QString programs = QDir(root).filePath(QStringLiteral("programs"));
    QDir().mkpath(programs);
    const QString legacySource = QDir(root).filePath(QStringLiteral("bin/programs"));
    if (QFileInfo(legacySource).exists())
    {
        const QString legacyTarget = QDir(programs).filePath(QStringLiteral("Legacy"));
        if (QFileInfo(legacyTarget).exists() || !QDir().rename(legacySource, legacyTarget))
            throw std::runtime_error(
                "Cannot preserve the original installer bundle without overwriting an existing folder.");
    }
    auto categories = rows;
    QFile embedded(QStringLiteral(":/driverpack/software-base.json"));
    if (embedded.open(QIODevice::ReadOnly))
        categories += DriverPackSoftware::parseJsonCatalog(embedded.readAll());
    auto categoryFor = [&](const QString &name)
    {
        QString normal = name.toLower();
        normal.remove(QRegularExpression(QStringLiteral("_light(?=\\.exe$)")));
        for (const auto &row : categories)
        {
            const QString candidate =
                (row.fileName.isEmpty() ? QUrl(row.url).fileName() : row.fileName).toLower();
            if (normal == candidate)
                return DriverPackSoftware::categoryFolder(row.category);
        }
        if (normal.startsWith(QStringLiteral("avast")) || normal.startsWith(QStringLiteral("360ts")))
            return QStringLiteral("Security");
        if (normal.startsWith(QStringLiteral("7-zip")))
            return QStringLiteral("Tools");
        if (normal.startsWith(QStringLiteral("flashplayer")))
            return QStringLiteral("Media");
        return QStringLiteral("Other");
    };
    // Process ordinary folders first, then retained legacy bundle copies.
    QStringList sources = files(programs);
    sources += files(QDir(root).filePath(QStringLiteral("bin/programs")));
    std::stable_sort(sources.begin(), sources.end(),
                     [](const QString &a, const QString &b)
                     {
                         const bool al = a.contains(QStringLiteral("/Legacy/")) ||
                                         a.contains(QStringLiteral("/bin/programs/"));
                         const bool bl = b.contains(QStringLiteral("/Legacy/")) ||
                                         b.contains(QStringLiteral("/bin/programs/"));
                         return al < bl;
                     });
    QStringList duplicates;
    for (const QString &source : sources)
    {
        const QFileInfo file(source);
        if (file.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0 ||
            DriverPackSoftware::promotionalPackage(file.fileName()))
            continue;
        const QString category = categoryFor(file.fileName());
        const bool legacy =
            source.contains(QStringLiteral("/Legacy/")) || source.contains(QStringLiteral("/bin/programs/"));
        if (legacy && category == QStringLiteral("Other"))
        {
            if (source.contains(QStringLiteral("/bin/programs/")))
            {
                const QString relative =
                    QDir(QDir(root).filePath(QStringLiteral("bin/programs"))).relativeFilePath(source);
                moveFile(source, QDir(programs).filePath(QStringLiteral("Legacy/") + relative));
            }
            continue;
        }
        QString target = QDir(programs).filePath(category + QLatin1Char('/') + file.fileName());
        if (QFileInfo(target).absoluteFilePath() == file.absoluteFilePath())
            continue;
        if (QFileInfo(target).exists())
        {
            if (QFileInfo(target).size() == file.size() && digest(source) == digest(target))
            {
                duplicates << source;
                continue;
            }
            if (!legacy)
                throw std::runtime_error(
                    "Two different installers have the same destination. Neither was overwritten.");
            bool identical = false;
            for (const auto &other :
                 QDir(QFileInfo(target).dir())
                     .entryInfoList(QStringList() << QStringLiteral("*.exe"), QDir::Files))
                if (other.size() == file.size() && digest(other.absoluteFilePath()) == digest(source))
                {
                    identical = true;
                    break;
                }
            if (identical)
            {
                duplicates << source;
                continue;
            }
            target = QDir(programs).filePath(
                category + QLatin1Char('/') + file.completeBaseName() + QLatin1Char('-') +
                QString::fromLatin1(digest(source).toHex().left(10)) + QStringLiteral(".exe"));
            if (QFileInfo(target).exists() && digest(target) == digest(source))
            {
                duplicates << source;
                continue;
            }
        }
        moveFile(source, target);
        const QString companion = QFileInfo(source).dir().filePath(QStringLiteral("PotPlayerMini.ini"));
        if (file.fileName().compare(QStringLiteral("PotPlayer.exe"), Qt::CaseInsensitive) == 0 &&
            QFileInfo(companion).exists())
            moveFile(companion, QFileInfo(target).dir().filePath(QStringLiteral("PotPlayerMini.ini")));
    }
    DriverPackSoftware::writeCatalog(root, rows);
    for (int i = 0; i < rows.size(); ++i)
        if (!available[i].isEmpty())
        {
            const QString path = DriverPackSoftware::localProgram(root, rows[i], true);
            if (path.isEmpty() || digest(path) != available[i])
                throw std::runtime_error("Installer verification failed; legacy deletion was not started.");
        }
    for (const QString &name : removals(root))
        erase(QDir(root).filePath(name));
    for (const QString &duplicate : duplicates)
        if (QFileInfo(duplicate).exists())
            erase(duplicate);
    emptyFolders(root, root);
}
#ifndef MAINTENANCE_CORE_ONLY
class CleanupPanel : public RoundedDialog
{
  public:
    bool busy = false;
    bool failed = false;
    void reject() Q_DECL_OVERRIDE
    {
        if (!busy)
            RoundedDialog::reject();
    }

  protected:
    void closeEvent(QCloseEvent *event) Q_DECL_OVERRIDE
    {
        if (busy)
            event->ignore();
        else
            RoundedDialog::closeEvent(event);
    }
};
class CleanupJob : public QThread
{
  public:
    QString library, error;
    void run() Q_DECL_OVERRIDE
    {
        try
        {
            clean(library);
        }
        catch (const std::exception &e)
        {
            error = QString::fromUtf8(e.what());
        }
    }
};
static QString cleanupLanguage()
{
    const QString forced = QString::fromUtf8(qgetenv("DRIVERPACK_CLEANUP_LANGUAGE"));
    for (const auto &locale : UiLanguage::available())
        if (locale.first == forced)
            return forced;
    QFile file(QStringLiteral(":/sdio/i18n/index.json"));
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("english");
    const auto locales =
        QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("locales")).toArray();
    const QStringList preferences = QLocale::system().uiLanguages();
    for (const QString &wanted : preferences)
    {
        const QString tag = wanted.toLower();
        if (tag.startsWith(QStringLiteral("zh")))
            return tag.contains(QStringLiteral("hant")) || tag.endsWith(QStringLiteral("tw")) ||
                           tag.endsWith(QStringLiteral("hk")) || tag.endsWith(QStringLiteral("mo"))
                       ? QStringLiteral("chinese_tw")
                       : QStringLiteral("chinese_cn");
        if (tag.startsWith(QStringLiteral("pt")))
            return tag.endsWith(QStringLiteral("br")) ? QStringLiteral("brazilian")
                                                      : QStringLiteral("portuguese");
        if (tag.startsWith(QStringLiteral("nb")) || tag.startsWith(QStringLiteral("nn")))
            return QStringLiteral("norwegian");

        for (const auto &value : locales)
        {
            const auto item = value.toObject();
            if (item.value(QStringLiteral("tag")).toString().compare(wanted, Qt::CaseInsensitive) == 0)
                return item.value(QStringLiteral("id")).toString();
        }
        for (const auto &value : locales)
        {
            const auto item = value.toObject();
            if (item.value(QStringLiteral("tag"))
                    .toString()
                    .section(QLatin1Char('-'), 0, 0)
                    .compare(wanted.section(QLatin1Char('-'), 0, 0), Qt::CaseInsensitive) == 0)
                return item.value(QStringLiteral("id")).toString();
        }
    }
    return QStringLiteral("english");
}
int dialog(const QString &library)
{
    try
    {
        UiLanguage::setLanguage(cleanupLanguage());
        auto tr = [](const char *value) { return UiLanguage::text(QString::fromUtf8(value)); };
        const QStringList paths = preview(library);
        CleanupPanel panel;
        panel.setWindowTitle(tr("Clean library"));
        panel.resize(720, 560);
        QFont bilingual = panel.font();
        bilingual.setFamily(UiFont::family());
        panel.setFont(bilingual);
        QScopedPointer<QSettings> settings(
            createAppSettings(QCoreApplication::applicationDirPath(), 0, false));
        const QString mode =
            settings->value(QStringLiteral("theme/mode"), QStringLiteral("system")).toString();
        bool dark = mode == QStringLiteral("dark");
#ifdef Q_OS_WIN
        if (mode == QStringLiteral("system") && QSysInfo::windowsVersion() >= QSysInfo::WV_WINDOWS10)
        {
            QSettings system(
                QStringLiteral(
                    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                QSettings::NativeFormat);
            dark = system.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
        }
#endif
        panel.setProperty("darkMode", dark);
        panel.setStyleSheet(
            Theme::styleSheet(dark) +
            QStringLiteral(
                "QLabel{color:%1;background:transparent;font-size:14px;}"
                "QPushButton{background:%2;color:%1;border:1px solid %3;border-radius:12px;"
                "min-height:40px;padding:0 18px;} QPushButton:hover{background:%4;border-color:#00aaa9;}"
                "QPushButton:pressed{background:#147c91;color:white;}"
                "QPushButton#cleanAction:enabled{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 "
                "#08799b,stop:1 #00aaa0);color:white;border:0;}"
                "QPushButton#cleanAction:hover:enabled{background:#079b9d;}"
                "QPushButton:disabled{color:%5;background:%2;}")
                .arg(Theme::color("text", dark).name(), Theme::color("surface", dark).name(),
                     Theme::color("border", dark).name(), Theme::color("elevated", dark).name(),
                     Theme::color("muted", dark).name()));
        auto frame = new QVBoxLayout(&panel);
        frame->setContentsMargins(0, 0, 0, 0);
        frame->setSpacing(0);
        frame->addWidget(
            new DialogHeader(tr("Clean library"), QStringLiteral("shield"), &panel, UiLanguage::isRtl()));
        auto body = new QWidget(&panel);
        auto layout = new QVBoxLayout(body);
        layout->setContentsMargins(24, 20, 24, 24);
        layout->setSpacing(12);
        frame->addWidget(body, 1);
        auto text = new QLabel(tr("Files not in the keep list will be permanently deleted. The original "
                                  "DriverPack will no longer work."));
        text->setWordWrap(true);
        text->setAlignment((UiLanguage::isRtl() ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignTop);
        layout->addWidget(text);
        auto list = new QTextEdit;
        list->setReadOnly(true);
        list->setProperty("skipTranslation", true);
        list->setPlainText(
            tr("Keep") +
            QStringLiteral(
                ":\nDriverPackModern.exe\nClean "
                "DriverPack.cmd\ndrivers/*.{7z,zip,cab,bin}\ndrivers/.driverpack-modern-downloads/**\nindex/"
                "db.sqlite{,-wal,-shm,-journal}\nprograms/"
                "catalog.json\nprograms/**/*.{exe,msi,msp,dll,dat,bin,ini,cab,7z,zip}\nprograms/icons/"
                "*.{png,jpg,jpeg,ico,svg}\nlicenses/OriginalDriverPack-NOTICES.txt\nlogs/"
                "*.{log,jsonl,txt,dmp}\n\n") +
            tr("Delete") + QStringLiteral(":\n") + paths.join(QLatin1Char('\n')));
        layout->addWidget(list, 1);
        auto accept = new QCheckBox(tr("I understand. DriverPack is closed."));
        layout->addWidget(accept);
        auto buttons = new QHBoxLayout;
        auto cancel = new QPushButton(tr("Cancel"));
        auto run = new QPushButton(tr("Clean library"));
        run->setObjectName(QStringLiteral("cleanAction"));
        cancel->setCursor(Qt::PointingHandCursor);
        run->setCursor(Qt::PointingHandCursor);
        run->setEnabled(false);
        buttons->addWidget(cancel);
        buttons->addWidget(run);
        layout->addLayout(buttons);
        QObject::connect(accept, &QCheckBox::toggled, run, &QPushButton::setEnabled);
        QObject::connect(cancel, &QPushButton::clicked, &panel, &QDialog::reject);
        QObject::connect(run, &QPushButton::clicked, &panel,
                         [&]()
                         {
                             panel.busy = true;
                             run->setEnabled(false);
                             accept->setEnabled(false);
                             cancel->setEnabled(false);
                             list->setPlainText(tr("Please wait"));
                             auto job = new CleanupJob;
                             job->library = library;
                             QObject::connect(job, &QThread::finished, &panel,
                                              [=, &panel]()
                                              {
                                                  panel.busy = false;
                        panel.failed = !job->error.isEmpty();
                                                  list->setPlainText(job->error.isEmpty()
                                                                         ? tr("Cleanup completed")
                                                                         : job->error);
                                                  cancel->setText(tr("Close"));
                                                  cancel->setEnabled(true);
                                                  job->deleteLater();
                                              });
                             job->start();
                         });
        const QString capture = QString::fromLocal8Bit(qgetenv("DRIVERPACK_CLEANUP_CAPTURE"));
        if (!capture.isEmpty())
            QTimer::singleShot(500, &panel,
                               [&]()
                               {
                                   panel.grab().save(capture);
                                   QJsonObject proof;
                                   proof.insert(QStringLiteral("language"), UiLanguage::language());
                                   proof.insert(QStringLiteral("rtl"), UiLanguage::isRtl());
                                   proof.insert(QStringLiteral("warning"), text->text());
                                   proof.insert(QStringLiteral("deleteCount"), paths.size());
                                   proof.insert(QStringLiteral("databaseMarkedForDeletion"),
                                                paths.contains(QStringLiteral("index/db.sqlite")));
                                   proof.insert(QStringLiteral("cleanupExecuted"), false);
                                   QFile report(capture + QStringLiteral(".json"));
                                   if (report.open(QIODevice::WriteOnly))
                                       report.write(QJsonDocument(proof).toJson());
                                   panel.reject();
                               });
        panel.exec();
        return panel.failed ? 1 : 0;
    }
    catch (const std::exception &e)
    {
        projectMessage(0, QStringLiteral("Cleanup stopped"), QString::fromUtf8(e.what()), false, false);
        return 1;
    }
}
#endif
} // namespace LibraryMaintenance
