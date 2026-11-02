// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/crash_diagnostics.h"
#include <QCoreApplication>
#include <QThread>
#include <QFile>
#include <QDir>
#include <exception>
#include <cstdlib>
#include <windows.h>
class FaultingThread:public QThread {
    void run() Q_DECL_OVERRIDE {CrashDiagnostics::checkpoint(QStringLiteral("worker-before-crash"));CrashDiagnostics::testCrash();}
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(args.size()!=3 || !CrashDiagnostics::initialize(args[1]))return 2;
    CrashDiagnostics::checkpoint(QStringLiteral("probe-before-action"));
    if(args[2]==QStringLiteral("clear")) {
        const QString folder=CrashDiagnostics::directory();QFile events(QDir(folder).filePath(QStringLiteral("native-events.jsonl")));events.open(QIODevice::WriteOnly);events.write("old events");events.close();
        if(!CrashDiagnostics::clearLogs())return 10;events.open(QIODevice::ReadOnly);if(events.size()!=0)return 11;events.close();
        CrashDiagnostics::checkpoint(QStringLiteral("probe-after-clear"));CrashDiagnostics::shutdown();return 0;
    }
    if(args[2]==QStringLiteral("normal")){CrashDiagnostics::shutdown();return 0;}
    if(args[2]==QStringLiteral("full"))for(int n=0;n<1200;++n)CrashDiagnostics::checkpoint(QString(4096,QLatin1Char('x')));
    if(args[2]==QStringLiteral("qt-fatal"))qFatal("intentional Qt fatal test");
    if(args[2]==QStringLiteral("terminate"))std::terminate();
    if(args[2]==QStringLiteral("abort"))std::abort();
    if(args[2]==QStringLiteral("worker")){FaultingThread thread;thread.start();thread.wait();return 4;}
    CrashDiagnostics::testCrash();return 3;
}
