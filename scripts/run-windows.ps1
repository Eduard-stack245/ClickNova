# SPDX-License-Identifier: Apache-2.0
# One-click developer launcher: rebuild only when a source file changed.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
try {
    $root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
    $exe = Join-Path $root 'build-vs2026-qt\Release\ClickNova.exe'
    $qtPlatform = Join-Path $root 'build-vs2026-qt\Release\platforms\qwindows.dll'
    $rebuild = -not ((Test-Path -LiteralPath $exe -PathType Leaf) -and
                     (Test-Path -LiteralPath $qtPlatform -PathType Leaf))
    if (-not $rebuild) {
        $modified = (Get-Item -LiteralPath $exe).LastWriteTimeUtc
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
        & (Join-Path $PSScriptRoot 'build-windows.ps1')
        if ($LASTEXITCODE -ne 0) { throw "Build failed (exit code $LASTEXITCODE)." }
    }
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "ClickNova.exe was not generated: $exe"
    }
    Write-Host 'Starting ClickNova...' -ForegroundColor Green
    Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Path $exe -Parent)
} catch {
    Write-Host "ClickNova launcher error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
