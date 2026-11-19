# SPDX-License-Identifier: MIT
# Invoke from the prepared source tree, never inside the migration ZIP.
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$Interactive,
    [string]$QtRoot = $env:QT_ROOT_DIR
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $QtRoot -or -not (Test-Path -LiteralPath (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    throw 'Qt 6.12 is required, for example -QtRoot C:\Qt\6.12.0\msvc2022_64.'
}
$env:PATH = (Join-Path $QtRoot 'bin') + [IO.Path]::PathSeparator + $env:PATH
$build = Join-Path $project 'build-phase21-msaa'
$flag = if ($Interactive) { 'ON' } else { 'OFF' }
& cmake -S $project -B $build -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_PREFIX_PATH=$QtRoot" '-DCLICKNOVA_BUILD_GUI=ON' '-DBUILD_TESTING=ON' `
    "-DCLICKNOVA_MSAA_INTERACTIVE_TESTS=$flag"
if ($LASTEXITCODE -ne 0) { throw 'Phase21 CMake configure failed.' }
& cmake --build $build --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Phase21 build failed.' }
$filter = '^qml_msaa_worker_policy_tests$'
if ($Interactive) { $filter = '^qml_msaa_worker_(policy|e2e)_tests$' }
$registered = (& ctest --test-dir $build -C $Configuration -N -R $filter | Out-String)
$expected = if ($Interactive) { 2 } else { 1 }
if ($registered -notmatch ('Total Tests:\s+{0}\b' -f $expected)) {
    throw "Expected $expected MSAA tests; actual CTest:`n$registered"
}
$result = & ctest --test-dir $build -C $Configuration -R $filter --output-on-failure 2>&1
$exitCode = $LASTEXITCODE
$result | ForEach-Object { Write-Host $_ }
if ($exitCode -ne 0) { throw 'Phase21 MSAA checks failed.' }
if ($Interactive -and (($result | Out-String) -match '(?i)\*\*\*Skipped|\bSkipped\b')) {
    throw 'Interactive MSAA fixture was SKIPPED, NOT verified.'
}
Write-Host "Phase21 MSAA $Configuration successful; interactive=$Interactive" -ForegroundColor Green
