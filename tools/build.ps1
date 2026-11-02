param(
 [ValidateSet('Build','Test','Probe','Preview','All')][string]$Action='Build',
 [string]$SdkRoot='',
 [string]$Library='D:\Prog\DriverPack_Solution_Offline_17'
)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
if(-not $SdkRoot){$SdkRoot=$env:DRIVERPACK_SDK_ROOT}
if(-not $SdkRoot){throw 'Provide -SdkRoot or DRIVERPACK_SDK_ROOT; see docs/BUILDING.md.'}
$qmake=Join-Path $SdkRoot 'vendor\qt-static\bin\qmake.exe'
$compiler='C:\Qt\Qt5.6.3\Tools\mingw492_32\bin'
if($env:DRIVERPACK_MINGW_BIN){$compiler=$env:DRIVERPACK_MINGW_BIN}
if(-not (Test-Path -LiteralPath $qmake)){throw 'Set -SdkRoot to the existing SDIO development SDK containing vendor\qt-static.'}
$env:PATH=$compiler+';'+$env:PATH
function Checked([scriptblock]$Command){& $Command;if($LASTEXITCODE -ne 0){throw "Command failed: $LASTEXITCODE"}}
function Compile([string]$Pro,[string]$Directory,[string[]]$Extra=@()){
 New-Item -ItemType Directory -Force -Path $Directory | Out-Null
 Checked {& $qmake $Pro @Extra -o (Join-Path $Directory 'Makefile')}
 Checked {& mingw32-make -C $Directory -j2}
}
$build=Join-Path $project 'build'
$exe=Join-Path $build 'app\release\DriverPackModern.exe'
if($Action -in @('Build','All')){
 Checked {& python (Join-Path $project 'tools\integration\bootstrap_fonts.py')}
 & (Join-Path $project 'tools\integration\build_install_helper.ps1') -OriginRoot $project -Output (Join-Path $project 'build\helper\install64.exe')
 Copy-Item -LiteralPath (Join-Path $project 'build\helper\install64.exe') -Destination (Join-Path $project 'native\install64.exe') -Force
 $peer=Join-Path $SdkRoot 'build-torrent-static\torrent-link.pri'
 if(-not (Test-Path -LiteralPath $peer)){throw 'Build the static torrent SDK first; peer transport is required for this release configuration.'}
 Compile (Join-Path $project 'DriverPackModern.pro') (Join-Path $build 'app') @('CONFIG+=single_file',('DRIVERPACK_TORRENT_PRI='+$peer.Replace('\','/')))
 $release=Join-Path $project 'release';New-Item -ItemType Directory -Force $release | Out-Null
 Copy-Item -LiteralPath $exe -Destination (Join-Path $release 'DriverPackModern.exe') -Force
 Copy-Item -LiteralPath (Join-Path $project 'tools\Clean DriverPack.cmd') -Destination (Join-Path $release 'Clean DriverPack.cmd') -Force
 Get-FileHash -LiteralPath (Join-Path $release 'DriverPackModern.exe') -Algorithm SHA256
}
if($Action -in @('Test','All')){
 Compile (Join-Path $project 'tests\catalog_tests.pro') (Join-Path $build 'checks')
 Compile (Join-Path $project 'tests\transfer_tests.pro') (Join-Path $build 'transfers')
 Compile (Join-Path $project 'tests\independence_tests.pro') (Join-Path $build 'independence')
 Compile (Join-Path $project 'tests\maintenance_tests.pro') (Join-Path $build 'maintenance-tests')
 Compile (Join-Path $project 'tests\activity_log_tests.pro') (Join-Path $build 'activity-log-tests')
 Compile (Join-Path $project 'tests\software_tests.pro') (Join-Path $build 'software-tests')
 $peer=Join-Path $SdkRoot 'build-torrent-static\torrent-link.pri'
 Compile (Join-Path $project 'tests\peer_tests.pro') (Join-Path $build 'peer-tests') @(('DRIVERPACK_TORRENT_PRI='+$peer.Replace('\','/')))
 Compile (Join-Path $project 'tests\device_installer_tests.pro') (Join-Path $build 'device-installer-tests')
 Compile (Join-Path $project 'tests\engine_retry_tests.pro') (Join-Path $build 'engine-retry')
 Compile (Join-Path $project 'tests\diagnostic_sessions_tests.pro') (Join-Path $build 'session-tests')
 $data=Join-Path $project 'tests\data'
 Checked {& (Join-Path $build 'checks\release\DriverPackCatalogTests.exe') $data}
 Checked {& (Join-Path $build 'transfers\release\DriverPackTransferTests.exe') $data}
 Checked {& (Join-Path $build 'independence\release\DriverPackIndependenceTests.exe') $data}
 Checked {& (Join-Path $build 'software-tests\release\DriverPackSoftwareTests.exe')}
 Checked {& (Join-Path $build 'activity-log-tests\release\DriverPackActivityLogTests.exe')}
 Checked {& (Join-Path $build 'maintenance-tests\release\DriverPackMaintenanceTests.exe')}
 Checked {& (Join-Path $build 'peer-tests\release\DriverPackPeerTests.exe')}
 Checked {& (Join-Path $build 'device-installer-tests\release\DriverPackDeviceScopeTests.exe')}
 Checked {& (Join-Path $build 'engine-retry\release\DriverPackEngineRetryTests.exe')}
 Checked {& (Join-Path $build 'engine-retry\release\DriverPackEngineRetryTests.exe') --all --defer}
 Checked {& (Join-Path $build 'engine-retry\release\DriverPackEngineRetryTests.exe') --all --cancel}
 Checked {& (Join-Path $build 'session-tests\release\DriverPackSessionTests.exe')}
}
if($Action -in @('Probe','All')){
 if(-not (Test-Path -LiteralPath $exe)){throw 'Run Build first.'}
 # Read-only matching and extraction; this does not call the driver installer.
 Checked {& $exe --scan-report (Join-Path $build 'scan-report.json') --drivers (Join-Path $Library 'drivers') --indexes (Join-Path $Library 'index') --repeat-scan --extract-first-to (Join-Path $build 'extracted')}
}
if($Action -eq 'Preview'){
 if(-not (Test-Path -LiteralPath $exe)){throw 'Run Build first.'}
 $env:DRIVERPACK_MODERN_TEST_SETTINGS=Join-Path $build 'preview-settings.ini'
 [IO.File]::WriteAllText($env:DRIVERPACK_MODERN_TEST_SETTINGS,"[General]`nlanguage=english`nreduceMotion=true`n[transfer]`nkeepSharing=false`n")
 Checked {& $exe --capture-live (Join-Path $build 'preview') --library $Library}
}
