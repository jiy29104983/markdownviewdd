param(
    [string]$QtRoot = "",
    [string]$Generator = "Visual Studio 16 2019",
    [string]$Toolset = "",
    [switch]$Clean,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"

if (-not $SkipBuild) {
    $BuildArguments = @{
        QtRoot = $QtRoot
        Generator = $Generator
        Toolset = $Toolset
        Clean = $Clean
    }
    & (Join-Path $PSScriptRoot "build-windows.ps1") @BuildArguments
}

if (-not (Test-Path (Join-Path $BuildDir "CTestTestfile.cmake") -PathType Leaf)) {
    throw "CTest metadata was not found in '$BuildDir'. Run without -SkipBuild or configure with -DBUILD_TESTING=ON."
}

$CTestCommand = Get-Command "ctest.exe" -ErrorAction SilentlyContinue
$CTestExe = if ($CTestCommand) { $CTestCommand.Source } else { $null }
if (-not $CTestExe) {
    $CMakeCommand = Get-Command "cmake.exe" -ErrorAction SilentlyContinue
    if ($CMakeCommand) {
        $CTestCandidate = Join-Path (Split-Path -Parent $CMakeCommand.Source) "ctest.exe"
        if (Test-Path $CTestCandidate -PathType Leaf) {
            $CTestExe = $CTestCandidate
        }
    }
}
if (-not $CTestExe) {
    throw "ctest.exe was not found. Install CMake or use a Visual Studio installation that includes CMake tools."
}

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    if (-not [string]::IsNullOrWhiteSpace($env:QTDIR)) {
        $QtRoot = $env:QTDIR
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:QT_ROOT_DIR)) {
        $QtRoot = $env:QT_ROOT_DIR
    }
    else {
        $QtRoot = "C:\Qt\5.15.2\msvc2019_64"
    }
}
$QtRoot = [System.IO.Path]::GetFullPath($QtRoot)
$env:PATH = "$(Join-Path $QtRoot 'bin');$env:PATH"

Write-Host "Running Qt behavior regression tests..."
& $CTestExe --test-dir $BuildDir -C Release --output-on-failure --no-tests=error
if ($LASTEXITCODE -ne 0) {
    throw "Qt behavior regression tests failed with exit code $LASTEXITCODE"
}

$BashCommand = Get-Command "bash.exe" -ErrorAction SilentlyContinue
if (-not $BashCommand) {
    throw "bash.exe was not found. Git for Windows is required to run the isolated release publishing tests."
}

Write-Host "Running isolated release publishing tests..."
Push-Location $ProjectRoot
try {
    & $BashCommand.Source "./tests/release_publish_test.sh"
    if ($LASTEXITCODE -ne 0) {
        throw "Release publishing tests failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}

Write-Host "All regression tests passed."
