#include <QApplication>
#include <cstdio>
#include <QCoreApplication>
#include <QFont>
#include <QTimer>
#include <QTextCodec>
#include <QDesktopWidget>
#include <QSettings>
#include <QDir>
#include "driverpack_shell.h"
#include "ui_font.h"
#include "ui_scale.h"
#include "ui_language.h"
#include "ui_elements.h"
#include "device_types.h"
#include "scan_report.h"
#include "online_packages.h"
#include "software_catalog.h"
#include "library_maintenance.h"
#include "hardware_report.h"
#include "automation_plan.h"
#include "single_instance.h"
#include "appearance.h"
#include "app_settings.h"
#include "app_update.h"

#include <QScopedPointer>
#include <QPointer>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPropertyAnimation>
#include <QSharedPointer>
#include <QElapsedTimer>
#include <QLabel>
#include <cstring>
#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
struct GuiComScope
{
    HRESULT result;
    GuiComScope() : result(CoInitializeEx(0, COINIT_APARTMENTTHREADED)) {}
    ~GuiComScope()
    {
        if (SUCCEEDED(result))
            CoUninitialize();
    }
};
#endif

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--hardware-report") == 0)
            return runHardwareReport(argc, argv);
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--apply-update") == 0)
            return AppUpdate::apply(argc, argv);
    bool report = false, online = false, automation = false, software = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--version") == 0)
        {
            fprintf(stdout, "DriverPack Modern 0.22.0 Beta | UI by Tarek Sadek\n");
            return 0;
        }
        if (std::strcmp(argv[i], "--scan-report") == 0)
            report = true;
        if (std::strcmp(argv[i], "--online-report") == 0)
            online = true;
        if (std::strcmp(argv[i], "--software-report") == 0)
            software = true;
        if (std::strcmp(argv[i], "--automation-plan") == 0)
            automation = true;
    }
    for (int i = 1; i < argc; ++i)
        if (std::strncmp(argv[i], "-script", 7) == 0 || std::strcmp(argv[i], "--reindex") == 0 ||
            std::strcmp(argv[i], "--load-snapshot") == 0)
        {
            fprintf(stderr,
                    "DriverPack Modern does not support SDI scripts, .snp snapshots or .bin rebuilding.\n");
            return 2;
        }
    if (!UiScale::restartAfterParent(argc, argv))
        return 4;
    SingleInstance instance;
    if (report || online || automation || software)
    {
        if (instance.alreadyRunning() || !instance.available())
            return 4;
        return software     ? DriverPackSoftware::runReport(argc, argv)
               : automation ? runAutomationReport(argc, argv)
               : online     ? OnlinePackages::runReport(argc, argv)
                            : runScanReport(argc, argv);
    }
    // Device-independent Qt layout and sharp SVG rasterization for HiDPI Windows.
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    UiScale::prepareEnvironment();
#ifdef Q_OS_WIN
    GuiComScope guiCom;
#ifdef DRIVERPACK_CATALOG_LINKED
    const std::uint32_t restoreCallbacks = sdiomodern::initializeRestoreCallbacks();
#endif
#endif
    QApplication app(argc, argv);
#if defined(Q_OS_WIN) && defined(DRIVERPACK_CATALOG_LINKED)
    if (app.arguments().contains(QStringLiteral("--gui-security-probe")))
    {
        fprintf(stdout, "{\"guiCom\":%lu,\"restoreCallbacks\":%lu,\"installationRequested\":false}\n",
                static_cast<unsigned long>(guiCom.result), static_cast<unsigned long>(restoreCallbacks));
        return FAILED(guiCom.result) || restoreCallbacks ? 1 : 0;
    }
#endif
    // Verifiable release identity: scripts can reject stale executables.
    if (app.arguments().contains(QStringLiteral("--version")))
    {
        fprintf(stdout, "DriverPack Modern 0.22.0 Beta | UI by Tarek Sadek\n");
        fflush(stdout);
        return 0;
    }
    app.setApplicationName(QStringLiteral("DriverPack Modern"));
    app.setWindowIcon(QIcon(QStringLiteral(":/sdio/assets/driverpack-emblem.png")));
    app.setOrganizationName(QStringLiteral("TarekSadek"));
    app.setQuitOnLastWindowClosed(true);
    UiFont::applyLanguage(QStringLiteral("english"));
    installInterfaceMotion();

    if (instance.alreadyRunning() || !instance.available())
    {
        QScopedPointer<QSettings> settings(createAppSettings(app.applicationDirPath(), 0, false));
        const QString language =
            settings->value(QStringLiteral("language"), QStringLiteral("english")).toString();
        const bool arabic = language == QStringLiteral("arabic") || language == QStringLiteral("ar");
        UiLanguage::setLanguage(arabic ? QStringLiteral("arabic") : language);
        instance.activateExisting();
        projectMessage(
            0, arabic ? QString::fromUtf8("البرنامج مفتوح بالفعل") : QStringLiteral("Already running"),
            instance.alreadyRunning()
                ? (arabic ? QString::fromUtf8("DriverPack Modern مفتوح بالفعل. ارجع للنافذة الحالية لمتابعة "
                                              "العمل؛ لا يمكن تشغيل نسختين في نفس الوقت.")
                          : QStringLiteral("DriverPack Modern is already open. Return to the existing window "
                                           "to continue; a second session cannot be started."))
                : (arabic
                       ? QString::fromUtf8("تعذّر إنشاء جلسة تشغيل آمنة. أغلق النسخة الحالية وأعد المحاولة.")
                       : QStringLiteral("Cannot reserve an application session. Close the existing copy and "
                                        "try again.")),
            false, arabic);
        return instance.alreadyRunning() ? 0 : 4;
    }
    qRegisterMetaType<QVector<DeviceInfo>>("QVector<DeviceInfo>");
    const QStringList args = app.arguments();
    const int cleanup=args.indexOf(QStringLiteral("--cleanup-library"));
    if(cleanup>=0 && cleanup+1<args.size())return LibraryMaintenance::dialog(args[cleanup+1]);
    const int gallery=args.indexOf(QStringLiteral("--capture-gallery"));
    const bool demo = args.contains(QStringLiteral("--demo"))||gallery>=0;
    const bool noSplash = args.contains(QStringLiteral("--no-splash"));

    const int liveCapture = args.indexOf(QStringLiteral("--capture-live"));
    const int previewCapture = args.indexOf(QStringLiteral("--capture-preview"));
    const int capture = liveCapture >= 0 ? liveCapture : previewCapture;
    DriverPackShell window(demo, capture >= 0||gallery>=0);
    instance.registerWindow(&window);
    const QRect usable = QApplication::desktop()->availableGeometry(&window);
    window.resize(qMin(640, usable.width() - 24), qMin(440, usable.height() - 24));
    window.move(usable.center() - window.rect().center());
    if(gallery>=0&&gallery+1<args.size())return window.gallery(args[gallery+1]);
    if (capture >= 0 && capture + 1 < args.size())
        return window.capture(args[capture + 1]);
    if (noSplash)
    {
        window.show();
        QTimer::singleShot(0, &window, [&window]() { window.beginScan(); });
    }
    else
    {
        auto splash = new Splash();
        QPointer<Splash> guard = splash;
        const QString probe = QString::fromUtf8(qgetenv("DRIVERPACK_MODERN_STARTUP_REPORT"));
        const bool probing = !probe.isEmpty() && !qgetenv("DRIVERPACK_MODERN_TEST_SETTINGS").isEmpty();
        if (probing)
        {
            window.setAttribute(Qt::WA_DontShowOnScreen);
            splash->setAttribute(Qt::WA_DontShowOnScreen);
            QTimer::singleShot(30000, &app, [&app]() { app.exit(72); });
        }
        auto startupClock = QSharedPointer<QElapsedTimer>::create();
        startupClock->start();
        auto opening = QSharedPointer<bool>::create(false);
        QObject::connect(
            &window, &DriverPackShell::workspaceReady, &window,
            [&window, guard, probing, probe, startupClock, opening, &app]()
            {
                if (*opening)
                    return;
                *opening = true;
                QTimer::singleShot(
                    qMax(0, 1600 - int(startupClock->elapsed())), &window,
                    [&window, guard, probing, probe, startupClock, &app]()
                    {
                        auto completion = new QPropertyAnimation(guard.data(), "progress", &window);
                        completion->setDuration(300);
                        completion->setStartValue(guard ? guard->progress() : 88);
                        completion->setEndValue(100);
                        completion->setEasingCurve(QEasingCurve::OutCubic);
                        QObject::connect(
                            completion, &QPropertyAnimation::finished, &window,
                            [&window, guard, probing, probe, startupClock, &app]()
                            {
                                window.show();
                                if (probing)
                                {
                                    QJsonObject result;
                                    result.insert(QStringLiteral("workspaceReady"),
                                                  window.findChild<QWidget *>(QStringLiteral("welcomeCard")) !=
                                                      0);
                                    result.insert(
                                        QStringLiteral("titlebarHeight"),
                                        window.findChild<QWidget *>(QStringLiteral("customTitleBar"))
                                            ->height());
                                    result.insert(QStringLiteral("applicationIconAvailable"),
                                                  !window.windowIcon().isNull());
                                    result.insert(QStringLiteral("splashWidth"), guard ? guard->width() : 0);
                                    result.insert(QStringLiteral("splashHeight"),
                                                  guard ? guard->height() : 0);
                                    result.insert(QStringLiteral("startupVisibleMs"),
                                                  double(startupClock->elapsed()));
                                    result.insert(QStringLiteral("splashProgress"),
                                                  guard ? guard->progress() : 0);
                                    result.insert(QStringLiteral("installationRequested"), false);
                                    QSaveFile file(probe);
                                    if (file.open(QIODevice::WriteOnly))
                                    {
                                        file.write(QJsonDocument(result).toJson());
                                        file.commit();
                                    }
                                    QTimer::singleShot(
                                        0, &app,
                                        [&app, result]()
                                        {
                                            app.exit(result.value(QStringLiteral("workspaceReady")).toBool()
                                                         ? 0
                                                         : 73);
                                        });
                                }
                                if (guard)
                                {
                                    guard->close();
                                    guard->deleteLater();
                                }
                            });
                        completion->start(QAbstractAnimation::DeleteWhenStopped);
                    });
            });
        splash->begin();
        QTimer::singleShot(0, &window, [&window]() { window.beginScan(); });
    }

    return app.exec();
}
