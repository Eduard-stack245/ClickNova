# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]
param([string]$ProjectRoot = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
try {
    $bundle = $PSScriptRoot
    if (-not $ProjectRoot) { $ProjectRoot = Read-Host 'Enter ClickNova source directory' }
    $root = (Resolve-Path -LiteralPath $ProjectRoot).Path
    if (-not (Test-Path -LiteralPath (Join-Path $root 'CMakeLists.txt') -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $root 'src\gui\input_lab.cpp') -PathType Leaf)) {
        throw 'Not a valid ClickNova project source directory.'
    }
    $baseMap = @{}
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $bundle 'BASELINE.sha256'))) {
        if (-not $line.Trim()) { continue }
        $fields = $line.Split([char[]]@([char]9), 2)
        if ($fields.Count -ne 2) { throw 'Invalid baseline manifest.' }
        $baseMap[$fields[1]] = $fields[0]
    }
    $newMap = @{}
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $bundle 'MANIFEST.sha256'))) {
        if (-not $line.Trim()) { continue }
        $fields = $line.Split([char[]]@([char]9), 2)
        if ($fields.Count -ne 2) { throw 'Invalid patch manifest.' }
        $newMap[$fields[1]] = $fields[0]
    }
    $patch = Join-Path $bundle 'patch'
    $files = @(Get-ChildItem -LiteralPath $patch -Recurse -File -Force)
    if ($files.Count -ne $newMap.Count) { throw 'Package corruption: patch file count differs from manifest.' }
    $toApply = New-Object 'System.Collections.Generic.List[string]'
    foreach ($file in $files) {
        $rel = $file.FullName.Substring($patch.Length + 1).Replace('\','/')
        if (-not $newMap.ContainsKey($rel) -or $rel -match '[\x00-\x1f:]' -or
            $rel.Contains('../') -or $rel.StartsWith('/')) { throw ('Unsafe or unknown patch path: ' + $rel) }
        if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ine $newMap[$rel]) {
            throw ('Patch file was modified or corrupted: ' + $rel)
        }
        $destination = Join-Path $root $rel.Replace('/','\')
        if (Test-Path -LiteralPath $destination -PathType Leaf) {
            $currentHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($currentHash -ieq $newMap[$rel]) { continue }
            if (-not $baseMap.ContainsKey($rel) -or $currentHash -ine $baseMap[$rel]) {
                throw ('Source differs from audited baseline; refusing to overwrite: ' + $rel)
            }
        } elseif ($baseMap.ContainsKey($rel)) {
            throw ('Existing source file missing: ' + $rel)
        }
        $toApply.Add($rel)
    }
    if ($toApply.Count -eq 0) { Write-Host 'ClickNova organized portable source update already applied. No changes.' -ForegroundColor Green; exit 0 }
    $backup = Join-Path $root ('backup-portable-layout-v2-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    foreach ($rel in $toApply) {
        $from = Join-Path $root $rel.Replace('/','\')
        if (Test-Path -LiteralPath $from -PathType Leaf) {
            $saved = Join-Path $backup $rel.Replace('/','\')
            New-Item -ItemType Directory -Path (Split-Path -Parent $saved) -Force | Out-Null
            Copy-Item -LiteralPath $from -Destination $saved
        }
    }
    $installed = New-Object 'System.Collections.Generic.List[string]'
    try {
        foreach ($rel in $toApply) {
            $from = Join-Path $patch $rel.Replace('/','\')
            $to = Join-Path $root $rel.Replace('/','\')
            New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force | Out-Null
            # Track the current path BEFORE the copy; rollback also covers a partially copied file.
            $installed.Add($rel)
            Copy-Item -LiteralPath $from -Destination $to -Force
            if ((Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash -ine $newMap[$rel]) {
                throw ('Hash mismatch after copy: ' + $rel)
            }
        }
    } catch {
        foreach ($rel in $installed) {
            $to = Join-Path $root $rel.Replace('/','\')
            $saved = Join-Path $backup $rel.Replace('/','\')
            if (Test-Path -LiteralPath $saved -PathType Leaf) {
                Copy-Item -LiteralPath $saved -Destination $to -Force
            } elseif (Test-Path -LiteralPath $to) { Remove-Item -LiteralPath $to -Force }
        }
        throw
    }
    Write-Host ('Applied ' + $toApply.Count + ' source changes; backup: ' + $backup) -ForegroundColor Green
    Write-Host 'Rebuild ClickNova before creating the new Portable ZIP.'
} catch {
    Write-Host ('CLICKNOVA UPDATE STOPPED: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
