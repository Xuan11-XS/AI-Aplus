param(
    [string]$Repo = (Split-Path -Parent $PSScriptRoot),
    [string]$QtRoot = 'C:\Qt\6.11.2\mingw_64',
    [string]$MingwRoot = 'C:\Qt\Tools\mingw1310_64',
    [string]$OutDir = ''
)

# Builds the portable Windows package: aplus.exe + Qt runtime + trimmed MinGW g++.
# Usage: powershell -ExecutionPolicy Bypass -File tools/package-windows.ps1

$ErrorActionPreference = 'Stop'
$qtBin = Join-Path $QtRoot 'bin'
if ($OutDir -eq '') { $OutDir = Join-Path $Repo 'package\Aplus' }

if (-not (Test-Path (Join-Path $Repo 'build\aplus.exe'))) {
    throw 'build\aplus.exe not found - configure and build with CMake first'
}

if (Test-Path $OutDir) { Remove-Item $OutDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# 1. exe + Qt runtime next to it
Copy-Item (Join-Path $Repo 'build\aplus.exe') (Join-Path $OutDir 'aplus.exe')
$env:PATH = $qtBin + ';' + $MingwRoot + '\bin;' + $env:PATH
$prevEap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'   # windeployqt writes warnings to stderr
& (Join-Path $qtBin 'windeployqt.exe') --release --no-compiler-runtime --no-opengl-sw (Join-Path $OutDir 'aplus.exe') 2>&1 |
    Select-Object -Last 2
$ErrorActionPreference = $prevEap

# 2. workspace template
New-Item -ItemType Directory -Force -Path (Join-Path $OutDir 'workspace') | Out-Null
Copy-Item (Join-Path $Repo 'workspace\main.cpp') (Join-Path $OutDir 'workspace\main.cpp')

# 3. trimmed g++ toolchain (headers, libs, cc1plus, binutils)
$tc = Join-Path $OutDir 'toolchain\mingw64'
$bin = Join-Path $tc 'bin'
$gccVer = '13.1.0'
$gccRel = 'x86_64-w64-mingw32'
$libexec = Join-Path $tc ('libexec\gcc\' + $gccRel + '\' + $gccVer)
$libGcc = Join-Path $tc ('lib\gcc\' + $gccRel + '\' + $gccVer)

New-Item -ItemType Directory -Force -Path $bin, $libexec, $libGcc | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $tc 'lib') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $tc $gccRel) | Out-Null

$exes = @('g++.exe', 'c++.exe', 'x86_64-w64-mingw32-g++.exe', 'as.exe', 'ld.exe', 'ld.bfd.exe', 'objdump.exe', 'strip.exe')
foreach ($e in $exes) {
    $src = Join-Path $MingwRoot ('bin\' + $e)
    if (Test-Path $src) { Copy-Item $src (Join-Path $bin $e) }
}
Get-ChildItem (Join-Path $MingwRoot 'bin') -Filter '*.dll' | Copy-Item -Destination $bin

$libexecFiles = @('cc1plus.exe', 'collect2.exe', 'lto-wrapper.exe', 'liblto_plugin.dll', 'liblto_plugin.dll.a')
foreach ($f in $libexecFiles) {
    $src = Join-Path $MingwRoot ('libexec\gcc\' + $gccRel + '\' + $gccVer + '\' + $f)
    if (Test-Path $src) { Copy-Item $src (Join-Path $libexec $f) }
}

Copy-Item (Join-Path $MingwRoot ('lib\gcc\' + $gccRel + '\' + $gccVer + '\*')) $libGcc -Recurse -Force
Copy-Item (Join-Path $MingwRoot ($gccRel + '\include')) (Join-Path $tc $gccRel) -Recurse -Force
Copy-Item (Join-Path $MingwRoot ($gccRel + '\lib')) (Join-Path $tc $gccRel) -Recurse -Force
Copy-Item (Join-Path $MingwRoot 'lib\*.a') (Join-Path $tc 'lib') -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $MingwRoot 'lib\*.dll.a') (Join-Path $tc 'lib') -Force -ErrorAction SilentlyContinue

# 4. launcher
$launcher = @'
@echo off
set "ROOT=%~dp0"
set "PATH=%ROOT%toolchain\mingw64\bin;%PATH%"
set "GXX=%ROOT%toolchain\mingw64\bin\g++.exe"
cd /d "%ROOT%"
start "" "%ROOT%aplus.exe"
'@
[System.IO.File]::WriteAllText((Join-Path $OutDir 'Aplus.cmd'), ($launcher -replace "`r?`n", "`r`n"), [System.Text.Encoding]::ASCII)

# 5. readme (Chinese filename, UTF-8 with BOM)
$readmeSource = Join-Path $Repo 'tools\portable-readme.txt'
if (Test-Path $readmeSource) {
    $text = [System.IO.File]::ReadAllText($readmeSource, [System.Text.Encoding]::UTF8)
    $name = ([char]0x4F7F) + ([char]0x7528) + ([char]0x8BF4) + ([char]0x660E) + '.txt'
    [System.IO.File]::WriteAllText((Join-Path $OutDir $name), ($text -replace "`r?`n", "`r`n"),
        (New-Object System.Text.UTF8Encoding($true)))
}

$size = (Get-ChildItem $OutDir -Recurse -File | Measure-Object Length -Sum).Sum
'package: {0}' -f $OutDir
'total:   {0:N1} MB' -f ($size / 1MB)
