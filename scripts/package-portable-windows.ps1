# SPDX-License-Identifier: MIT
# Package a PREBUILT deployed Windows release without machine-specific paths.
# Source ZIP is NOT a runnable portable Windows bundle.
param([string]$BuildDirectory = '', [string]$OutputZip = '')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $root 'build-vs2026-qt' }
$release = Join-Path $BuildDirectory 'Release'
if (-not (Test-Path -LiteralPath (Join-Path $release 'ClickNova.exe') -PathType Leaf)) {
    throw 'Build and deploy ClickNova.exe before packaging.'
}
if (-not (Test-Path -LiteralPath (Join-Path $release 'Qt6Core.dll') -PathType Leaf) -or
    -not (Test-Path -LiteralPath (Join-Path $release 'platforms\qwindows.dll') -PathType Leaf)) {
    throw 'Qt runtime missing. Run scripts/build-windows.ps1 (windeployqt) first.'
}
# Require tests from the same build tree; otherwise refuse a release package.
if (-not (Test-Path -LiteralPath (Join-Path $BuildDirectory '.clicknova-tests-passed') -PathType Leaf)) {
    throw 'The Windows build has no passing CTest marker. Run all tests before packaging.'
}
foreach ($requiredDll in @('Qt6Widgets.dll', 'Qt6Gui.dll', 'Qt6Core.dll', 'Qt6Network.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $release $requiredDll) -PathType Leaf)) {
        throw ('Required Qt runtime is missing: ' + $requiredDll)
    }
}
if (-not $OutputZip) { $OutputZip = Join-Path $root 'ClickNova-Windows-x64-portable.zip' }
$stage = Join-Path ([IO.Path]::GetTempPath()) ('clicknova-portable-' + [Guid]::NewGuid().ToString('N'))
try {
    $target = Join-Path $stage 'ClickNova'
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    # Copy Qt deployment and associated runtime DLLs. Do not ship test executables,
    # source files, user data, logs or secrets in the distribution.
    Get-ChildItem -LiteralPath $release -File | Where-Object {
        $_.Name -eq 'ClickNova.exe' -or ($_.Extension -eq '.dll' -and $_.Name -notmatch '^(?:msvcp|vcruntime|concrt|ucrtbase)')
    } | Copy-Item -Destination $target
    foreach ($sub in @('platforms','styles','imageformats','iconengines','tls','networkinformation','generic','translations')) {
        $from = Join-Path $release $sub
        if (Test-Path -LiteralPath $from -PathType Container) {
            Copy-Item -LiteralPath $from -Destination (Join-Path $target $sub) -Recurse -Force
        }
    }
    Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $target 'LICENSE')
    foreach ($browser in @('chromium','firefox')) {
        $source = Join-Path $root ('extensions\' + $browser)
        if (Test-Path $source) {
            New-Item -ItemType Directory -Path (Join-Path $target 'extensions') -Force | Out-Null
            Copy-Item -LiteralPath $source -Destination (Join-Path $target 'extensions') -Recurse
        }
    }
    New-Item -ItemType Directory -Path (Join-Path $target 'data\tessdata') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $target 'README-PORTABLE.txt') -Encoding UTF8 -Value @'
ClickNova — portable Windows x64 application / портативна програма для Windows x64.

ENGLISH
1. Run ClickNova.exe. You may move the ClickNova folder to another disk.
2. Settings, scenarios and recordings live in data/ next to the executable when writable.
3. The program needs the shipped Qt plugins and QML imports; keep the entire folder together.
4. OpenCV/Tesseract features work only when those optional dependencies were enabled at build time.
5. A clean Windows installation may require Microsoft's official Visual C++ Redistributable x64.
6. Never distribute Visual Studio or third-party DLLs without their redistribution rights.
7. Linux requires a separate build; the Windows executable does not run natively on Linux.

УКРАЇНСЬКА
1. Запустіть ClickNova.exe. Папку ClickNova можна перенести на інший диск.
2. Налаштування, сценарії та записи зберігаються в data/ поряд із програмою, якщо є доступ до запису.
3. Не вилучайте бібліотеки Qt, плагіни та QML-модулі з портативної папки.
4. Функції OpenCV/Tesseract працюють лише тоді, коли їх було ввімкнено під час збирання.
5. На чистій системі Windows може знадобитися офіційний Microsoft Visual C++ Redistributable x64.
6. Дотримуйтеся ліцензій та дозволів на поширення сторонніх компонентів.
7. Для Linux потрібне окреме збирання. EXE для Windows не є нативною програмою Linux.
'@
    # Bundle a tightly scoped uninstaller. The uninstall manifest is generated
    # only for files actually shipped in THIS portable ZIP (never system files).
    foreach ($script in @('uninstall-clicknova.ps1','Uninstall-ClickNova.cmd')) {
        Copy-Item -LiteralPath (Join-Path $root (Join-Path 'scripts' $script)) -Destination (Join-Path $target $script)
    }
    $records = New-Object 'System.Collections.Generic.List[string]'
    Get-ChildItem -LiteralPath $target -Recurse -File | ForEach-Object {
        if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw ('Symlink/junction in portable package: ' + $_.FullName)
        }
        $rel = $_.FullName.Substring($target.Length + 1).Replace('\', '/')
        if ($rel -match '[\t\r\n]' -or $rel -match '(^|/)\.\.?(/|$)') {
            throw ('Unsafe archive path: ' + $rel)
        }
        if ($rel -notin @('Uninstall-ClickNova.cmd','uninstall-clicknova.ps1')) {
            $sha = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            $records.Add($sha + [char]9 + $rel)
        }
    }
    $ledger = Join-Path $target 'install-manifest.tsv'
    $manifestLines = New-Object 'System.Collections.Generic.List[string]'
    $manifestLines.Add('# ClickNova portable manifest v1')
    foreach ($record in $records) { $manifestLines.Add($record) }
    [IO.File]::WriteAllLines($ledger, [string[]]$manifestLines, (New-Object System.Text.UTF8Encoding($false)))
    if (Test-Path -LiteralPath $OutputZip) { Remove-Item -LiteralPath $OutputZip -Force }
    Compress-Archive -LiteralPath $target -DestinationPath $OutputZip -CompressionLevel Optimal
    Write-Host "Portable Windows ZIP created: $OutputZip"
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Force -Recurse }
}
