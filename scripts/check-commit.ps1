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

# Reject staged documentation, private data, binaries, and build output.
$staged = @(& git diff --cached --name-only --diff-filter=ACMR)
$sensitive = @($staged | Where-Object {
    $_ -match '(?i)(^|/)(README|CHANGELOG|CONTRIBUTING|RELEASE_NOTES)\.md$' -or
    $_ -match '(^|/)(docs|\.github|build(-[^/]*)?|cmake-build-[^/]*|dist(-[^/]*)?|\.clicknova|user-data|user-profiles|user-macros|scenarios|user-scenarios|profiles|backups)/' -or
    $_ -match '(?i)\.(exe|dll|lib|obj|pdb|log|dmp|zip|json)$' -or
    $_ -match '(^|/)\.env(\.|$)' -or
    $_ -match 'autosave'
})
if ($sensitive.Count -gt 0) {
    $sensitive | ForEach-Object { Write-Host "Unsafe staged file: $_" -ForegroundColor Yellow }
    throw 'Remove generated files, documentation, or private data from staging.'
}

# Previously tracked documentation must also be removed from the index.
$disallowedTracked = @($tracked | Where-Object {
    $_ -match '(?i)(^|/)(README|CHANGELOG|CONTRIBUTING|RELEASE_NOTES)\.md$' -or
    $_ -match '(^|/)(docs|\.github)/'
})
if ($disallowedTracked.Count -gt 0) {
    $disallowedTracked | ForEach-Object { Write-Host "Remove from Git index: $_" -ForegroundColor Yellow }
    throw 'Remove legacy documentation and workflows from Git before committing.'
}

# C++ source comments and build tooling are English; localized UI strings are allowed.
$nonEnglishComments = @(Select-String -Path $sourceFiles.FullName -Pattern '^\s*(?://|/\*|\*|#)\s*.*[А-Яа-яЁё]')
if ($nonEnglishComments.Count -gt 0) {
    throw 'Non-English source comments found. Keep comments in English and localized UI strings as-is.'
}

# Confirm version and repository metadata before publishing source.
$cmakeText = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
$projectVersion = [regex]::Match($cmakeText, 'project\(ClickNova\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)').Groups[1].Value
if (-not $projectVersion) { throw 'ClickNova project version not found.' }
& cmake "-DPROJECT_ROOT=$root" "-DPROJECT_VERSION=$projectVersion" -P (Join-Path $PSScriptRoot 'check-repository.cmake')
if ($LASTEXITCODE -ne 0) { throw 'Repository consistency check failed.' }

Write-Host 'Git status:' -ForegroundColor Cyan
& git status --short
Write-Host 'Clean source and Git checks passed; review Windows GUI tests before committing.' -ForegroundColor Green
