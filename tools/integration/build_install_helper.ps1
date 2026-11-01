param([Parameter(Mandatory=$true)][string]$OriginRoot,[Parameter(Mandatory=$true)][string]$Output)
. (Join-Path $PSScriptRoot 'common.ps1')
function Check-X64([string]$File) {
    $s=[IO.File]::OpenRead($File);$r=New-Object IO.BinaryReader($s)
    try {
        if($r.ReadUInt16() -ne 0x5A4D){throw 'Helper is not a PE executable.'}
        $s.Position=0x3c;$offset=$r.ReadUInt32();$s.Position=$offset
        if($r.ReadUInt32() -ne 0x4550 -or $r.ReadUInt16() -ne 0x8664){throw 'Helper must be a Windows x64 PE executable.'}
    } finally {$r.Dispose();$s.Dispose()}
}
$source=Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'native\install64_xp.c'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
# Compile the CRT-free implementation of the original helper protocol.
if($env:SDIO_MINGW64_BIN -and (Test-Path -LiteralPath (Join-Path $env:SDIO_MINGW64_BIN 'gcc.exe'))) {
    $gcc=Join-Path $env:SDIO_MINGW64_BIN 'gcc.exe'
    $machine=((& $gcc -dumpmachine)|Out-String).Trim();Require-Exit 'Query helper compiler'
    if($machine -notmatch 'x86_64.*mingw'){throw 'SDIO_MINGW64_BIN is not a 64-bit Windows compiler.'}
    & $gcc '-Os' '-s' '-nostdlib' '-fno-stack-protector' '-mwindows' '-Wl,--entry,helperEntry' '-Wl,--major-subsystem-version,5,--minor-subsystem-version,2' $source '-o' $Output '-lnewdev' '-lshell32' '-lkernel32'
    Require-Exit 'Compile x64 installation helper'
} else {
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if(-not (Test-Path -LiteralPath $vswhere)){throw 'Experimental installation requires an x64 helper. Install the Visual Studio Desktop development with C++ workload, or set SDIO_MINGW64_BIN.'}
    $vs=((& $vswhere '-latest' '-products' '*' '-requires' 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' '-property' 'installationPath')|Out-String).Trim()
    if(-not $vs){throw 'Visual Studio is present but its C++ x64 build tools are missing. Matching-only builds do not need this component.'}
    $vcvars=Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    if(-not (Test-Path -LiteralPath $vcvars)){throw 'vcvars64.bat was not found in the selected Visual Studio.'}
    $build=Split-Path -Parent $Output
    $obj=Join-Path $build 'install64.obj'
    $cmd=Join-Path $build 'compile-helper.cmd'
    $text="@echo off`r`ncall `"$vcvars`"`r`nif errorlevel 1 exit /b %errorlevel%`r`ncl /nologo /O1 /GS- /TC `"$source`" /Fo`"$obj`" /Fe`"$Output`" /link /NODEFAULTLIB /ENTRY:helperEntry /SUBSYSTEM:WINDOWS,5.02 /OSVERSION:5.2 /DYNAMICBASE /NXCOMPAT newdev.lib shell32.lib kernel32.lib`r`nexit /b %errorlevel%`r`n"
    [IO.File]::WriteAllText($cmd,$text,[Text.Encoding]::ASCII)
    & $env:ComSpec '/d' '/c' $cmd;Require-Exit 'Compile x64 installation helper'
    Say 'CRT-free x64 helper built with XP-era imports. Runtime compatibility still requires a VM test.'
}
if(-not (Test-Path -LiteralPath $Output)){throw 'Helper compiler did not produce the requested file.'}
Check-X64 $Output
Say 'x64 helper PE header validated. Actual driver installation still requires controlled Windows testing.'
