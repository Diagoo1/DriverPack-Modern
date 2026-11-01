// SPDX-License-Identifier: GPL-3.0-or-later
#include "origin_worker.h"
#include "app_settings.h"
#include <QScopedPointer>
#include "offline_paths.h"
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>
#include <QProcess>
#include <QFileInfo>
#include <QSysInfo>
#include <exception>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
struct WorkerComScope {
    HRESULT hr;
    WorkerComScope():hr(CoInitializeEx(0,COINIT_MULTITHREADED)){}
    ~WorkerComScope(){if(SUCCEEDED(hr))CoUninitialize();}
};
#endif
OriginWorker::OriginWorker(QObject* p):QThread(p),job(Scan),m_stop(false) {}
OriginWorker::~OriginWorker(){stopAfterCurrent();wait();}
void OriginWorker::resetRequest(){m_stop.store(false);error.clear();output=sdiomodern::Snapshot();installation=sdiomodern::InstallReport();}
void OriginWorker::stopAfterCurrent(){m_stop.store(true);}
void OriginWorker::run()
{
    try {
#ifdef Q_OS_WIN
        WorkerComScope com;
        if(FAILED(com.hr) && com.hr!=RPC_E_CHANGED_MODE)throw std::runtime_error("Cannot initialize COM for the engine worker.");
#endif
        if(!m_core)m_core.reset(new sdiomodern::OriginCore);
        QScopedPointer<QSettings> settings(createAppSettings(QString(),0,false));
        paths.noLog=!settings->value(QStringLiteral("driverpack/enableLogging"),true).toBool();paths.keepTempFiles=!settings->value(QStringLiteral("driverpack/removeTemp"),false).toBool();
        m_core->setOperationOptions(paths);
        QDir logDir(QString::fromStdWString(paths.logDirectory));
        if(!paths.noLog&&!logDir.exists())logDir.mkpath(QStringLiteral("."));
        QFile log(logDir.filePath(QStringLiteral("native-events.jsonl")));
        if(!paths.noLog&&!log.open(QIODevice::WriteOnly|QIODevice::Append) && job==Install)
            throw std::runtime_error("Cannot open the local operation log. No installation was started.");
        const sdiomodern::EventSink sink=[this,&log](const sdiomodern::Event& e) {
            if(m_stop.load() && job==Scan)throw std::runtime_error("DriverPack catalog scan cancelled.");
            // Serialize exact engine events. No telemetry or automatic upload.
            if(log.isOpen()) {
                QJsonObject row;
                row.insert(QStringLiteral("time"),QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
                row.insert(QStringLiteral("stage"),static_cast<int>(e.stage));
                row.insert(QStringLiteral("outcome"),static_cast<int>(e.outcome));
                row.insert(QStringLiteral("device"),QString::fromStdWString(e.deviceId));
                row.insert(QStringLiteral("message"),QString::fromStdWString(e.message));
                row.insert(QStringLiteral("win32Error"),static_cast<double>(e.nativeError));
                row.insert(QStringLiteral("completed"),static_cast<double>(e.done));
                row.insert(QStringLiteral("total"),static_cast<double>(e.total));
                log.write(QJsonDocument(row).toJson(QJsonDocument::Compact));log.write("\n");log.flush();
            }
            emit progress(e);
        };
        if(job==Scan){sdiomodern::Paths session=paths;session.indexes=prepareLibraryIndexes(QString::fromStdWString(paths.drivers),QString::fromStdWString(paths.indexes)).toStdWString();output=m_core->scan(session,sink);if(!paths.indexTextDirectory.empty())m_core->exportIndexText(paths.indexTextDirectory);}
        else if(job==Install)installation=m_core->install(plan,sink,m_stop);
        else if(job==SaveSnapshot)m_core->saveSnapshot(snapshotFile);
        else if(job==Extract)snapshotFile=m_core->extract(plan,snapshotFile,sink,m_stop);
        else if(job==Restore)m_core->createRestorePoint();
        else if(job==DeviceList)m_core->writeDeviceList(snapshotFile);
        else {
#ifdef Q_OS_WIN
            if(QSysInfo::windowsVersion()<QSysInfo::WV_WINDOWS8_1)throw std::runtime_error("Driver export requires Windows 8.1 or later.");
            const QString windows=QString::fromLocal8Bit(qgetenv("SystemRoot"));
            QString dism=QDir(windows).filePath(QStringLiteral("Sysnative/dism.exe"));
            if(!QFileInfo(dism).isFile())dism=QDir(windows).filePath(QStringLiteral("System32/dism.exe"));
            if(!QFileInfo(dism).isFile())throw std::runtime_error("Installed-driver export requires Windows DISM. It is unavailable on XP.");
            QProcess process;process.setProcessChannelMode(QProcess::MergedChannels);
            process.start(dism,QStringList()<<QStringLiteral("/Online")<<QStringLiteral("/Export-Driver")
                <<(QStringLiteral("/Destination:")+QString::fromStdWString(snapshotFile)));
            if(!process.waitForStarted())throw std::runtime_error("Cannot start Windows driver export.");
            process.waitForFinished(-1);
            QFile details(QDir(QString::fromStdWString(snapshotFile)).filePath(QStringLiteral("export-log.txt")));
            if(details.open(QIODevice::WriteOnly))details.write(process.readAll());
            if(process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0)
                throw std::runtime_error("Windows could not export driver packages. Run as Administrator on a Windows version supporting DISM /Export-Driver; see export-log.txt.");
#else
            throw std::runtime_error("Installed-driver export requires Windows.");
#endif
        }
    }catch(const std::exception& e){error=QString::fromUtf8(e.what());}
    catch(...){error=QStringLiteral("The native engine failed with an unknown error.");}
}
