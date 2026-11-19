# SPDX-License-Identifier: MIT
# Execute from the prepared source tree. Requires MSVC / Qt 6.12 and JDK 11+.
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
    throw 'Install Qt 6.12 (MSVC) and supply -QtRoot C:\Qt\6.12.0\msvc2022_64.'
}
if ($Interactive -and (-not $env:JAVA_HOME -or -not (Test-Path -LiteralPath (Join-Path $env:JAVA_HOME 'bin\java.exe')))) {
    throw 'Interactive Java test needs JDK 11+ and JAVA_HOME.'
}
$env:PATH = (Join-Path $QtRoot 'bin') + [IO.Path]::PathSeparator + $env:PATH
$build = Join-Path $project 'build-phase22-java'
$enabled = if ($Interactive) { 'ON' } else { 'OFF' }
& cmake -S $project -B $build -G 'Visual Studio 17 2022' -A x64 `
    "-DCMAKE_PREFIX_PATH=$QtRoot" '-DCLICKNOVA_BUILD_GUI=ON' '-DBUILD_TESTING=ON' `
    "-DCLICKNOVA_JAVA_INTERACTIVE_TESTS=$enabled"
if ($LASTEXITCODE -ne 0) { throw 'Phase22 CMake configuration failed.' }
& cmake --build $build --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Phase22 build failed.' }
$filter = '^qml_java_(preview_policy|worker_protocol)_tests$'
if ($Interactive) { $filter = '^qml_java_(preview_policy|worker_protocol|worker_e2e)_tests$' }
$registered = (& ctest --test-dir $build -C $Configuration -N -R $filter | Out-String)
$expected = if ($Interactive) {3} else {2}
if ($registered -notmatch ('Total Tests:\s+{0}\b' -f $expected)) { throw "Expected $expected tests; got $registered" }
$out = & ctest --test-dir $build -C $Configuration -R $filter --output-on-failure 2>&1
$status = $LASTEXITCODE
$out | ForEach-Object { Write-Host $_ }
if ($status -ne 0) { throw 'Phase22 Java tests failed.' }
if ($Interactive -and (($out | Out-String) -match '(?i)\*\*\*Skipped|\bSkipped\b')) {
    throw 'Interactive Java Access Bridge test was SKIPPED — NOT verified.'
}
Write-Host "Phase22 Java checks passed: $Configuration / interactive=$Interactive" -ForegroundColor Green
