# ClickNova Windows x64 portable builder, PowerShell 5.1-compatible.
# SPDX-License-Identifier: Apache-2.0
# Run OUTSIDE the checkout. This script does not install software or modify source/data.
[CmdletBinding()]
param(
    [string]$ProjectRoot = '',
    [switch]$NoOcrModels,
    [switch]$SkipSmokeTest
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$stage = ''
$pendingZip = ''
$completed = $false
try {
    if ([string]::IsNullOrWhiteSpace($ProjectRoot)) {
        $ProjectRoot = Read-Host 'Enter your ClickNova source directory (with CMakeLists.txt)'
    }
    $ProjectRoot = $ProjectRoot.Trim().Trim('"')
    if ([string]::IsNullOrWhiteSpace($ProjectRoot)) { throw 'No ClickNova source directory was provided.' }
    if (-not (Test-Path -LiteralPath $ProjectRoot -PathType Container)) {
        throw ('Source directory does not exist: ' + $ProjectRoot)
    }
    $root = (Resolve-Path -LiteralPath $ProjectRoot).Path
    $packager = Join-Path $root 'scripts\package-portable-windows.ps1'
    $build = Join-Path $root 'build-vs2026-qt'
    $release = Join-Path $build 'Release'
    $sourceFiles = @('CMakeLists.txt', 'LICENSE', 'scripts\package-portable-windows.ps1', 'scripts\uninstall-clicknova.ps1', 'scripts\Uninstall-ClickNova.cmd')
    foreach ($item in $sourceFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $root $item) -PathType Leaf)) {
            throw ('Missing project source file: ' + $item)
        }
    }
    $requiredRuntime = @('ClickNova.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
                         'Qt6Network.dll', 'opencv_core4.dll', 'opencv_imgproc4.dll',
                         'opencv_imgcodecs4.dll', 'tesseract55.dll')
    foreach ($file in $requiredRuntime) {
        if (-not (Test-Path -LiteralPath (Join-Path $release $file) -PathType Leaf)) {
            throw ('Missing deployed application component: ' + $file + '. Run Enable-OpenCV-OCR.cmd first.')
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $release 'platforms\qwindows.dll') -PathType Leaf)) {
        throw 'Qt Windows platform plugin is missing. Run Enable-OpenCV-OCR.cmd first.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $build '.clicknova-tests-passed') -PathType Leaf)) {
        throw 'Test success marker missing. Run Enable-OpenCV-OCR.cmd to build and pass CTest first.'
    }

    $dist = Join-Path $root 'dist-portable'
    $archiveName = 'ClickNova-0.6.0-Windows-x64-portable.zip'
    $finalZip = Join-Path $dist $archiveName
    $hashFile = Join-Path $dist 'ClickNova-0.6.0-Windows-x64-portable.sha256'
    if ((Test-Path -LiteralPath $finalZip) -or (Test-Path -LiteralPath $hashFile)) {
        throw ('Portable ZIP or checksum already exists. Move existing files first: ' + $dist)
    }
    if (-not (Test-Path -LiteralPath $dist -PathType Container)) {
        New-Item -ItemType Directory -Path $dist -Force | Out-Null
    }
    $guid = [Guid]::NewGuid().ToString('N')
    $stage = Join-Path ([IO.Path]::GetTempPath()) ('clicknova-portable-builder-' + $guid)
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    $baseZip = Join-Path $stage 'build.zip'
    $extract = Join-Path $stage 'unpacked'
    $pendingZip = Join-Path $dist ('.clicknova-' + $guid + '.zip')

    Write-Host '[1/5] Packaging deployed Windows application and DLLs ...' -ForegroundColor Cyan
    & $packager -BuildDirectory $build -OutputZip $baseZip
    if (-not (Test-Path -LiteralPath $baseZip -PathType Leaf)) { throw 'Built-in packager did not create the ZIP.' }
    Expand-Archive -LiteralPath $baseZip -DestinationPath $extract
    $portable = Join-Path $extract 'ClickNova'
    if (-not (Test-Path -LiteralPath (Join-Path $portable 'ClickNova.exe') -PathType Leaf)) {
        throw 'The packaged application executable is missing.'
    }

    Write-Host '[2/5] Preparing OCR languages for offline use ...' -ForegroundColor Cyan
    $modelCount = 0
    if (-not $NoOcrModels) {
        $modelParent = Join-Path $root 'data\tessdata'
        $targetParent = Join-Path $portable 'data\tessdata'
        $ledger = Join-Path $portable 'install-manifest.tsv'
        if (-not (Test-Path -LiteralPath $ledger -PathType Leaf)) { throw 'Portable uninstall manifest is missing.' }
        foreach ($name in @('eng', 'rus')) {
            $fileName = $name + '.traineddata'
            $source = Join-Path $modelParent $fileName
            if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
                Write-Host ('  ' + $fileName + ' not available; can be downloaded in Settings > Components.') -ForegroundColor Yellow
                continue
            }
            # Prevent exporting links to external/private locations and reject implausible model files.
            $unsafe = $false
            foreach ($candidate in @((Join-Path $root 'data'), $modelParent, $source)) {
                if ((Test-Path -LiteralPath $candidate) -and
                    ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                    $unsafe = $true
                }
            }
            if ($unsafe) { throw ('Refusing linked or redirected OCR file: ' + $source) }
            $size = (Get-Item -LiteralPath $source).Length
            if ($size -lt 65536 -or $size -gt 104857600) {
                throw ('Unusual OCR model file size; refusing to package: ' + $source)
            }
            if (-not (Test-Path -LiteralPath $targetParent -PathType Container)) {
                New-Item -ItemType Directory -Path $targetParent -Force | Out-Null
            }
            $destination = Join-Path $targetParent $fileName
            if (Test-Path -LiteralPath $destination) { throw ('Unexpected duplicate OCR model: ' + $fileName) }
            Copy-Item -LiteralPath $source -Destination $destination
            $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
            [IO.File]::AppendAllText($ledger, ($hash + [char]9 + 'data/tessdata/' + $fileName + [Environment]::NewLine),
                                     (New-Object System.Text.UTF8Encoding($false)))
            Write-Host ('  Bundled offline OCR language: ' + $name)
            $modelCount++
        }
    } else {
        Write-Host '  OCR model inclusion disabled; they can be downloaded on first use.'
    }
    Write-Host ('  OCR models included: ' + $modelCount)

    Write-Host '[3/5] Building final ZIP ...' -ForegroundColor Cyan
    Compress-Archive -LiteralPath $portable -DestinationPath $pendingZip -CompressionLevel Optimal
    if (-not (Test-Path -LiteralPath $pendingZip -PathType Leaf)) { throw 'Final ZIP not created.' }

    Write-Host '[4/5] Inspecting ZIP contents for user data and required files ...' -ForegroundColor Cyan
    Add-Type -AssemblyName 'System.IO.Compression'
    Add-Type -AssemblyName 'System.IO.Compression.FileSystem'
    $zip = [IO.Compression.ZipFile]::OpenRead($pendingZip)
    try {
        $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName.Replace('\', '/')
            if ($name.EndsWith('/')) { continue }
            if (-not $seen.Add($name)) { throw ('Duplicate ZIP file: ' + $name) }
            if (-not $name.StartsWith('ClickNova/', [StringComparison]::Ordinal) -or
                $name.Contains('../') -or $name.Contains('/./') -or $name.Contains(':') -or
                $name -match '[\x00-\x1f]') { throw ('Unsafe ZIP file name: ' + $name) }
            $relative = $name.Substring('ClickNova/'.Length)
            $allowed = ($relative -match '^[^/]+\.(dll|exe)$' -or
                $relative -in @('LICENSE', 'README-PORTABLE.txt', 'install-manifest.tsv',
                                'Uninstall-ClickNova.cmd', 'uninstall-clicknova.ps1') -or
                $relative -match '^(platforms|styles|imageformats|iconengines|tls|networkinformation|generic)/[^/]+\.dll$' -or
                $relative -match '^translations/[^/]+\.qm$' -or
                $relative -match '^extensions/(chromium|firefox)/(manifest\.json|background\.js|options\.html|options\.js)$' -or
                $relative -match '^data/tessdata/(eng|rus)\.traineddata$')
            if (-not $allowed) { throw ('Unexpected or private file in portable ZIP: ' + $name) }
        }
        foreach ($required in @('ClickNova/ClickNova.exe', 'ClickNova/Qt6Core.dll',
                                'ClickNova/Qt6Gui.dll', 'ClickNova/Qt6Widgets.dll',
                                'ClickNova/Qt6Network.dll', 'ClickNova/platforms/qwindows.dll',
                                'ClickNova/opencv_core4.dll', 'ClickNova/tesseract55.dll',
                                'ClickNova/install-manifest.tsv', 'ClickNova/LICENSE')) {
            if (-not $seen.Contains($required)) { throw ('Required file missing from portable ZIP: ' + $required) }
        }
        foreach ($lang in @('eng', 'rus')) {
            $entryName = 'ClickNova/data/tessdata/' + $lang + '.traineddata'
            if ($seen.Contains($entryName)) { Write-Host ('  Verified model: ' + $lang) }
        }
        Write-Host ('  ZIP file entries: ' + $seen.Count)
    } finally { $zip.Dispose() }

    Write-Host '[5/5] Testing copied portable EXE ...' -ForegroundColor Cyan
    if (-not $SkipSmokeTest) {
        $smoke = Join-Path $stage 'smoke'
        Expand-Archive -LiteralPath $pendingZip -DestinationPath $smoke
        $smokeDir = Join-Path $smoke 'ClickNova'
        $smokeExe = Join-Path $smokeDir 'ClickNova.exe'
        $process = Start-Process -FilePath $smokeExe -ArgumentList '--smoke-test-ui' -WorkingDirectory $smokeDir -PassThru
        try {
            if (-not $process.WaitForExit(60000)) {
                try { $process.Kill() } catch { }
                throw 'Portable GUI smoke test timed out after 60 seconds.'
            }
            $process.Refresh()
            if ($process.ExitCode -ne 0) { throw ('Portable GUI smoke test failed with exit code ' + $process.ExitCode) }
        } finally { $process.Dispose() }
        Write-Host '  Standalone GUI smoke test passed.' -ForegroundColor Green
    } else {
        Write-Host '  Smoke test skipped by request.' -ForegroundColor Yellow
    }

    Move-Item -LiteralPath $pendingZip -Destination $finalZip
    $pendingZip = ''
    $archiveHash = (Get-FileHash -LiteralPath $finalZip -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText($hashFile, ($archiveHash + '  ' + $archiveName + [Environment]::NewLine),
                            (New-Object System.Text.UTF8Encoding($false)))
    Write-Host ''
    Write-Host 'SUCCESS: Windows x64 ClickNova Portable is ready.' -ForegroundColor Green
    Write-Host ('ZIP: ' + $finalZip) -ForegroundColor Green
    Write-Host ('SHA-256: ' + $archiveHash)
    Write-Host ('OCR models included: ' + $modelCount)
    Write-Host 'No source files, personal scenarios, settings, vcpkg or build tree included.'
    Write-Host 'Note: depending on the target computer, a Microsoft VC++ Redistributable x64 may be needed.'
} catch {
    Write-Host ''
    Write-Host ('PORTABLE BUILD ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host 'Existing portable archives and user data were not overwritten.' -ForegroundColor Yellow
    exit 1
} finally {
    if ($pendingZip -and (Test-Path -LiteralPath $pendingZip)) {
        Remove-Item -LiteralPath $pendingZip -Force -ErrorAction SilentlyContinue
    }
    if ($stage -and (Test-Path -LiteralPath $stage -PathType Container)) {
        Remove-Item -LiteralPath $stage -Force -Recurse -ErrorAction SilentlyContinue
    }
}
