#include "restart_prompt.h"
#include "appearance.h"
#include "ui_language.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QPushButton>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
static QString restartWindows(){
#ifdef Q_OS_WIN
    HANDLE token=0;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token))return QStringLiteral("Cannot obtain the Windows restart privilege.");
    TOKEN_PRIVILEGES privileges={};privileges.PrivilegeCount=1;
    if(!LookupPrivilegeValueW(0,SE_SHUTDOWN_NAME,&privileges.Privileges[0].Luid)){CloseHandle(token);return QStringLiteral("Cannot obtain the Windows restart privilege.");}
    privileges.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;SetLastError(ERROR_SUCCESS);
    const BOOL adjusted=AdjustTokenPrivileges(token,FALSE,&privileges,0,0,0);const DWORD error=GetLastError();CloseHandle(token);
    if(!adjusted || error!=ERROR_SUCCESS)return QStringLiteral("Windows did not grant permission to restart.");
    // Applications may save their work; never force their termination.
    if(!ExitWindowsEx(EWX_REBOOT,SHTDN_REASON_MAJOR_HARDWARE|SHTDN_REASON_MINOR_INSTALLATION|SHTDN_REASON_FLAG_PLANNED))return QStringLiteral("Windows could not start the restart request.");
    return QString();
#else
    return QStringLiteral("Restart requires Windows.");
#endif
}
bool restartPrompt(QWidget *parent,int seconds,const std::function<QString()> &action){
    using UiLanguage::text;
    RoundedDialog dialog(parent);dialog.setObjectName(QStringLiteral("restartPrompt"));dialog.resize(530,290);
    QVBoxLayout *layout=new QVBoxLayout(&dialog);layout->setContentsMargins(0,0,0,18);
    layout->addWidget(new DialogHeader(text(QStringLiteral("Restart required"),QString::fromUtf8("إعادة التشغيل مطلوبة")),QStringLiteral("refresh"),&dialog,UiLanguage::isRtl()));
    QLabel *message=new QLabel(text(QStringLiteral("Driver installation finished. Save your work before Windows restarts. You can cancel and restart later."),QString::fromUtf8("اكتمل تثبيت التعريفات. احفظ عملك قبل إعادة تشغيل ويندوز. يمكنك الإلغاء وإعادة التشغيل لاحقًا.")),&dialog);message->setWordWrap(true);message->setMargin(20);layout->addWidget(message);
    QLabel *count=new QLabel(&dialog);count->setObjectName(QStringLiteral("restartCountdown"));count->setAlignment(Qt::AlignCenter);layout->addWidget(count);
    QHBoxLayout *buttons=new QHBoxLayout();buttons->setContentsMargins(20,0,20,0);ActionButton *cancel=new ActionButton(text(QStringLiteral("Restart later"),QString::fromUtf8("إعادة التشغيل لاحقًا")),&dialog);cancel->setObjectName(QStringLiteral("restartCancel"));cancel->setDefault(true);buttons->addWidget(cancel);buttons->addStretch();ActionButton *now=new ActionButton(text(QStringLiteral("Restart now"),QString::fromUtf8("إعادة التشغيل الآن")),&dialog);now->setObjectName(QStringLiteral("primaryButton"));buttons->addWidget(now);layout->addLayout(buttons);
    QObject::connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);QObject::connect(now,&QPushButton::clicked,&dialog,&QDialog::accept);
    int remaining=qMax(1,seconds);auto show=[&]{count->setText(text(QStringLiteral("Restarting in %1 seconds"),QString::fromUtf8("إعادة التشغيل خلال %1 ثانية")).arg(remaining));};show();
    QTimer timer;timer.setInterval(1000);QObject::connect(&timer,&QTimer::timeout,&dialog,[&]{--remaining;show();if(remaining<=0)dialog.accept();});timer.start();
    const bool accepted=dialog.exec()==QDialog::Accepted;timer.stop();if(!accepted)return false;
    const QString error=action?action():restartWindows();if(!error.isEmpty()){projectMessage(parent,text(QStringLiteral("Restart failed")),error);return false;}return true;
}
