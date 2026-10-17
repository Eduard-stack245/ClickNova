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
        # Linux CTest invokes tests/uninstall_safety_tests.py; omitting it
        # produced a source package which configured but failed its tests.
        # Linux portable utilities and Windows optional setup also belong to
        # a buildable source release, despite not being C++ files.
        $extensions = if ($dir -eq 'scripts') { @('.ps1', '.cmake', '.sh', '.cmd', '.py') }
                      elseif ($dir -eq 'tests') { @('.cpp', '.hpp', '.h', '.in', '.py', '.mjs') }
                      else { @('.cpp', '.hpp', '.h', '.in') }
        Get-ChildItem -LiteralPath $sourceDir -Recurse -File | Where-Object { $_.Extension -in $extensions } | ForEach-Object {
            $relative = $_.FullName.Substring($root.Length).TrimStart('\', '/')
            $copyTo = Join-Path $stage $relative
            New-Item -Path (Split-Path $copyTo -Parent) -ItemType Directory -Force | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $copyTo -Force
        }
    }
    # The browser integrations are source assets, not generated/private JSON.
    # Keep exactly the signed-off WebExtensions files in every source ZIP.
    foreach ($engine in @('chromium', 'firefox')) {
        $sourceDir = Join-Path $root "extensions\$engine"
        Get-ChildItem -LiteralPath $sourceDir -File | Where-Object {
            $_.Name -in @('manifest.json', 'background.js', 'options.html', 'options.js')
        } | ForEach-Object {
            $relative = $_.FullName.Substring($root.Length).TrimStart('\', '/')
            $copyTo = Join-Path $stage $relative
            New-Item -Path (Split-Path $copyTo -Parent) -ItemType Directory -Force | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $copyTo -Force
        }
    }
    # The release recipe and AppImage desktop assets are required to create
    # ready-to-run downloads from the source ZIP via GitHub Actions.
    foreach ($item in @('.github/workflows/portable-releases.yml',
                        'packaging/clicknova.desktop', 'packaging/clicknova.svg')) {
        $from = Join-Path $root $item
        if (-not (Test-Path -LiteralPath $from -PathType Leaf)) { throw "Missing portable release asset: $item" }
        $to = Join-Path $stage $item
        New-Item -Path (Split-Path $to -Parent) -ItemType Directory -Force | Out-Null
        Copy-Item -LiteralPath $from -Destination $to
    }
    foreach ($file in @('.gitattributes', '.gitignore', 'CMakeLists.txt', 'ClickNova.cmd', 'Enable-OpenCV-OCR.cmd', 'LICENSE')) {
        Copy-Item -LiteralPath (Join-Path $root $file) -Destination (Join-Path $stage $file) -Force
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Force }
    [System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $destination, [System.IO.Compression.CompressionLevel]::Optimal, $false)
    $zip = [System.IO.Compression.ZipFile]::OpenRead($destination)
    try {
        $entries = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        foreach ($item in @('scripts/build-windows.ps1', 'scripts/verify-source-tree.ps1',
                             'src/gui/main.cpp', 'CMakeLists.txt', '.gitignore', 'ClickNova.cmd', 'LICENSE',
                             'extensions/chromium/manifest.json', 'extensions/firefox/manifest.json',
                             'scripts/build-linux.sh', 'scripts/uninstall-clicknova.sh',
                             'scripts/uninstall-clicknova.ps1', 'scripts/package-portable-linux.sh',
                             'tests/uninstall_safety_tests.py', 'tests/visual_installer_contract_tests.py',
                             'Enable-OpenCV-OCR.cmd',
                             'tests/preflight_tests.cpp', 'scripts/visual-recognition-installer.ps1',
                             'tests/x11_window_tests.cpp',
                             'src/platform/x11/window_target_x11.cpp',
                             'src/platform/x11/x11_input_backend.cpp', 'include/clicknova/x11_input_backend.hpp',
                             'tests/x11_input_tests.cpp',
                             '.github/workflows/portable-releases.yml',
                             'packaging/clicknova.desktop', 'packaging/clicknova.svg',
                             'scripts/package-appimage-linux.sh',
                             'scripts/package-appimage-bundle-linux.py',
                             'tests/release_packaging_tests.py')) {
            if ($entries -notcontains $item) { throw "Package is missing $item" }
        }
        $invalid = @($entries | Where-Object {
            ($_ -match '(^|/)(\.git|\.github|build[^/]*|dist[^/]*|scenarios|user-data|profiles|autosave[^/]*)/' -and
             $_ -ne '.github/workflows/portable-releases.yml') -or
            $_ -match '(?i)(^|/)(README|CHANGELOG|CONTRIBUTING|RELEASE_NOTES)\.md$' -or
            ($_ -match '(?i)\.(json|log|exe|dll|obj|pdb|zip|bak|tmp)$' -and
             $_ -notmatch '^extensions/(chromium|firefox)/manifest[.]json$')
        })
        if ($invalid.Count -gt 0) { throw "Package contains non-source files: $($invalid -join ', ')" }
    } finally { $zip.Dispose() }
    Write-Host "Verified minimal source package: $destination" -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}
