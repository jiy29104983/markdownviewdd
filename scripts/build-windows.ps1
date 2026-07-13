param(
    [string]$QtRoot = "",

    [string]$Generator = "Visual Studio 16 2019",

    [string]$InstallDir = "",

    [switch]$Clean
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:QTDIR)) {
        $QtRoot = $env:QTDIR
    }
    else {
        $QtRoot = "C:\Qt\5.15.2\msvc2019_64"
    }
}

$QtRoot = [System.IO.Path]::GetFullPath($QtRoot)
$QtConfig = Join-Path $QtRoot "lib\cmake\Qt5\Qt5Config.cmake"
if (-not (Test-Path $QtConfig -PathType Leaf)) {
    throw "Qt 5.15.2 msvc2019_64 was not found at '$QtRoot'. Pass -QtRoot with the correct path or set QTDIR."
}

$CMakeCommand = Get-Command "cmake.exe" -ErrorAction SilentlyContinue
$CMakeExe = if ($CMakeCommand) { $CMakeCommand.Source } else { $null }
if (-not $CMakeExe) {
    $CMakeCandidates = @(
        (Join-Path $env:ProgramFiles "CMake\bin\cmake.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2019\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2019\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"),
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
    )
    $CMakeExe = $CMakeCandidates |
        Where-Object { $_ -and (Test-Path $_ -PathType Leaf) } |
        Select-Object -First 1
}
if (-not $CMakeExe) {
    throw "cmake.exe was not found. Install CMake or the CMake tools included with Visual Studio 2019."
}

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Removing old build directory: $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

$env:QTDIR = $QtRoot
$env:PATH = "$(Join-Path $QtRoot 'bin');$env:PATH"

Write-Host "Project:   $ProjectRoot"
Write-Host "Qt:        $QtRoot"
Write-Host "Generator: $Generator"
Write-Host "CMake:     $CMakeExe"
Write-Host ""

& $CMakeExe `
    -S $ProjectRoot `
    -B $BuildDir `
    -G $Generator `
    -A x64 `
    "-DCMAKE_PREFIX_PATH=$QtRoot"

if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

& $CMakeExe --build $BuildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE"
}

$PluginDll = Join-Path $BuildDir "plugin\markdownviewdd.dll"
if (-not (Test-Path $PluginDll)) {
    throw "Build completed but $PluginDll was not found"
}

Write-Host ""
Write-Host "Built: $PluginDll"

if ($InstallDir) {
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Copy-Item -Force $PluginDll (Join-Path $InstallDir "markdownviewdd.dll")
    Write-Host "Installed: $(Join-Path $InstallDir 'markdownviewdd.dll')"
}
