@echo off
setlocal DisableDelayedExpansion
if not exist "%~dp0DriverPackModern.exe" (
 echo Place this CMD beside DriverPackModern.exe in the DriverPack library.
 pause
 exit /b 1
)
start "" /wait "%~dp0DriverPackModern.exe" --cleanup-library "%~dp0."
exit /b %errorlevel%
