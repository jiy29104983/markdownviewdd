param(
    [string]$QtRoot = $env:QTDIR,

    [string]$Generator = "Visual Studio 16 2019",

    [string]$InstallDir = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    throw "QtRoot is required. Pass -QtRoot or set the QTDIR environment variable."
}

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"

& cmake `
    -S $ProjectRoot `
    -B $BuildDir `
    -G $Generator `
    -A x64 `
    "-DCMAKE_PREFIX_PATH=$QtRoot"

if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

& cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE"
}

$PluginDll = Join-Path $BuildDir "plugin\markdownviewdd.dll"
if (-not (Test-Path $PluginDll)) {
    throw "Build completed but $PluginDll was not found"
}

Write-Host "Built: $PluginDll"

if ($InstallDir) {
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Copy-Item -Force $PluginDll (Join-Path $InstallDir "markdownviewdd.dll")
    Write-Host "Installed: $(Join-Path $InstallDir 'markdownviewdd.dll')"
}
