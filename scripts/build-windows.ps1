# SPDX-License-Identifier: Apache-2.0
param(
    [string]$QtRoot = "D:\QT\6.12.0\msvc2022_64",
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& (Join-Path $PSScriptRoot 'verify-source-tree.ps1')
$build = Join-Path $root 'build-vs2026-qt'
if (-not (Test-Path (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    throw "Qt6Config.cmake not found at '$QtRoot'. Select the actual Qt kit folder."
}
# CMake caches the absolute source directory. A one-time move/rename to the
# permanent ClickNova folder must not cause a mysterious cache mismatch.
$cachePath = Join-Path $build 'CMakeCache.txt'
if (Test-Path $cachePath) {
    $homeLine = Get-Content $cachePath | Where-Object { $_ -like 'CMAKE_HOME_DIRECTORY:INTERNAL=*' } | Select-Object -First 1
    if ($homeLine) {
        $cacheRoot = $homeLine.Substring('CMAKE_HOME_DIRECTORY:INTERNAL='.Length)
        if ([System.IO.Path]::GetFullPath($cacheRoot).TrimEnd('\', '/') -ine [System.IO.Path]::GetFullPath($root).TrimEnd('\', '/')) {
            Write-Host 'Source directory changed; recreating only the generated build-vs2026-qt folder.' -ForegroundColor Yellow
            Remove-Item -LiteralPath $build -Recurse -Force
        }
    }
}
Write-Host "ClickNova: configure with Qt at $QtRoot and MSVC v143" -ForegroundColor Cyan
& cmake -S $root -B $build -G 'Visual Studio 18 2026' -A x64 -T v143 "-DCMAKE_PREFIX_PATH=$QtRoot" -DCLICKNOVA_BUILD_GUI=ON
if ($LASTEXITCODE -ne 0) { throw 'Configuration failed. In Visual Studio Installer install MSVC v143 C++ x64/x86 build tools, then try again.' }
& cmake --build $build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
# Deploy Qt runtime before the GUI smoke tests, including a fresh build folder.
$exe = Join-Path $build 'Release\ClickNova.exe'
$deployer = Join-Path $QtRoot 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $deployer -PathType Leaf)) { throw 'windeployqt.exe is missing from the selected Qt kit.' }
& $deployer --release $exe
if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed.' }
& ctest --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
Write-Host "Built: $build\Release\ClickNova.exe" -ForegroundColor Green
if ($Package) {
    $dist = Join-Path $root 'dist-windows'
    & cmake --install $build --config Release --prefix $dist
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
    Write-Host "Deployment staging: $dist" -ForegroundColor Green
    Write-Host 'Before public release, verify official VC++ runtime redistributable and test on a clean machine.'
}
