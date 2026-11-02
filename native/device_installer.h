// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DRIVERPACK_DEVICE_INSTALLER_H
#define DRIVERPACK_DEVICE_INSTALLER_H
// Shared by the x86 application and its CRT-free native x64 companion.
#include <windows.h>
#include <setupapi.h>
typedef BOOL (WINAPI *DrpDiInstallDevice)(HWND,HDEVINFO,PSP_DEVINFO_DATA,PSP_DRVINFO_DATA_W,DWORD,PBOOL);
static HMODULE drpNewdev(void) {
    WCHAR path[MAX_PATH]; UINT size=GetSystemDirectoryW(path,MAX_PATH);
    if(!size || size>MAX_PATH-12)return NULL;
    lstrcatW(path,L"\\newdev.dll");return LoadLibraryW(path);
}
static BOOL drpInstanceInstallAvailable(void) {
    HMODULE module=drpNewdev();BOOL ok=module && GetProcAddress(module,"DiInstallDevice")!=NULL;
    if(module)FreeLibrary(module);return ok;
}
static BOOL drpVersion(const WCHAR *text,ULONGLONG *value) {
    int part; ULONGLONG out=0;
    if(!text || !*text)return FALSE;
    for(part=0;part<4;++part) {
        DWORD number=0;BOOL digit=FALSE;
        while(*text>=L'0' && *text<=L'9'){digit=TRUE;number=number*10+(*text++-L'0');if(number>65535)return FALSE;}
        if(!digit)return FALSE;out=(out<<16)|number;
        if(!*text){for(++part;part<4;++part)out<<=16;*value=out;return TRUE;}
        if(*text++!=L'.')return FALSE;
    }
    return FALSE;
}
// apply=FALSE enumerates Windows-compatible nodes only: no staging or installation.
// Never fall back from this function to a hardware-ID-wide update.
static DWORD drpInstallInstance(const WCHAR *instance,const WCHAR *inf,const WCHAR *version,
                               BOOL apply,BOOL *reboot) {
    struct Work { SP_DEVINFO_DATA device; SP_DEVINSTALL_PARAMS_W params; SP_DRVINFO_DATA_W driver;
                  SP_DRVINFO_DATA_W selected; SP_DRVINFO_DETAIL_DATA_W detail; WCHAR staged[MAX_PATH]; };
    struct Work *work; HDEVINFO devices; HMODULE module; DrpDiInstallDevice install;
    ULONGLONG expected=0;DWORD error=ERROR_INVALID_PARAMETER,index,pass;BOOL found=FALSE;
    if(reboot)*reboot=FALSE;
    if(!reboot || !instance || !*instance || !inf || !*inf || lstrlenW(inf)>=MAX_PATH || !drpVersion(version,&expected))return error;
    module=drpNewdev();if(!module)return GetLastError();
    install=(DrpDiInstallDevice)GetProcAddress(module,"DiInstallDevice");
    if(!install){FreeLibrary(module);return ERROR_CALL_NOT_IMPLEMENTED;}
    work=(struct Work*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(struct Work));
    if(!work){FreeLibrary(module);return ERROR_NOT_ENOUGH_MEMORY;}
    devices=SetupDiCreateDeviceInfoList(NULL,NULL);
    if(devices==INVALID_HANDLE_VALUE){error=GetLastError();goto cleanup;}
    work->device.cbSize=sizeof(work->device);
    if(!SetupDiOpenDeviceInfoW(devices,instance,NULL,0,&work->device)){error=GetLastError();goto cleanup;}
    // First validate the extracted INF without changes. After staging, rebuild
    // from that exact OEM INF; never ask Windows to pick another store driver.
    for(pass=0;pass<(apply?2u:1u);++pass) {
        work->params.cbSize=sizeof(work->params);
        if(!SetupDiGetDeviceInstallParamsW(devices,&work->device,&work->params)){error=GetLastError();goto cleanup;}
        work->params.Flags|=DI_ENUMSINGLEINF|DI_QUIETINSTALL;
        work->params.FlagsEx|=DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
        {DWORD length=GetFullPathNameW(pass?work->staged:inf,MAX_PATH,work->params.DriverPath,NULL);if(!length || length>=MAX_PATH){error=ERROR_FILENAME_EXCED_RANGE;goto cleanup;}}
        if(!SetupDiSetDeviceInstallParamsW(devices,&work->device,&work->params) ||
           !SetupDiBuildDriverInfoList(devices,&work->device,SPDIT_COMPATDRIVER)){error=GetLastError();goto cleanup;}
        found=FALSE;
        for(index=0;;++index) {
            work->driver.cbSize=sizeof(work->driver);
            if(!SetupDiEnumDriverInfoW(devices,&work->device,SPDIT_COMPATDRIVER,index,&work->driver)) {
                error=GetLastError();if(error!=ERROR_NO_MORE_ITEMS)goto cleanup;break;
            }
            if(work->driver.DriverVersion!=expected)continue;
            work->detail.cbSize=sizeof(work->detail);
            if(!SetupDiGetDriverInfoDetailW(devices,&work->device,&work->driver,&work->detail,sizeof(work->detail),NULL)
               && GetLastError()!=ERROR_INSUFFICIENT_BUFFER){error=GetLastError();goto cleanup;}
            if(lstrcmpiW(work->detail.InfFileName,work->params.DriverPath))continue;
            // Equal-version compatible nodes in the same INF retain Windows order.
            if(!found){DWORD b;volatile BYTE *to=(volatile BYTE*)&work->selected;const volatile BYTE *from=(const volatile BYTE*)&work->driver;for(b=0;b<sizeof(work->driver);++b)to[b]=from[b];found=TRUE;}
        }
        if(!found){error=ERROR_NO_COMPAT_DRIVERS;goto cleanup;}
        if(!apply){error=ERROR_SUCCESS;goto cleanup;}
        if(pass==0) {
            SetupDiDestroyDriverInfoList(devices,&work->device,SPDIT_COMPATDRIVER);
            if(!SetupCopyOEMInfW(inf,NULL,SPOST_PATH,0,work->staged,MAX_PATH,NULL,NULL)){error=GetLastError();goto cleanup;}
        }
    }
    if(!SetupDiSetSelectedDriverW(devices,&work->device,&work->selected)){error=GetLastError();goto cleanup;}
    if(install(NULL,devices,&work->device,&work->selected,0,reboot))error=ERROR_SUCCESS;
    else {error=GetLastError();if(!error)error=ERROR_GEN_FAILURE;}
cleanup:
    if(devices!=INVALID_HANDLE_VALUE){SetupDiDestroyDriverInfoList(devices,&work->device,SPDIT_COMPATDRIVER);SetupDiDestroyDeviceInfoList(devices);}
    HeapFree(GetProcessHeap(),0,work);FreeLibrary(module);return error;
}
#endif
