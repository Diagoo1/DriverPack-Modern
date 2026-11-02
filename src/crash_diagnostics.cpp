// SPDX-License-Identifier: GPL-3.0-or-later
#include "crash_diagnostics.h"
#include "diagnostic_sessions.h"
#include <QLockFile>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>
#include <exception>
#include <csignal>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#endif

namespace {
QString logDirectory;
QLockFile *sessionLock=0;
#ifdef Q_OS_WIN
HANDLE journal=INVALID_HANDLE_VALUE, requested=0, finished=0, writer=0;
volatile LONG handling=0;
volatile LONG detailsLimited=0;
DWORD faultThread=0;
EXCEPTION_POINTERS *fault=0;
wchar_t dumpPath[32768]={0};
typedef BOOL (WINAPI *DumpFunction)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION,PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
DumpFunction writeDump=0;
QtMessageHandler previousMessageHandler=0;
void raw(const char *bytes,DWORD size,bool critical=false) {
    if(journal==INVALID_HANDLE_VALUE)return;
    LARGE_INTEGER length;
    if(!critical && GetFileSizeEx(journal,&length) && length.QuadPart>4*1024*1024){
        if(InterlockedCompareExchange(&detailsLimited,1,0)==0){const char notice[]="DETAIL LIMIT: session trace capped at 4 MiB; fatal exception/dump records remain enabled.\r\n";raw(notice,sizeof(notice)-1,true);}
        return;
    }
    DWORD written=0;WriteFile(journal,bytes,size,&written,0);FlushFileBuffers(journal);
}
DWORD WINAPI dumpWorker(void *) {
    if(WaitForSingleObject(requested,INFINITE)!=WAIT_OBJECT_0)return 0;
    DWORD error=ERROR_PROC_NOT_FOUND;BOOL saved=FALSE;
    if(writeDump) {
        HANDLE file=CreateFileW(dumpPath,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
        if(file!=INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION info={faultThread,fault,FALSE};
            saved=writeDump(GetCurrentProcess(),GetCurrentProcessId(),file,MiniDumpNormal,&info,0,0);
            error=saved?0:GetLastError();FlushFileBuffers(file);CloseHandle(file);
        } else error=GetLastError();
    }
    char line[128];const int n=wsprintfA(line,"crash-dump saved=%lu win32Error=%lu\r\n",DWORD(saved),error);
    raw(line,n,true);SetEvent(finished);return 0;
}
LONG WINAPI onException(EXCEPTION_POINTERS *info) {
    if(InterlockedCompareExchange(&handling,1,0)!=0)return EXCEPTION_CONTINUE_SEARCH;
    fault=info;faultThread=GetCurrentThreadId();
    char line[200];const int n=wsprintfA(line,
        "FATAL exception=0x%08lX address=0x%p imageBase=0x%p thread=%lu pid=%lu\r\n",
        info&&info->ExceptionRecord?info->ExceptionRecord->ExceptionCode:0,
        info&&info->ExceptionRecord?info->ExceptionRecord->ExceptionAddress:0,
        GetModuleHandleW(0),
        faultThread,GetCurrentProcessId());raw(line,n,true);
    // No Qt, heap allocation, or UI locks on the faulting thread. The dedicated
    // writer was created before the crash; preserve exception pointers while it runs.
    if(writer && requested && finished){SetEvent(requested);WaitForSingleObject(finished,10000);}
    return EXCEPTION_EXECUTE_HANDLER;
}
void fatalTermination() {
    const char text[]="FATAL std::terminate or SIGABRT\r\n";raw(text,sizeof(text)-1,true);
    RaiseException(0xE044504D,EXCEPTION_NONCONTINUABLE,0,0);
    TerminateProcess(GetCurrentProcess(),0xE044504D);
}
void onAbort(int){fatalTermination();}
void qtMessage(QtMsgType type,const QMessageLogContext &context,const QString &message) {
    CrashDiagnostics::checkpoint(QStringLiteral("qt type=%1 %2 (%3:%4)")
        .arg(int(type)).arg(message,QString::fromUtf8(context.file?context.file:"")).arg(context.line));
    if(previousMessageHandler)previousMessageHandler(type,context,message);
}
#endif
}

bool CrashDiagnostics::initialize(const QString &requestedDirectory) {
#ifdef Q_OS_WIN
    if(journal!=INVALID_HANDLE_VALUE)return true;
    const QString session=DiagnosticSessions::create(requestedDirectory);if(session.isEmpty())return false;
    QDir folder(session);const QString text=folder.filePath(QStringLiteral("session.txt"));
    journal=CreateFileW(reinterpret_cast<LPCWSTR>(text.utf16()),FILE_APPEND_DATA|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,0,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,0);
    if(journal==INVALID_HANDLE_VALUE)return false;
    logDirectory=folder.absolutePath();
    sessionLock=new QLockFile(folder.filePath(QStringLiteral("active.lock")));sessionLock->setStaleLockTime(0);sessionLock->tryLock(0);
    DiagnosticSessions::prune(requestedDirectory,logDirectory);
    const QString dump=QDir::toNativeSeparators(folder.filePath(QStringLiteral("crash.dmp")));
    if(dump.size()<32768)lstrcpyW(dumpPath,reinterpret_cast<LPCWSTR>(dump.utf16()));
    // Load Windows' own copy, never a DLL supplied beside the library.
    wchar_t system[MAX_PATH];const UINT len=GetSystemDirectoryW(system,MAX_PATH);
    if(len && len+13<MAX_PATH) {
        lstrcatW(system,L"\\dbghelp.dll");HMODULE dbg=LoadLibraryW(system);
        if(dbg)writeDump=reinterpret_cast<DumpFunction>(GetProcAddress(dbg,"MiniDumpWriteDump"));
    }
    requested=CreateEventW(0,TRUE,FALSE,0);finished=CreateEventW(0,TRUE,FALSE,0);
    if(requested&&finished)writer=CreateThread(0,0,dumpWorker,0,0,0);
    SetUnhandledExceptionFilter(onException);std::set_terminate(fatalTermination);std::signal(SIGABRT,onAbort);
    previousMessageHandler=qInstallMessageHandler(qtMessage);
    checkpoint(QStringLiteral("session-start version=0.22.9 Beta pid=%1 exe=%2 windows=%3 cpu=%4 automaticUpload=false dumpWriter=%5")
        .arg(GetCurrentProcessId()).arg(QCoreApplication::applicationFilePath(),QSysInfo::prettyProductName(),QSysInfo::currentCpuArchitecture()).arg(writer&&writeDump?1:0));
    return true;
#else
    Q_UNUSED(requestedDirectory);return false;
#endif
}
QString CrashDiagnostics::directory(){return logDirectory;}
void CrashDiagnostics::checkpoint(const QString &message) {
#ifdef Q_OS_WIN
    QString clean=message;clean.replace(QLatin1Char('\r'),QLatin1Char(' '));clean.replace(QLatin1Char('\n'),QLatin1Char(' '));
    const QByteArray line=(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)+QStringLiteral(" [thread=%1] ").arg(GetCurrentThreadId())+clean+QStringLiteral("\r\n")).toUtf8();
    raw(line.constData(),DWORD(line.size()));
#else
    Q_UNUSED(message);
#endif
}
void CrashDiagnostics::shutdown(){checkpoint(QStringLiteral("session-clean-exit"));/* lock stays held until process exit */}
bool CrashDiagnostics::clearLogs(int *removedSessions){
    if(logDirectory.isEmpty())return false;
    const int removed=DiagnosticSessions::clearFinished(QFileInfo(logDirectory).absolutePath(),logDirectory);
    if(removedSessions)*removedSessions=removed;
    bool ok=true;for(const QString &name:{QStringLiteral("native-events.jsonl"),QStringLiteral("native-events.previous.jsonl"),QStringLiteral("session.txt")}){
        QFile file(QDir(logDirectory).filePath(name));if(!file.exists())continue;
        if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))ok=false;
    }
#ifdef Q_OS_WIN
    InterlockedExchange(&detailsLimited,0);
#endif
    checkpoint(QStringLiteral("session-log-reset version=0.22.9 removedSessions=%1 exe=%2").arg(removed).arg(QCoreApplication::applicationFilePath()));return ok;
}
void CrashDiagnostics::testCrash(){
#ifdef Q_OS_WIN
    RaiseException(EXCEPTION_ACCESS_VIOLATION,EXCEPTION_NONCONTINUABLE,0,0);
#endif
}
