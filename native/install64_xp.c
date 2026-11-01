// SPDX-License-Identifier: GPL-3.0-or-later
// CRT-free x64 Windows driver API companion. A structured result prevents
// ERROR_INVALID_FUNCTION (1) from ever being interpreted as success.
#define WINVER 0x0502
#define _WIN32_WINNT 0x0502
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <newdev.h>
void helperEntry(void){
 int count=0;WCHAR **args=CommandLineToArgvW(GetCommandLineW(),&count);
 DWORD status=ERROR_INVALID_PARAMETER;
 if(args&&count==4){
  HANDLE output=CreateFileW(args[3],GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
  if(output!=INVALID_HANDLE_VALUE){
   BOOL restart=FALSE;BOOL ok=UpdateDriverForPlugAndPlayDevicesW(NULL,args[1],args[2],INSTALLFLAG_FORCE,&restart);
   DWORD error=ok?ERROR_SUCCESS:GetLastError();DWORD fields[4];DWORD written=0;
   fields[0]=0x44504D31;fields[1]=ok?1:0;fields[2]=ok?0:(error?error:ERROR_GEN_FAILURE);fields[3]=restart?1:0;
   status=WriteFile(output,fields,sizeof(fields),&written,NULL)&&written==sizeof(fields)?0:ERROR_WRITE_FAULT;
   CloseHandle(output);
  }else status=GetLastError();
 }
 if(args)LocalFree(args);ExitProcess(status);
}
