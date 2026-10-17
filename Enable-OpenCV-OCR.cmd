@echo off
setlocal
REM ClickNova optional visual recognition installer (opens a native progress window).
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\visual-recognition-installer.ps1" %*
exit /b %errorlevel%
