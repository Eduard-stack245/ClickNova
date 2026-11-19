@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
echo.
echo ClickNova 0.6.0 - Windows x64 Portable Builder
echo This packages your already-built application. It does not reinstall dependencies.
echo.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-ClickNova-Portable.ps1" %*
set "CLICKNOVA_EXIT=%ERRORLEVEL%"
echo.
if not "%CLICKNOVA_EXIT%"=="0" (
    echo Portable creation FAILED. No existing archive was overwritten.
) else (
    echo Portable creation completed. See the ZIP path above.
)
pause
exit /b %CLICKNOVA_EXIT%
