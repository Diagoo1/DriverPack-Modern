// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DRIVERPACK_NATIVE_SHELL_H
#define DRIVERPACK_NATIVE_SHELL_H
#include "driver_catalog.h"
#include "software_catalog.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QWidget>
class EngineBridge;
class ToolsDrawer;
class QSettings;
class QStackedWidget;
class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QAbstractButton;
class QProgressBar;
class QSlider;
class QCheckBox;
class QTimer;
class QScrollArea;
class QTreeWidget;
class QComboBox;
class HardwareWorker;
class QCloseEvent;
class QResizeEvent;
class QDialog;
class DrpTransferDialog;
class DrpSoftwareFetch;
class QTreeWidgetItem;
class DriverPackShell : public QWidget
{
    Q_OBJECT
  public:
    explicit DriverPackShell(bool demo = false, bool capture = false, QWidget *parent = 0);
    ~DriverPackShell();
    void beginScan();
    int capture(const QString &folder);
    int gallery(const QString &folder);

  signals:
    void workspaceReady();

  protected:
    void resizeEvent(QResizeEvent *) Q_DECL_OVERRIDE;
    void closeEvent(QCloseEvent *) Q_DECL_OVERRIDE;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &, void *, long *) Q_DECL_OVERRIDE;
#endif
  private:
    QString t(const char *, const char *) const;
    void rebuild();
    void appearance();
    void expert(int page);
    void startPage();
    void buildDrivers(QWidget *);
    void buildSoftware(QWidget *, bool security = false);
    void addProgramDialog(bool security = false, int editIndex = -1);
    void softwareSection(QVBoxLayout *, const QString &, const QVector<int> &);
    void buildProtection(QWidget *);
    void buildDiagnostics(QWidget *);
    void buildOffline(QWidget *);
    void buildSettings(QWidget *);
    void buildAbout(QWidget *);
    void installationPage();
    void finalPage();
    void presentInstallationResult();
    bool confirmWithoutRestore(const QString &captureFile=QString());
    void loadSoftware();
    void logDialog();
    void updateCounts();
    void startInstall();
    void beginSoftware();
    void downloadSoftwareOnly();
    void nextSoftware();
    void pollSoftware();
    void nativeFinished(const sdiomodern::InstallReport &);
    void nativeProgress(const sdiomodern::Event &);
    void details(int row, int candidate = -1);
    QDialog *detailDialog(int row, int candidate = -1);
    void tools();
    void downloads(bool builder = false);
    void chooseLibrary();
    void windowsTool(const QString &, const QStringList & = QStringList());
    void setLanguage(bool);
    void applyLanguage(const QString &);
    void toolAction(const QString &);
    void setDark(const QString &);
    void refreshHardware();
    void showMessage(const QString &, const QString &);
    void refreshChrome();
    void languageDialog();
    void reportDialog();
    bool confirm(const QString &, const QString &);
    bool busy() const;
    QStringList neededPackages() const;
    void setDriver(int row, bool);
    void selectRecommended();
    void toggleSidebar();
    void toggleSoftware(const QString &, bool);
    QWidget *pageBody(QWidget *, QVBoxLayout **);
    QPushButton *action(const QString &, QWidget *, bool primary = false);
    QLabel *copy(const QString &, QWidget *, const char *role = "text");
    void section(QVBoxLayout *, const QString &, const QVector<int> & = QVector<int>());
    void programRow(QVBoxLayout *, int, bool toolkit = false);
    void driverRow(QVBoxLayout *, int, int candidate = -1);
    void setting(QVBoxLayout *, const char *, const char *, const char *, const char *, const QString &,
                 bool);
    void snapshot(const sdiomodern::Snapshot &);
    void applyOptions();
    void restoreDefaults();
    bool m_batchSelecting = false;
    bool m_downloadOnly = false, m_resultDownloadOnly = false, m_restoreFailed = false, m_restoreSkipped=false;
    sdiomodern::InstallPlan m_activeInstallPlan;
    bool m_cancelSoftware = false, m_softwareOnly = false;
    QString m_singleSoftware;
    QSet<QString> m_activeSoftware;
    bool m_arabic = false, m_dark = false, m_demo = false, m_capture = false, m_initial = true,
         m_installing = false, m_pendingSoftware = false, m_softwareActive = false, m_showInstalled = true,
         m_additional = false, m_suspicious = false, m_lastReboot = false;
    int m_page = 0, m_mode = 2, m_softwarePosition = 0, m_currentProgram = -1;
    QString m_library, m_error, m_model, m_themeMode, m_language;
    int m_settingsTab = 0, m_diagnosticTab = 0;
    QWidget *m_settingsGrid = 0;
    ToolsDrawer *m_tools = 0;
    EngineBridge *m_engine;
    DriverCatalog *m_catalog;
    QSettings *m_settings;
    HardwareWorker *m_hardware;
    QJsonObject m_hardwareData;
    QVector<DriverPackSoftware::Program> m_programs;
    QSet<QString> m_programSelection, m_expanded, m_remote, m_wanted;
    QVector<int> m_softwareQueue;
    void *m_setup = 0;
    DrpSoftwareFetch *m_fetch = 0;
    QTimer *m_setupTimer, *m_clock;
    QElapsedTimer m_elapsed;
    QMap<QString, int> m_remoteChoices;
    QMap<QString, QString> m_resultStates;
    QMap<QString, QTreeWidgetItem *> m_installItemMap;
    QMap<QString, qint64> m_itemStarted;
    QString m_activeItem;
    QMap<QString, int> m_resultRows;
    QStringList m_resultNames;
    bool m_resultFailed = false;
    QStackedWidget *m_root, *m_pages;
    QWidget *m_header, *m_side, *m_start, *m_expert, *m_loading, *m_install, *m_final;
    QHBoxLayout *m_headerLayout;
    QPlainTextEdit *m_loadDetail;
    QLabel *m_loadHeading;
    QLabel *m_brand, *m_loadText, *m_installText, *m_elapsedText;
    QProgressBar *m_loadBar, *m_installBar;
    QTreeWidget *m_installItems;
    QPushButton *m_primary, *m_footerPrimary, *m_softwarePrimary;
    QMap<int, QAbstractButton *> m_nav;
};
#endif
