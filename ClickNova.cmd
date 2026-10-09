@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-windows.ps1"
if errorlevel 1 (
    echo.
    echo ClickNova could not start. Review the error above.
    pause
)
endlocal
