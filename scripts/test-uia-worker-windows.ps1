# SPDX-License-Identifier: MIT
# Run in the PREPARED ClickNova source tree, not inside the migration ZIP.
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
    throw 'Qt 6.12 CMake package not found. Pass -QtRoot C:\path\to\Qt\6.12\msvc2022_64.'
}
# The child is the newly built ClickNova.exe, which needs Qt6Core.dll and
# other Qt runtime binaries available in its inherited process environment.
$env:PATH = (Join-Path $QtRoot 'bin') + [IO.Path]::PathSeparator + $env:PATH
$build = Join-Path $project 'build-phase20-uia'
$interactiveSwitch = if ($Interactive) { 'ON' } else { 'OFF' }
& cmake -S $project -B $build -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_PREFIX_PATH=$QtRoot" '-DCLICKNOVA_BUILD_GUI=ON' '-DBUILD_TESTING=ON' `
    "-DCLICKNOVA_UIA_INTERACTIVE_TESTS=$interactiveSwitch"
if ($LASTEXITCODE -ne 0) { throw 'Phase20 CMake configure failed.' }
& cmake --build $build --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Phase20 build failed.' }
$testFilter = '^qml_uia_(worker_protocol|isolated_transport)_tests$'
if ($Interactive) {
    $testFilter = '^qml_uia_(worker_protocol|isolated_transport|windows_integration|worker_e2e)_tests$'
}
$registered = (& ctest --test-dir $build -C $Configuration -N -R $testFilter | Out-String)
$expected = if ($Interactive) { 4 } else { 2 }
if ($registered -notmatch ('Total Tests:\s+{0}\b' -f $expected)) {
    throw "Expected $expected UIA tests, but CTest reported:`n$registered"
}
$output = & ctest --test-dir $build -C $Configuration -R $testFilter --output-on-failure 2>&1
$exitCode = $LASTEXITCODE
$output | ForEach-Object { Write-Host $_ }
if ($exitCode -ne 0) { throw 'Phase20 UIA tests FAILED; check CTest output.' }
if ($Interactive -and (($output | Out-String) -match '(?i)\*\*\*Skipped|\bSkipped\b')) {
    throw 'The interactive UIA test was skipped: NOT a verified Windows COM result.'
}
Write-Host "Phase20 $Configuration checks passed. Interactive mode: $Interactive" -ForegroundColor Green
