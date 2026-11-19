@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
echo ClickNova 0.6.0 - Clean Portable Layout v2
echo Source update must be applied before building.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-ClickNova-Clean-Portable.ps1" %*
set "ret=%ERRORLEVEL%"
if not "%ret%"=="0" echo Portable creation did not finish. Existing files were preserved.
pause
exit /b %ret%
