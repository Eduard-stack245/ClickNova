# SPDX-License-Identifier: Apache-2.0
# One-click developer launcher: rebuild only when a source file changed.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
try {
    $root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
    $exe = Join-Path $root 'build-vs2026-qt\Release\ClickNova.exe'
    $qtPlatform = Join-Path $root 'build-vs2026-qt\Release\platforms\qwindows.dll'
    $validated = Join-Path $root 'build-vs2026-qt\.clicknova-tests-passed'
    $rebuild = -not ((Test-Path -LiteralPath $exe -PathType Leaf) -and
                     (Test-Path -LiteralPath $qtPlatform -PathType Leaf) -and
                     (Test-Path -LiteralPath $validated -PathType Leaf))
    if (-not $rebuild) {
        # A built EXE is not proof that CTest passed. Compare against the
        # validation marker, written only after a fully successful build.
        $modified = (Get-Item -LiteralPath $validated).LastWriteTimeUtc
        foreach ($item in @('src', 'include', 'scripts', 'tests', 'CMakeLists.txt')) {
            $path = Join-Path $root $item
            $files = if (Test-Path $path -PathType Container) {
                Get-ChildItem -LiteralPath $path -Recurse -File
            } else {
                Get-Item -LiteralPath $path
            }
            foreach ($file in $files) {
                if ($file.LastWriteTimeUtc -gt $modified) { $rebuild = $true; break }
            }
            if ($rebuild) { break }
        }
    }
    if ($rebuild) {
        Write-Host 'ClickNova: building the updated application...' -ForegroundColor Cyan
        $log = Join-Path $root 'build-vs2026-qt\last-build.log'
        New-Item -ItemType Directory -Path (Split-Path $log -Parent) -Force | Out-Null
        $transcribing = $false
        try {
            Start-Transcript -Path $log -Force | Out-Null
            $transcribing = $true
            & (Join-Path $PSScriptRoot 'build-windows.ps1')
            if ($LASTEXITCODE -ne 0) { throw "Build failed (exit code $LASTEXITCODE)." }
        } finally {
            if ($transcribing) { Stop-Transcript | Out-Null }
        }
        Write-Host "Build log saved to $log" -ForegroundColor Cyan
    }
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "ClickNova.exe was not generated: $exe"
    }
    Write-Host 'Starting ClickNova...' -ForegroundColor Green
    Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Path $exe -Parent)
} catch {
    Write-Host "ClickNova launcher error: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "If a build was started, see build-vs2026-qt\last-build.log for the full output." -ForegroundColor Yellow
    exit 1
}
