# SPDX-License-Identifier: Apache-2.0
# Read-only Git check. Does not stage, commit, push, clean or delete files.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $root

$gitRoot = (& git rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -ne 0 -or -not $gitRoot) {
    throw 'Run this inside the existing ClickNova Git checkout. Keep its .git directory.'
}
$resolvedGitRoot = (Resolve-Path ($gitRoot.Trim())).Path
if ($resolvedGitRoot.TrimEnd('\', '/') -ne $root.TrimEnd('\', '/')) {
    throw "Expected the ClickNova Git repository root, found: $resolvedGitRoot"
}

& git diff --check
if ($LASTEXITCODE -ne 0) { throw 'Whitespace issues in unstaged changes.' }
& git diff --cached --check
if ($LASTEXITCODE -ne 0) { throw 'Whitespace issues in staged changes.' }

$tracked = @(& git ls-files)
$badFiles = @($tracked | Where-Object {
    $_ -match '(^|/)(build|build-[^/]*|cmake-build-[^/]*|dist-windows|out)/' -or
    $_ -match '\.(exe|dll|obj|pdb|lib|ilk)$'
})
if ($badFiles.Count -gt 0) {
    $badFiles | ForEach-Object { Write-Host "Tracked build artifact: $_" -ForegroundColor Yellow }
    throw 'Build artifacts are already tracked. .gitignore does not untrack them.'
}

$license = Get-Content (Join-Path $root 'LICENSE') -Raw
if ($license -notmatch 'Apache License\s+Version 2\.0') {
    throw 'LICENSE must contain Apache License 2.0.'
}
$sourceFiles = @(Get-ChildItem (Join-Path $root 'src'), (Join-Path $root 'include'), (Join-Path $root 'tests') -Recurse -File |
    Where-Object { $_.Extension -in '.cpp', '.hpp', '.h' -or $_.Name.EndsWith('.hpp.in') })
foreach ($file in $sourceFiles) {
    $firstLine = Get-Content $file.FullName -TotalCount 1
    if ($firstLine -ne '// SPDX-License-Identifier: Apache-2.0') {
        throw "Wrong or missing source license marker: $($file.FullName)"
    }
}
$scanDirs = @('src', 'include', 'tests', 'docs') | ForEach-Object { Join-Path $root $_ } | Where-Object { Test-Path -LiteralPath $_ -PathType Container }
$obsolete = @(Get-ChildItem $scanDirs -Recurse -File |
    Select-String -Pattern 'GPL-3\.0-only|GPLv3-only')
if ($obsolete.Count -gt 0) {
    $obsolete | ForEach-Object { Write-Host "Outdated license statement: $($_.Path):$($_.LineNumber)" -ForegroundColor Yellow }
    throw 'Outdated GPL-only project license markers remain.'
}

# Information-only files are intentionally omitted from early commits.
$staged = @(& git diff --cached --name-only --diff-filter=ACMR)
$informationOnly = @($staged | Where-Object {
    $_ -match '^(docs/|examples/|schemas/|\.github/)' -or
    $_ -match '^(README|QUICKSTART|QUICKSTART-RU|CHANGELOG|CONTRIBUTING)\.md$'
})
if ($informationOnly.Count -gt 0) {
    $informationOnly | ForEach-Object { Write-Host "Staged optional information file: $_" -ForegroundColor Yellow }
    throw 'Unstage optional documentation for the minimal source commit. LICENSE must remain in the repository.'
}

Write-Host 'Git status:' -ForegroundColor Cyan
& git status --short
Write-Host 'License and Git checks passed; review changes and Windows tests before committing.' -ForegroundColor Green
