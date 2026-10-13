# SPDX-License-Identifier: Apache-2.0
# Package application source only; never include documentation, settings, or generated output.
param([string]$OutputPath = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& (Join-Path $PSScriptRoot 'verify-source-tree.ps1')

$versionMatch = [regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(ClickNova\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $versionMatch.Success) { throw 'ClickNova version not found in CMakeLists.txt.' }
if (-not $OutputPath) {
    $OutputPath = Join-Path (Split-Path $root -Parent) ("ClickNova-v{0}-clean-source.zip" -f $versionMatch.Groups[1].Value)
}
$destination = [System.IO.Path]::GetFullPath($OutputPath)
if ([System.IO.Path]::GetDirectoryName($destination) -eq $root) {
    throw 'Write the package outside the source checkout.'
}
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ('clicknova-pack-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    foreach ($dir in @('include', 'src', 'tests', 'scripts')) {
        $sourceDir = Join-Path $root $dir
        $extensions = if ($dir -eq 'scripts') { @('.ps1', '.cmake') } else { @('.cpp', '.hpp', '.h', '.in') }
        Get-ChildItem -LiteralPath $sourceDir -Recurse -File | Where-Object { $_.Extension -in $extensions } | ForEach-Object {
            $relative = $_.FullName.Substring($root.Length).TrimStart('\', '/')
            $copyTo = Join-Path $stage $relative
            New-Item -Path (Split-Path $copyTo -Parent) -ItemType Directory -Force | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $copyTo -Force
        }
    }
    foreach ($file in @('.gitattributes', '.gitignore', 'CMakeLists.txt', 'ClickNova.cmd', 'LICENSE')) {
        Copy-Item -LiteralPath (Join-Path $root $file) -Destination (Join-Path $stage $file) -Force
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Force }
    [System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $destination, [System.IO.Compression.CompressionLevel]::Optimal, $false)
    $zip = [System.IO.Compression.ZipFile]::OpenRead($destination)
    try {
        $entries = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        foreach ($item in @('scripts/build-windows.ps1', 'scripts/verify-source-tree.ps1',
                             'src/gui/main.cpp', 'CMakeLists.txt', '.gitignore', 'ClickNova.cmd', 'LICENSE')) {
            if ($entries -notcontains $item) { throw "Package is missing $item" }
        }
        $invalid = @($entries | Where-Object {
            $_ -match '(^|/)(\.git|\.github|build[^/]*|dist[^/]*|scenarios|user-data|profiles|autosave[^/]*)/' -or
            $_ -match '(?i)(^|/)(README|CHANGELOG|CONTRIBUTING|RELEASE_NOTES)\.md$' -or
            $_ -match '(?i)\.(json|log|exe|dll|obj|pdb|zip|bak|tmp)$'
        })
        if ($invalid.Count -gt 0) { throw "Package contains non-source files: $($invalid -join ', ')" }
    } finally { $zip.Dispose() }
    Write-Host "Verified minimal source package: $destination" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
