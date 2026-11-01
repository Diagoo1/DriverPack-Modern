@echo off
setlocal
cd /d "%~dp0"
where pwsh >nul 2>nul
if errorlevel 1 (set "DPM_SHELL=powershell") else (set "DPM_SHELL=pwsh")
if not "%~1"=="" goto run
echo DriverPack Modern - Build / Test / Probe / Preview / All
echo Example: BUILD.cmd -Action Build -SdkRoot "C:\Dev\Snappy-Driver-Installer-Modern"
echo Requires a prepared static SDK; see docs\BUILDING.md.
exit /b 0
:run
%DPM_SHELL% -NoProfile -ExecutionPolicy Bypass -File "%~dp0toolsuild.ps1" %*
exit /b %errorlevel%
