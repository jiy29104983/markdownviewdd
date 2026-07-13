@echo off
setlocal

cd /d "%~dp0"

echo Building markdownviewdd...
echo.

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass ^
  -File "%~dp0scripts\build-windows.ps1" %*

set "BUILD_EXIT_CODE=%ERRORLEVEL%"
echo.

if not "%BUILD_EXIT_CODE%"=="0" (
    echo Build failed with exit code %BUILD_EXIT_CODE%.
    echo Check the error message above.
    pause
    exit /b %BUILD_EXIT_CODE%
)

echo Build succeeded.
echo DLL: %~dp0build\plugin\markdownviewdd.dll
pause
exit /b 0
