#include "single_instance.h"
#include <QWidget>
#ifdef Q_OS_WIN
#include <windows.h>
static const wchar_t windowProperty[]=L"DriverPackModern.TarekSadek.MainWindow";
static BOOL CALLBACK activateWindow(HWND window,LPARAM){
    if(GetPropW(window,windowProperty)){
        if(IsIconic(window))ShowWindow(window,SW_RESTORE);
        SetForegroundWindow(window);return FALSE;
    }
    return TRUE;
}
#endif
SingleInstance::SingleInstance(const wchar_t *name):m_handle(0),m_existing(false),m_available(true){
#ifdef Q_OS_WIN
    // Kernel-owned lifetime: crashes do not leave a stale lock file behind.
    // Shared across copies of the EXE in the same Windows user session.
    m_handle=CreateMutexW(0,FALSE,name?name:L"Local\\DriverPackModern.TarekSadek.ModernUI.SingleInstance");
    m_existing=m_handle && GetLastError()==ERROR_ALREADY_EXISTS;
    m_available=m_handle!=0;
    // A duplicate notification must not keep the primary session alive after
    // its real window exits. Only the primary retains a handle to the guard.
    if(m_existing){CloseHandle(static_cast<HANDLE>(m_handle));m_handle=0;}
#else
    Q_UNUSED(name);
#endif
}
SingleInstance::~SingleInstance(){
#ifdef Q_OS_WIN
    if(m_handle)CloseHandle(static_cast<HANDLE>(m_handle));
#endif
}
void SingleInstance::registerWindow(QWidget *window){
#ifdef Q_OS_WIN
    SetPropW(reinterpret_cast<HWND>(window->winId()),windowProperty,reinterpret_cast<HANDLE>(1));
#else
    Q_UNUSED(window);
#endif
}
void SingleInstance::activateExisting(){
#ifdef Q_OS_WIN
    EnumWindows(activateWindow,0);
#endif
}
