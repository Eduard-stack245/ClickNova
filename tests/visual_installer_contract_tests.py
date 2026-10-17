#!/usr/bin/env python3
# ClickNova installer contract checks; intentionally no network, writes or installs.
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
worker = (root / 'scripts/enable-visual-recognition.ps1').read_text(encoding='utf-8-sig')
gui = (root / 'scripts/visual-recognition-installer.ps1').read_text(encoding='utf-8-sig')
batch = (root / 'Enable-OpenCV-OCR.cmd').read_text()
qt = (root / 'src/gui/optional_components.cpp').read_text()
build = (root / 'scripts/build-windows.ps1').read_text()
ignore = (root / '.gitignore').read_text()
precommit = (root / 'scripts/check-commit.ps1').read_text()
source_package = (root / 'scripts/package-source.ps1').read_text()

assert all(x in worker for x in ('scripts\\bootstrap.ps1', 'scripts\\buildsystems\\vcpkg.cmake', 'bootstrap-vcpkg.bat'))
assert worker.index('Test-CompleteCheckout $chosen') < worker.index("bootstrap-vcpkg.bat')")
assert 'Preserving the pre-existing tools/vcpkg checkout' in worker and 'Get-ManagedRoot' in worker
assert 'Qt6Config.cmake' in worker
assert 'Test-ReadyVcpkg' in worker and 'clicknova-visual-vcpkg' in worker
assert 'FileShare]::None' in worker and 'LockBusy' in worker
assert 'DownloadFile' in worker and 'tessdata_fast' in worker
assert 'OpenCV enabled' in worker and 'Tesseract OCR enabled' in worker
assert '::CLICKNOVA_RESULT::SUCCESS' in worker and '::CLICKNOVA_RESULT::FAILED' in worker
assert 'Install-OcrModels $project' in worker
assert not re.search(r'Remove-Item.*(?:tools\\vcpkg|vcpkg-running\.lock)', worker, re.I)
assert all(x in gui for x in ('ProgressBar', 'Marquee', 'Start-Process', 'Poll-InstallerLog', 'Finish-Installer', 'Повторить', 'Отменить'))
assert 'System.System.' not in gui
assert 'New-Object Drawing.' not in gui
assert 'New-Object Text.UTF8Encoding(' not in worker
assert 'New-Object Windows.Forms' not in gui
assert 'New-Object IO.FileStream' not in gui and 'New-Object IO.FileStream' not in worker
assert 'ShowDialog()' in gui and 'Get-Content -LiteralPath $state.Err' in gui
assert 'visual-recognition-installer.ps1' in batch and 'visual-recognition-installer.ps1' in qt
assert 'tools\\clicknova-visual-vcpkg' in build
assert '/tools/clicknova-visual-vcpkg*/' in ignore and '/tools/clicknova-visual-installer.lock' in ignore
assert 'clicknova-visual-vcpkg' in precommit
assert 'visual-recognition-installer.ps1' in source_package

# Windows PowerShell 5.1 must distinguish native stderr warnings (exit 0)
# from actual tool failures (nonzero exit), while keeping installer progress.
assert 'function Invoke-ClickNovaNative' in build
assert "$ErrorActionPreference = 'Continue'" in build
assert '2>&1 | ForEach-Object' in build
assert '$ExitCode.Value = [int]$LASTEXITCODE' in build
assert all(v in build for v in ("if ($nativeExit -ne 0)", "Invoke-ClickNovaNative 'cmake'", "Invoke-ClickNovaNative 'ctest'"))
assert "if (-not (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt') -PathType Leaf))" in build
assert "& powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File" in worker
assert 'if ($code -ne 0)' in worker and 'if (-not $buildResult.OpenCV -or -not $buildResult.OCR)' in worker

# The worker must persist a success token after verified build/CTest and OCR
# language processing. The UI must use the token from this invocation only,
# tolerate redirected-stdout flush lag, and never classify FAILED as success.
assert "[string]$ResultFile = ''" in worker
assert worker.index('Install-OcrModels $project') < worker.index("Set-Content -LiteralPath $resultPath -Value 'SUCCESS'")
assert worker.index("Set-Content -LiteralPath $resultPath -Value 'SUCCESS'") < worker.index("Write-Host '::CLICKNOVA_RESULT::SUCCESS'")
assert "StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)" in worker
assert "[guid]::NewGuid().ToString('N').Substring(0,8)" in gui
assert "-ResultFile" in gui and "$state.Result = Join-Path $logDir" in gui
assert 'function Test-InstallerSuccess' in gui and "if ($result -ceq 'SUCCESS')" in gui
assert "if ($result -ceq 'FAILED')" in gui
assert '$state.Cancelled -or $state.SeenFailure' in gui
assert '([DateTime]::UtcNow - $state.ExitObservedAt).TotalSeconds -ge 8' in gui
assert "if ($confirmed)" in gui and "Poll-InstallerLog" in gui
assert 'Диагностика: код процесса' in gui
assert gui.count('::CLICKNOVA_RESULT::FAILED') >= 2
print('visual installer contract: passed')
