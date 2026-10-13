# SPDX-License-Identifier: Apache-2.0
param(
    [string]$QtRoot = "",
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& (Join-Path $PSScriptRoot 'verify-source-tree.ps1')
# Discover Qt without embedding a developer-specific installation path.
if (-not $QtRoot) {
    $candidates = @()
    foreach ($name in @('CLICKNOVA_QT_ROOT', 'QTDIR', 'QT_ROOT', 'Qt6_DIR')) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value) {
            if ($name -eq 'Qt6_DIR' -and $value -match '[\\/]lib[\\/]cmake[\\/]Qt6$') {
                $value = (Resolve-Path (Join-Path $value '..\..\..')).Path
            }
            $candidates += $value
        }
    }
    foreach ($qtCommand in @('qmake6.exe', 'qmake.exe')) {
        $command = Get-Command $qtCommand -ErrorAction SilentlyContinue
        if ($command) {
            $qmakeRoot = & $command.Source -query QT_INSTALL_PREFIX 2>$null
            if ($LASTEXITCODE -eq 0 -and $qmakeRoot) { $candidates += $qmakeRoot.Trim() }
        }
    }
    foreach ($drive in @('C:', 'D:')) {
        $qtBase = Join-Path $drive 'Qt'
        if (Test-Path -LiteralPath $qtBase -PathType Container) {
            Get-ChildItem -LiteralPath $qtBase -Directory -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending | ForEach-Object {
                    $candidates += (Join-Path $_.FullName 'msvc2022_64')
                }
        }
    }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'lib\cmake\Qt6\Qt6Config.cmake') -PathType Leaf)) {
            $QtRoot = $candidate
            break
        }
    }
}
if (-not $QtRoot) {
    throw 'Qt 6.12 MSVC kit not found. Set CLICKNOVA_QT_ROOT or run scripts/build-windows.ps1 -QtRoot <Qt-directory>.'
}
$build = Join-Path $root 'build-vs2026-qt'
$validated = Join-Path $build '.clicknova-tests-passed'
# Clear the success marker before starting any build. A failed CTest must not
# be bypassed by the next launcher invocation just because ClickNova.exe exists.
if (Test-Path -LiteralPath $validated) { Remove-Item -LiteralPath $validated -Force }
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
# The PowerShell 5.1 transcript can omit MSBuild output from parallel native
# processes. Use MSBuild's own file logger as the authoritative diagnostic log.
# Keep parallelism bounded to avoid exhausting RAM on consumer Windows PCs.
$msbuildLog = Join-Path $build 'last-msbuild.log'
if (Test-Path -LiteralPath $msbuildLog) { Remove-Item -LiteralPath $msbuildLog -Force }
Write-Host "MSBuild diagnostic log: $msbuildLog" -ForegroundColor Cyan
& cmake --build $build --config Release --parallel 2 -- /fl "/flp:logfile=$msbuildLog;verbosity=normal;encoding=UTF-8"
if ($LASTEXITCODE -ne 0) {
    Write-Host 'MSBuild failed. Compiler and linker diagnostics:' -ForegroundColor Red
    if (Test-Path -LiteralPath $msbuildLog) {
        $buildErrors = @(Select-String -LiteralPath $msbuildLog -Pattern ': error ', 'fatal error', 'error C[0-9]+', 'error LNK[0-9]+', 'error MSB[0-9]+', 'ошибка' | Select-Object -Last 40)
        if ($buildErrors.Count -gt 0) {
            foreach ($entry in $buildErrors) { Write-Host $entry.Line -ForegroundColor Red }
        } else {
            Get-Content -LiteralPath $msbuildLog -Tail 65 | ForEach-Object { Write-Host $_ }
        }
    } else {
        Write-Host 'MSBuild did not create its diagnostic log; check the build console output.' -ForegroundColor Red
    }
    throw "Build failed. See $msbuildLog"
}
# Deploy Qt runtime before the GUI smoke tests, including a fresh build folder.
$exe = Join-Path $build 'Release\ClickNova.exe'
$deployer = Join-Path $QtRoot 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $deployer -PathType Leaf)) { throw 'windeployqt.exe is missing from the selected Qt kit.' }
& $deployer --release $exe
if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed.' }
Write-Host 'Qt may report optional Vulkan, OpenSSL or Direct3D deployment warnings. If tests pass, these are normally informational.' -ForegroundColor DarkYellow
$ctestLog = Join-Path $build 'last-ctest.log'
& ctest --test-dir $build -C Release --output-on-failure --output-log $ctestLog
if ($LASTEXITCODE -ne 0) {
    Write-Host "CTest failed. Test details: $ctestLog" -ForegroundColor Red
    if (Test-Path -LiteralPath $ctestLog) {
        Get-Content -LiteralPath $ctestLog -Tail 65 | ForEach-Object { Write-Host $_ }
    }
    # ClickNova is built as a Windows GUI subsystem executable. Some Qt
    # diagnostics are not attached to stderr, so the GUI test records failed
    # assertions in a dedicated file for reliable Windows troubleshooting.
    foreach ($testLogName in @('gui-layout-smoke.log', 'gui-action-menu-smoke.log', 'gui-sequence-smoke.log')) {
        $testLogPath = Join-Path $build (Join-Path 'Release' $testLogName)
        if (Test-Path -LiteralPath $testLogPath) {
            Write-Host "GUI smoke failure details ($testLogPath):" -ForegroundColor Red
            Get-Content -LiteralPath $testLogPath | ForEach-Object { Write-Host $_ -ForegroundColor Red }
        }
    }
    throw 'Tests failed.'
}
Set-Content -LiteralPath $validated -Value "ClickNova validation passed: $(Get-Date -Format o)" -Encoding ASCII
Write-Host "Built and tested: $build\Release\ClickNova.exe" -ForegroundColor Green
if ($Package) {
    $dist = Join-Path $root 'dist-windows'
    & cmake --install $build --config Release --prefix $dist
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
    Write-Host "Deployment staging: $dist" -ForegroundColor Green
    Write-Host 'Before public release, verify official VC++ runtime redistributable and test on a clean machine.'
}
