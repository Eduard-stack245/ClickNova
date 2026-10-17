# SPDX-License-Identifier: Apache-2.0
# ClickNova portable remover. Only packaged files listed by SHA-256 are eligible.
[CmdletBinding()]
param(
    [switch]$Confirmed,
    [int]$ParentPid = 0,
    [string]$DataRoot = '',
    [switch]$RemoveComponents,
    [switch]$RemoveSettings,
    [switch]$RemoveScenarios,
    [switch]$RemoveRecovery,
    [switch]$RemoveLogs
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = [IO.Path]::GetFullPath((Split-Path -Parent $MyInvocation.MyCommand.Path))
$manifest = Join-Path $root 'install-manifest.tsv'
function Ask([string]$question) {
    $answer = Read-Host "$question [y/N]"
    return ($answer -eq 'y' -or $answer -eq 'Y' -or $answer -eq 'yes')
}
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    Write-Host 'ClickNova: installation manifest is missing. Nothing deleted.' -ForegroundColor Yellow
    exit 3
}
if (-not $Confirmed) {
    Write-Host 'This removes only files in the ClickNova portable package, not system libraries.'
    if (-not (Ask 'Remove ClickNova application?')) { exit 0 }
    $RemoveComponents = Ask 'Remove OCR language files downloaded BY ClickNova (leave pre-existing files)?'
    $RemoveSettings = Ask 'Delete ClickNova settings?'
    $RemoveScenarios = Ask 'Delete saved scenarios and autosave?'
    $RemoveRecovery = Ask 'Delete recording recovery data?'
    $RemoveLogs = Ask 'Delete ClickNova activity logs?'
}
# Do not accept an arbitrary external data tree. Package-local data is the normal
# portable case, and fallback per-user ClickNova storage is the only other case.
$localData = [IO.Path]::GetFullPath((Join-Path $root 'data'))
if ([string]::IsNullOrWhiteSpace($DataRoot)) { $DataRoot = $localData }
$DataRoot = [IO.Path]::GetFullPath($DataRoot)
$normal = [IO.Path]::GetFullPath($localData).TrimEnd('\')
$isLocal = $DataRoot.TrimEnd('\').Equals($normal, [StringComparison]::OrdinalIgnoreCase)
$profilePrefix = [IO.Path]::GetFullPath([Environment]::GetFolderPath('ApplicationData')).TrimEnd('\') + '\'
$localProfilePrefix = [IO.Path]::GetFullPath([Environment]::GetFolderPath('LocalApplicationData')).TrimEnd('\') + '\'
$isLegacyFallback = ($DataRoot.StartsWith($profilePrefix, [StringComparison]::OrdinalIgnoreCase) -or
                     $DataRoot.StartsWith($localProfilePrefix, [StringComparison]::OrdinalIgnoreCase)) -and
                    ($DataRoot -match '[\\/]ClickNova$')
if (-not ($isLocal -or $isLegacyFallback)) { throw "Refusing unsafe data directory: $DataRoot" }
function SafeRelativeFile([string]$relative, [string]$base) {
    if ([string]::IsNullOrWhiteSpace($relative) -or $relative -match '[\x00-\x1f:]' -or $relative.Contains('\')) { return $null }
    $parts = $relative.Split('/')
    foreach ($part in $parts) { if ($part -eq '' -or $part -eq '.' -or $part -eq '..') { return $null } }
    $path = [IO.Path]::GetFullPath((Join-Path $base ($relative.Replace('/', '\'))))
    $prefix = [IO.Path]::GetFullPath($base).TrimEnd('\') + '\'
    if (-not $path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { return $null }
    $cursor = $base
    if ((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { return $null }
    foreach ($part in $parts) {
        $cursor = Join-Path $cursor $part
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { return $null }
        }
    }
    return $path
}
function RemoveVerified([string]$base, [string]$relative, [string]$expectedHash) {
    if ($expectedHash -notmatch '^[0-9a-fA-F]{64}$') { return $false }
    $path = SafeRelativeFile $relative $base
    if (-not $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $expectedHash) {
        Write-Host "Preserved modified file: $relative"
        return $false
    }
    Remove-Item -LiteralPath $path -Force
    Write-Host "Removed: $relative"
    return $true
}
function RemoveKnownDirectory([string]$relative) {
    $path = SafeRelativeFile $relative $DataRoot
    if (-not $path -or -not (Test-Path -LiteralPath $path -PathType Container)) { return }
    # Do not traverse any symlink or junction. Leave such entries intact.
    foreach ($entry in @(Get-ChildItem -LiteralPath $path -Force)) {
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
        if ($entry.PSIsContainer) {
            RemoveKnownDirectory ($relative + '/' + $entry.Name)
        } else {
            Remove-Item -LiteralPath $entry.FullName -Force
        }
    }
    if (@(Get-ChildItem -LiteralPath $path -Force).Count -eq 0) { Remove-Item -LiteralPath $path -Force }
}
if ($ParentPid -gt 0) {
    try { Wait-Process -Id $ParentPid -Timeout 60 -ErrorAction Stop }
    catch { if (Get-Process -Id $ParentPid -ErrorAction SilentlyContinue) { throw 'ClickNova is still running. Close it and try again.' } }
}
# Own-language manifest is written only after a NEW successful download.
$ownedPath = SafeRelativeFile 'owned-components.tsv' $DataRoot
if ($RemoveComponents -and $ownedPath -and (Test-Path -LiteralPath $ownedPath -PathType Leaf)) {
    $keep = New-Object 'System.Collections.Generic.List[string]'
    foreach ($line in [IO.File]::ReadAllLines($ownedPath)) {
        $fields = $line.Split([char[]]@([char]9), 2)
        if ($fields.Count -ne 2 -or $fields[1] -notin @('tessdata/eng.traineddata','tessdata/rus.traineddata')) { $keep.Add($line); continue }
        if (-not (RemoveVerified $DataRoot $fields[1] $fields[0])) { $keep.Add($line) }
    }
    if ($keep.Count -eq 0) { Remove-Item -LiteralPath $ownedPath -Force }
    else { [IO.File]::WriteAllLines($ownedPath, [string[]]$keep) }
}
if ($RemoveSettings) { RemoveKnownDirectory 'config' }
if ($RemoveScenarios) {
    RemoveKnownDirectory 'scenarios'
    RemoveKnownDirectory 'templates' # Portable OpenCV templates belong to scenarios.
    $draft = SafeRelativeFile 'clicknova-steps-autosave.json' $DataRoot
    if ($draft -and (Test-Path -LiteralPath $draft -PathType Leaf)) { Remove-Item -LiteralPath $draft -Force }
}
if ($RemoveRecovery) { RemoveKnownDirectory 'recording-recovery' }
if ($RemoveLogs) { RemoveKnownDirectory 'activity-logs' }
# Manifest is generated by the packager. Never scan/remove arbitrary DLLs.
$unremoved = 0
foreach ($line in [IO.File]::ReadAllLines($manifest)) {
    if (-not $line -or $line.StartsWith('#')) { continue }
    $fields = $line.Split([char[]]@([char]9), 2)
    if ($fields.Count -ne 2) { $unremoved++; continue }
    $path = SafeRelativeFile $fields[1] $root
    if ($path -and (Test-Path -LiteralPath $path -PathType Leaf)) {
        if (-not (RemoveVerified $root $fields[1] $fields[0])) { $unremoved++ }
    }
}
# Only attempt directories derived from the package manifest, not arbitrary
# user-created subfolders (especially data/).
$packageDirs = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($line in [IO.File]::ReadAllLines($manifest)) {
    $fields = $line.Split([char[]]@([char]9), 2)
    if ($fields.Count -ne 2) { continue }
    $path = SafeRelativeFile $fields[1] $root
    if (-not $path) { continue }
    $dir = Split-Path -Parent $path
    while ($dir -and -not $dir.Equals($root, [StringComparison]::OrdinalIgnoreCase)) {
        [void]$packageDirs.Add($dir)
        $dir = Split-Path -Parent $dir
    }
}
foreach ($dir in @($packageDirs | Sort-Object Length -Descending)) {
    if ((Test-Path -LiteralPath $dir -PathType Container) -and
        -not ((Get-Item -LiteralPath $dir -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -and
        @(Get-ChildItem -LiteralPath $dir -Force).Count -eq 0) {
        Remove-Item -LiteralPath $dir -Force
    }
}
if ($unremoved -gt 0) {
    Write-Host "$unremoved packaged files left intact (modified, unsafe or locked). Manual review required." -ForegroundColor Yellow
    exit 5
}
Remove-Item -LiteralPath $manifest -Force
foreach ($name in @('Uninstall-ClickNova.cmd','uninstall-clicknova.sh')) {
    $p = SafeRelativeFile $name $root
    if ($p -and (Test-Path -LiteralPath $p -PathType Leaf)) { Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue }
}
Write-Host 'ClickNova removed. Unselected user data and pre-existing components were preserved.' -ForegroundColor Green
# Self-removal is best effort; no system-wide removal is attempted.
Remove-Item -LiteralPath $MyInvocation.MyCommand.Path -Force -ErrorAction SilentlyContinue
