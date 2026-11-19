# SPDX-License-Identifier: Apache-2.0
# Build and verify a ClickNova clean-layout portable archive on the user's Windows PC.
[CmdletBinding()]
param([string]$ProjectRoot = '', [switch]$SkipRebuild)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$tmp = ''
try {
    if (-not $ProjectRoot) { $ProjectRoot = Read-Host 'Enter ClickNova source directory' }
    $root = (Resolve-Path -LiteralPath $ProjectRoot).Path
    $packager = Join-Path $root 'scripts\package-portable-windows.ps1'
    if (-not (Test-Path -LiteralPath $packager -PathType Leaf)) { throw 'Portable packager missing.' }
    $storageSource = Join-Path $root 'src\gui\portable_storage.cpp'
    $sourceText = [IO.File]::ReadAllText($storageSource)
    if (-not $sourceText.Contains('.clicknova-portable-v2')) { throw 'Apply the clean layout source patch first.' }
    $build = Join-Path $root 'build-vs2026-qt'
    if (-not $SkipRebuild) {
        Write-Host '[1/4] Building and testing ClickNova with portable v2 data paths ...' -ForegroundColor Cyan
        & (Join-Path $root 'scripts\build-windows.ps1')
        if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw 'Windows build failed.' }
    } else {
        Write-Host '[1/4] Rebuild skipped by explicit request. Checking binary timestamps.' -ForegroundColor Yellow
    }
    if (-not (Test-Path -LiteralPath (Join-Path $build '.clicknova-tests-passed') -PathType Leaf)) {
        throw 'Windows tests have not passed in this build tree.'
    }
    $dist = Join-Path $root 'dist-portable'
    if (-not (Test-Path -LiteralPath $dist -PathType Container)) {
        New-Item -Path $dist -ItemType Directory | Out-Null
    }
    $name = 'ClickNova-0.6.0-Windows-x64-portable-organized.zip'
    $output = Join-Path $dist $name
    $checksum = Join-Path $dist ($name + '.sha256')
    if ((Test-Path -LiteralPath $output) -or (Test-Path -LiteralPath $checksum)) {
        throw ('This build already exists. Move it elsewhere first; will not overwrite: ' + $output)
    }
    Write-Host '[2/4] Creating the clean portable layout ...' -ForegroundColor Cyan
    & $packager -BuildDirectory $build -OutputZip $output -IncludeOcrModels -RequireVisual
    if (-not (Test-Path -LiteralPath $output -PathType Leaf)) { throw 'Packager did not create the ZIP.' }
    Write-Host '[3/4] Testing the extracted root launcher (Qt/OpenCV/OCR dependencies) ...' -ForegroundColor Cyan
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ('clicknova-v2-smoke-' + [guid]::NewGuid().ToString('N'))
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($output, $tmp)
    $portableRoot = Join-Path $tmp 'ClickNova'
    $launcher = Join-Path $portableRoot 'ClickNova.exe'
    if (-not (Test-Path -LiteralPath (Join-Path $portableRoot 'data\tessdata\eng.traineddata') -PathType Leaf)) {
        Write-Host 'English OCR model not bundled. It can be downloaded from Settings.' -ForegroundColor Yellow
    }
    if (-not (Test-Path -LiteralPath (Join-Path $portableRoot 'data\tessdata\rus.traineddata') -PathType Leaf)) {
        Write-Host 'Russian OCR model not bundled. It can be downloaded from Settings.' -ForegroundColor Yellow
    }
    $process = Start-Process -FilePath $launcher -ArgumentList '--smoke-test-ui' -WorkingDirectory $portableRoot -PassThru
    try {
        if (-not $process.WaitForExit(60000)) {
            try { $process.Kill() } catch { }
            throw 'Portable GUI smoke test did not exit within 60 seconds.'
        }
        $process.Refresh()
        if ($process.ExitCode -ne 0) { throw ('Portable smoke test failed, exit code ' + $process.ExitCode) }
    } finally { $process.Dispose() }
    Write-Host '[4/4] Verifying user data directory and writing checksum ...' -ForegroundColor Cyan
    $actualData = Join-Path $portableRoot 'data'
    if (-not (Test-Path -LiteralPath $actualData -PathType Container)) { throw 'User data folder missing.' }
    if (Test-Path -LiteralPath (Join-Path $portableRoot 'app\data')) {
        throw 'Application wrote to app/data instead of the portable data/ folder. Rebuild with the patch.'
    }
    $hash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText($checksum, ($hash + '  ' + $name + [Environment]::NewLine),
                            (New-Object System.Text.UTF8Encoding($false)))
    Write-Host ('SUCCESS. Organized Portable ZIP: ' + $output) -ForegroundColor Green
    Write-Host ('SHA-256: ' + $hash)
    Write-Host 'Root contains launcher, README and organized app, data, extensions, maintenance and docs.'
    Write-Host 'No settings, scenarios, private files or build dependencies were copied.'
} catch {
    Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host 'Personal data and existing archives were preserved.' -ForegroundColor Yellow
    exit 1
} finally {
    if ($tmp -and (Test-Path -LiteralPath $tmp -PathType Container)) {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
}
