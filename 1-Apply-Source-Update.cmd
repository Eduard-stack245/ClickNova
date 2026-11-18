@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Apply-Clean-Portable-Update.ps1" %*
set "ret=%ERRORLEVEL%"
if not "%ret%"=="0" echo Update was NOT applied. Review the error above.
pause
exit /b %ret%
