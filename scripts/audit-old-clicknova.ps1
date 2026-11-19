# SPDX-License-Identifier: MIT
<#
.SYNOPSIS
  Reviews old ClickNova development directories without touching user data.
.DESCRIPTION
  Default mode is READ-ONLY. -Apply requires both -Remove and an explicit
  exact-name confirmation for each directory. The active ClickNova checkout,
  Qt, vcpkg, configuration, saved scenarios and Windows application data are
  never selected or modified by this script.
.EXAMPLE
  .\scripts\audit-old-clicknova.ps1 -WorkspaceRoot 'D:\project'
.EXAMPLE
  .\scripts\audit-old-clicknova.ps1 -WorkspaceRoot 'D:\project' -Apply -Remove 'ClickNova-QML-Upgrade-27-35'
#>
[CmdletBinding()]
param(
    [string]$WorkspaceRoot = 'D:\project',
    [string]$ActiveProject = 'D:\project\ClickNova',
    [string[]]$Remove = @(),
    [switch]$Apply
)
$ErrorActionPreference = 'Stop'

function Normalize-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}
function Get-FolderBytes([string]$Path) {
    [long]$sum = 0
    # Recursion does not follow directory reparse points in Windows PowerShell
    # 5.1. Skip any that do appear so a size never includes external drives.
    Get-ChildItem -LiteralPath $Path -Force -File -Recurse -ErrorAction SilentlyContinue |
        ForEach-Object { if (-not ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) { $sum += $_.Length } }
    return $sum
}
function Has-ReparsePoint([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { return $true }
    $links = @(Get-ChildItem -LiteralPath $Path -Force -Recurse -ErrorAction Stop |
        Where-Object { $_.Attributes -band [System.IO.FileAttributes]::ReparsePoint } |
        Select-Object -First 1)
    return ($links.Count -gt 0)
}

if (-not (Test-Path -LiteralPath $WorkspaceRoot -PathType Container)) {
    throw "Workspace directory does not exist: $WorkspaceRoot"
}
$root = Normalize-FullPath $WorkspaceRoot
$active = Normalize-FullPath $ActiveProject
if ([System.IO.Path]::GetPathRoot($root).TrimEnd('\', '/') -ieq $root) {
    throw 'Refusing to scan or remove anything from a drive root.'
}
if ($root -ieq $active) {
    throw 'WorkspaceRoot must be the parent of the active project, not the project itself.'
}
# Only sibling development copies, not normal files, user data or DLL folders.
$items = @(Get-ChildItem -LiteralPath $root -Directory -Force |
    Where-Object { $_.Name -match '^ClickNova[-_]' -and (Normalize-FullPath $_.FullName) -ine $active } |
    Sort-Object Name)
Write-Host "Active project (PROTECTED): $active"
Write-Host "Workspace scanned: $root"
Write-Host 'Read-only inventory of old, separate ClickNova folders:'
if ($items.Count -eq 0) { Write-Host '  No old sibling folders found.' }
else {
    $table = @($items | ForEach-Object {
        $bytes = Get-FolderBytes $_.FullName
        [pscustomobject]@{
            Directory = $_.Name
            SizeGB = [math]::Round($bytes / 1GB, 2)
            Note = 'Review: may contain unique source/data'
        }
    })
    $table | Format-Table -AutoSize | Out-Host
}
Write-Host 'Nothing in the active ClickNova folder or Qt/vcpkg/AppData is deleted.'
Write-Host 'Old project copies may contain important source files. Make a verified backup before deletion.'
if (-not $Apply) {
    Write-Host 'PREVIEW ONLY. To delete one reviewed old folder, rerun with -Apply -Remove "EXACT-FOLDER-NAME".'
    return
}
if ($Remove.Count -eq 0) {
    throw 'Deletion mode requires -Remove with one or more exact folder names; nothing was deleted.'
}
foreach ($name in $Remove) {
    if ([string]::IsNullOrWhiteSpace($name) -or $name -notmatch '^ClickNova[-_][A-Za-z0-9_.-]+$' -or
        $name.Contains('..')) { throw "Unsafe folder name: $name" }
    $candidate = @($items | Where-Object { $_.Name -ceq $name })
    if ($candidate.Count -ne 1) { throw "Not an eligible old ClickNova folder: $name" }
    $full = Normalize-FullPath $candidate[0].FullName
    if ((Normalize-FullPath (Split-Path -Path $full -Parent)) -ine $root -or $full -ieq $active) {
        throw "Protected or non-sibling path: $full"
    }
    if (Has-ReparsePoint $full) {
        throw "Folder contains symbolic links/junctions; refusing to remove: $full"
    }
    Write-Warning "Permanent deletion requested: $full"
    $confirmation = Read-Host "Type the EXACT folder name '$name' to permanently delete it (Enter cancels)"
    if ($confirmation -cne $name) {
        Write-Host "Skipped: $name"
        continue
    }
    Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction Stop
    Write-Host "Removed selected old folder: $name"
}
