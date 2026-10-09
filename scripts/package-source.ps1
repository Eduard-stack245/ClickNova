# SPDX-License-Identifier: Apache-2.0
# Package only source files with flat archive roots for updating an existing ClickNova checkout.
param([string]$OutputPath = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& (Join-Path $PSScriptRoot 'verify-source-tree.ps1')

# The allowlist prevents build outputs, .git and local configuration from entering source archives.
$dirs = @('include', 'scripts', 'src', 'tests')
$files = @('.gitignore', 'CMakeLists.txt', 'LICENSE')
$versionMatch = [regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(ClickNova\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $versionMatch.Success) { throw 'ClickNova version not found in CMakeLists.txt.' }
if (-not $OutputPath) {
    $OutputPath = Join-Path (Split-Path $root -Parent) ("ClickNova-v{0}-source.zip" -f $versionMatch.Groups[1].Value)
}
$destination = [System.IO.Path]::GetFullPath($OutputPath)
if ([System.IO.Path]::GetDirectoryName($destination) -eq $root) {
    throw 'Write the package outside the source checkout.'
}
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ('clicknova-pack-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    foreach ($dir in $dirs) {
        Copy-Item -LiteralPath (Join-Path $root $dir) -Destination (Join-Path $stage $dir) -Recurse -Force
    }
    foreach ($file in $files) {
        Copy-Item -LiteralPath (Join-Path $root $file) -Destination (Join-Path $stage $file) -Force
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path $destination) { Remove-Item -LiteralPath $destination -Force }
    [System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $destination, [System.IO.Compression.CompressionLevel]::Optimal, $false)
    $zip = [System.IO.Compression.ZipFile]::OpenRead($destination)
    try {
        $entries = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        foreach ($item in @('scripts/build-windows.ps1', 'scripts/verify-source-tree.ps1', 'src/gui/main.cpp', 'CMakeLists.txt', '.gitignore')) {
            if ($entries -notcontains $item) { throw "Package is missing $item" }
        }
        if (@($entries | Where-Object { $_ -match '(^|/)\.git/|(^|/)build[^/]*/|(^|/)dist-windows/' }).Count -gt 0) {
            throw 'Package contains generated or Git metadata.'
        }
    } finally { $zip.Dispose() }
    Write-Host "Verified source package: $destination" -ForegroundColor Green
} finally {
    if (Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
