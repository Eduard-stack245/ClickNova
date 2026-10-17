@echo off
REM SPDX-License-Identifier: Apache-2.0
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall-clicknova.ps1"
if errorlevel 1 (
  echo Some files were kept. Check the details above.
  pause
  exit /b 1
)
REM The PowerShell script may have removed this launcher already.
exit /b 0
