#include "ui_scale.h"
#include "app_settings.h"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedPointer>
#include <QByteArray>
#include <cstring>
#include <QCoreApplication>
#include <QDir>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif
namespace UiScale {
bool administrator(){
#ifdef Q_OS_WIN
    SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY;PSID group=0;BOOL member=FALSE;
    if(!AllocateAndInitializeSid(&authority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&group))return false;
    const BOOL checked=CheckTokenMembership(0,group,&member);FreeSid(group);return checked && member;
#else
    return false;
#endif
}
QString restartElevated(){
#ifdef Q_OS_WIN
    const QString executable=QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString directory=QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const QString arguments=QStringLiteral("--restart-after %1").arg(QCoreApplication::applicationPid());
    SHELLEXECUTEINFOW info={};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI;
    info.lpVerb=L"runas";info.lpFile=reinterpret_cast<LPCWSTR>(executable.utf16());info.lpParameters=reinterpret_cast<LPCWSTR>(arguments.utf16());info.lpDirectory=reinterpret_cast<LPCWSTR>(directory.utf16());info.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&info))return QString::number(GetLastError());
    if(info.hProcess)CloseHandle(info.hProcess);return QString();
#else
    return QStringLiteral("Windows only");
#endif
}
static int effective=100;
int fitPercent(int requested,const QSize &available){
    const int maximum=qMin((available.width()-24)*100/950,(available.height()-24)*100/640);
    return qBound(50,qMin(qBound(50,requested,200),qMax(50,maximum/5*5)),200);
}
void prepareEnvironment(){
    QScopedPointer<QSettings> settings(createAppSettings(QString(),0,false));
    bool overrideValid=false;const int overridePercent=qgetenv("SDIO_ZOOM_PERCENT_OVERRIDE").toInt(&overrideValid);
    const int requested=qBound(50,overrideValid?overridePercent:settings->value(QStringLiteral("ui/scalePercent"),100).toInt(),200);
    QByteArray original=qgetenv("SDIO_BASE_SCALE_FACTOR");if(original.isEmpty())original=qgetenv("QT_SCALE_FACTOR");
    bool valid=false;double base=original.toDouble(&valid);if(!valid || base<.5 || base>4)base=1;
    effective=requested;
#ifdef Q_OS_WIN
    RECT work={};if(SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0)){
        HDC screen=GetDC(0);const int dpi=screen?GetDeviceCaps(screen,LOGPIXELSX):96;if(screen)ReleaseDC(0,screen);
        const double system=qMax(1.,dpi/96.0)*base;
        effective=fitPercent(requested,QSize(qRound((work.right-work.left)/system),qRound((work.bottom-work.top)/system)));
    }
#endif
    qputenv("SDIO_BASE_SCALE_FACTOR",QByteArray::number(base,'f',4));
    qputenv("QT_SCALE_FACTOR",QByteArray::number(base*effective/100.,'f',4));
}
int effectivePercent(){return effective;}
bool restartAfterParent(int argc,char **argv){
    for(int i=1;i<argc;++i)if(std::strcmp(argv[i],"--restart-after")==0){
        if(i+1>=argc)return false;bool ok=false;const quint32 pid=QByteArray(argv[i+1]).toUInt(&ok);if(!ok || !pid)return false;
#ifdef Q_OS_WIN
        HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,pid);if(!parent)return GetLastError()==ERROR_INVALID_PARAMETER;const DWORD status=WaitForSingleObject(parent,60000);CloseHandle(parent);if(status!=WAIT_OBJECT_0)return false;
#endif
    }return true;
}
}
