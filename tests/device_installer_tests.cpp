// SPDX-License-Identifier: GPL-3.0-or-later
// All mutating Windows functions below are fakes; no real device is opened.
#include <windows.h>
#include <setupapi.h>
#include <cstdio>
#include <stdexcept>
static int staged=0,installed=0,checks=0;
static bool available=true,compatible=true,wrongFile=false,failInstall=false;
static WCHAR driverPath[MAX_PATH];
static BOOL WINAPI fakeInstall(HWND,HDEVINFO,PSP_DEVINFO_DATA d,PSP_DRVINFO_DATA_W r,DWORD flags,PBOOL reboot){
    if(d->DevInst!=123 || r->DriverVersion!=0x0001000200030004ULL || flags)throw std::runtime_error("installation escaped exact node");
    ++installed;*reboot=TRUE;if(failInstall){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return TRUE;
}
static HMODULE fakeLoad(LPCWSTR){return reinterpret_cast<HMODULE>(1);}
static BOOL fakeFree(HMODULE){return TRUE;}
static FARPROC fakeProc(HMODULE,LPCSTR){return available?reinterpret_cast<FARPROC>(&fakeInstall):NULL;}
static HDEVINFO fakeCreate(const GUID*,HWND){return reinterpret_cast<HDEVINFO>(2);}
static BOOL fakeOpen(HDEVINFO,LPCWSTR id,HWND,DWORD,PSP_DEVINFO_DATA d){if(lstrcmpW(id,L"EXACT-INSTANCE")){SetLastError(ERROR_NO_SUCH_DEVINST);return FALSE;}d->DevInst=123;return TRUE;}
static BOOL fakeGet(HDEVINFO,PSP_DEVINFO_DATA,PSP_DEVINSTALL_PARAMS_W){return TRUE;}
static BOOL fakeSet(HDEVINFO,PSP_DEVINFO_DATA,PSP_DEVINSTALL_PARAMS_W p){if(!(p->Flags&DI_ENUMSINGLEINF))throw std::runtime_error("unrestricted driver search");lstrcpyW(driverPath,p->DriverPath);return TRUE;}
static BOOL fakeBuild(HDEVINFO,PSP_DEVINFO_DATA,DWORD type){if(type!=SPDIT_COMPATDRIVER)throw std::runtime_error("not a compatible-node list");return TRUE;}
static BOOL fakeEnum(HDEVINFO,PSP_DEVINFO_DATA,DWORD,DWORD index,PSP_DRVINFO_DATA_W d){if(index>0 || !compatible){SetLastError(ERROR_NO_MORE_ITEMS);return FALSE;}d->DriverVersion=0x0001000200030004ULL;return TRUE;}
static BOOL fakeDetail(HDEVINFO,PSP_DEVINFO_DATA,PSP_DRVINFO_DATA_W,PSP_DRVINFO_DETAIL_DATA_W d,DWORD,PDWORD){lstrcpyW(d->InfFileName,wrongFile?L"C:\\different.inf":driverPath);SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}
static BOOL fakeStage(PCWSTR,PCWSTR,DWORD,DWORD,PWSTR out,DWORD,PDWORD,PWSTR*){++staged;lstrcpyW(out,L"C:\\Windows\\inf\\oem99.inf");return TRUE;}
static BOOL fakeSelect(HDEVINFO,PSP_DEVINFO_DATA,PSP_DRVINFO_DATA_W){return TRUE;}
static BOOL fakeDestroyList(HDEVINFO,PSP_DEVINFO_DATA,DWORD){return TRUE;}
static BOOL fakeDestroy(HDEVINFO){return TRUE;}
#define LoadLibraryW fakeLoad
#define FreeLibrary fakeFree
#define GetProcAddress fakeProc
#define SetupDiCreateDeviceInfoList fakeCreate
#define SetupDiOpenDeviceInfoW fakeOpen
#define SetupDiGetDeviceInstallParamsW fakeGet
#define SetupDiSetDeviceInstallParamsW fakeSet
#define SetupDiBuildDriverInfoList fakeBuild
#define SetupDiEnumDriverInfoW fakeEnum
#define SetupDiGetDriverInfoDetailW fakeDetail
#define SetupCopyOEMInfW fakeStage
#define SetupDiSetSelectedDriverW fakeSelect
#define SetupDiDestroyDriverInfoList fakeDestroyList
#define SetupDiDestroyDeviceInfoList fakeDestroy
#include "../native/device_installer.h"
static void check(bool ok){if(!ok)throw std::runtime_error("device installer assertion failed");++checks;}
int main(){try {
    ULONGLONG value=0;BOOL reboot=FALSE;
    check(drpVersion(L"1.2.3.4",&value) && value==0x0001000200030004ULL);
    check(!drpVersion(L"1.2.65536.4",&value));check(!drpVersion(L"1.2.3.4.5",&value));check(!drpVersion(L"1..2",&value));
    check(drpInstanceInstallAvailable());
    check(drpInstallInstance(L"EXACT-INSTANCE",L"C:\\private\\driver.inf",L"1.2.3.4",FALSE,&reboot)==0 && !staged && !installed && !reboot);
    check(drpInstallInstance(L"EXACT-INSTANCE",L"C:\\private\\driver.inf",L"1.2.3.4",TRUE,&reboot)==0 && staged==1 && installed==1 && reboot);
    staged=installed=0;
    check(drpInstallInstance(L"OTHER-INSTANCE",L"C:\\private\\driver.inf",L"1.2.3.4",TRUE,&reboot)==ERROR_NO_SUCH_DEVINST && !staged && !installed);
    check(drpInstallInstance(L"EXACT-INSTANCE",L"C:\\private\\driver.inf",L"9.9.9.9",TRUE,&reboot)==ERROR_NO_COMPAT_DRIVERS && !staged && !installed);
    wrongFile=true;
    check(drpInstallInstance(L"EXACT-INSTANCE",L"C:\\private\\driver.inf",L"1.2.3.4",TRUE,&reboot)==ERROR_NO_COMPAT_DRIVERS && !staged && !installed);
    wrongFile=false;compatible=false;
    check(drpInstallInstance(L"EXACT-INSTANCE",L"C:\\private\\driver.inf",L"1.2.3.4",TRUE,&reboot)==ERROR_NO_COMPAT_DRIVERS && !staged && !installed);
    compatible=true;available=false;
    check(!drpInstanceInstallAvailable());
    check(drpInstallInstance(L"EXACT-INSTANCE",L"C:\\private\\driver.inf",L"1.2.3.4",TRUE,&reboot)==ERROR_CALL_NOT_IMPLEMENTED && !staged && !installed);
    available=true;failInstall=true;
    check(drpInstallInstance(L"EXACT-INSTANCE",L"C:\\private\\driver.inf",L"1.2.3.4",TRUE,&reboot)==ERROR_ACCESS_DENIED && installed==1);
    printf("%d exact-device installer checks passed; all installation APIs mocked.\n",checks);return 0;
}catch(const std::exception &e){fprintf(stderr,"%s\n",e.what());return 1;}}
