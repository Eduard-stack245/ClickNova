# SPDX-License-Identifier: Apache-2.0
param(
    [string]$QtRoot = "",
    [string]$VcpkgRoot = "",
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# Windows PowerShell 5.1 promotes redirected native stderr into ErrorRecords.
# CMake prints *warnings* on stderr even when configure succeeds (exit 0).
# Convert native diagnostics to ordinary output for the installer and preserve
# the REAL exit status; only a nonzero status may fail the operation.
function Invoke-ClickNovaNative {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [ref]$ExitCode
    )
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments 2>&1 | ForEach-Object {
            # Keep both stdout and stderr visible in the GUI progress log.
            # ErrorRecords from native stderr must NOT terminate PowerShell 5.1.
            Write-Output ([string]$_)
        }
        $ExitCode.Value = [int]$LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
}

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
    foreach ($drive in @(Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Root)) {
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
if (-not $VcpkgRoot) { $VcpkgRoot = [Environment]::GetEnvironmentVariable('VCPKG_ROOT') }
# An explicit -VcpkgRoot takes precedence. Auto-detect an existing local
# installation so an opt-in visual dependency install remains enabled later
# when the user launches ClickNova.cmd from a new shell.
if (-not $VcpkgRoot) {
    $vcpkgCandidates = @((Join-Path $root 'tools\clicknova-visual-vcpkg'), (Join-Path $root 'tools\vcpkg'))
    Get-ChildItem -LiteralPath (Join-Path $root 'tools') -Directory -Filter 'clicknova-visual-vcpkg-*' -ErrorAction SilentlyContinue |
        ForEach-Object { $vcpkgCandidates += $_.FullName }
    foreach ($drive in @(Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Root)) {
        $vcpkgCandidates += (Join-Path $drive 'vcpkg')
    }
    $vcpkgCommand = Get-Command vcpkg.exe -ErrorAction SilentlyContinue
    if ($vcpkgCommand) { $vcpkgCandidates += (Split-Path -Parent $vcpkgCommand.Source) }
    foreach ($candidate in $vcpkgCandidates) {
        if ((Test-Path -LiteralPath (Join-Path $candidate 'vcpkg.exe') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $candidate 'scripts\buildsystems\vcpkg.cmake') -PathType Leaf)) {
            $VcpkgRoot = $candidate
            break
        }
    }
}
$cmakeExtra = @()
if ($VcpkgRoot) {
    $toolchain = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
    if (-not (Test-Path -LiteralPath $toolchain -PathType Leaf)) {
        throw "VCPKG_ROOT was provided, but toolchain was not found: $toolchain"
    }
    $toolchain = (Resolve-Path -LiteralPath $toolchain).Path
    # A toolchain must be selected before the first CMake configure.
    # Recreate only the generated build tree if an older Qt-only cache exists.
    $cacheToCheck = Join-Path $build 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cacheToCheck) {
        $configured = Get-Content -LiteralPath $cacheToCheck |
            Where-Object { $_ -match '^CMAKE_TOOLCHAIN_FILE:[^=]*=' } | Select-Object -First 1
        if (-not $configured -or
            ($configured.Split('=', 2)[1].Replace('\', '/').TrimEnd('/') -ine
             $toolchain.Replace('\', '/').TrimEnd('/'))) {
            Write-Host 'Enabling vcpkg: recreating generated CMake build cache.' -ForegroundColor Yellow
            Remove-Item -LiteralPath $build -Recurse -Force
        } else {
            # CMake already cached this toolchain. Passing -D again during a
            # reconfigure may cause misleading "unused-cli" warnings.
            $cmakeExtra = @()
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt') -PathType Leaf)) {
        $cmakeExtra = @("-DCMAKE_TOOLCHAIN_FILE=$toolchain")
    }
}

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
# Source relocation may also have invalidated the cache. Supply the vcpkg
# toolchain on every genuinely FRESH configure, never on unchanged reconfigure.
if ($VcpkgRoot -and -not (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt') -PathType Leaf)) {
    $cmakeExtra = @("-DCMAKE_TOOLCHAIN_FILE=$toolchain")
}
Write-Host "ClickNova: configure with Qt at $QtRoot and MSVC v143" -ForegroundColor Cyan
$nativeExit = -1
Invoke-ClickNovaNative 'cmake' (@('-S', $root, '-B', $build, '-G', 'Visual Studio 18 2026', '-A', 'x64', '-T', 'v143', "-DCMAKE_PREFIX_PATH=$QtRoot", '-DCLICKNOVA_BUILD_GUI=ON') + $cmakeExtra) ([ref]$nativeExit)
if ($nativeExit -ne 0) { throw "CMake configuration failed (exit $nativeExit). See the configure messages above." }
# The PowerShell 5.1 transcript can omit MSBuild output from parallel native
# processes. Use MSBuild's own file logger as the authoritative diagnostic log.
# Keep parallelism bounded to avoid exhausting RAM on consumer Windows PCs.
$msbuildLog = Join-Path $build 'last-msbuild.log'
if (Test-Path -LiteralPath $msbuildLog) { Remove-Item -LiteralPath $msbuildLog -Force }
Write-Host "MSBuild diagnostic log: $msbuildLog" -ForegroundColor Cyan
$nativeExit = -1
Invoke-ClickNovaNative 'cmake' @('--build', $build, '--config', 'Release', '--parallel', '2', '--', '/fl', "/flp:logfile=$msbuildLog;verbosity=normal;encoding=UTF-8") ([ref]$nativeExit)
if ($nativeExit -ne 0) {
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
$nativeExit = -1
Invoke-ClickNovaNative $deployer @('--release', $exe) ([ref]$nativeExit)
if ($nativeExit -ne 0) { throw "Qt runtime deployment failed (exit $nativeExit)." }
# Qt deployment does not copy the optional OpenCV/Tesseract/Leptonica DLLs.
# Copy installed x64 vcpkg runtime dependencies when using a vcpkg build.
if ($VcpkgRoot) {
    $runtimeDir = Join-Path $VcpkgRoot 'installed\x64-windows\bin'
    if (Test-Path -LiteralPath $runtimeDir -PathType Container) {
        Get-ChildItem -LiteralPath $runtimeDir -Filter '*.dll' -File |
            Copy-Item -Destination (Split-Path -Parent $exe) -Force
        Write-Host 'Deployed vcpkg x64-windows runtime DLLs.' -ForegroundColor Cyan
    } else {
        Write-Warning "vcpkg runtime directory not found: $runtimeDir"
    }
}
Write-Host 'Qt may report optional Vulkan, OpenSSL or Direct3D deployment warnings. If tests pass, these are normally informational.' -ForegroundColor DarkYellow
$ctestLog = Join-Path $build 'last-ctest.log'
$nativeExit = -1
Invoke-ClickNovaNative 'ctest' @('--test-dir', $build, '-C', 'Release', '--output-on-failure', '--output-log', $ctestLog) ([ref]$nativeExit)
if ($nativeExit -ne 0) {
    Write-Host "CTest failed. Test details: $ctestLog" -ForegroundColor Red
    if (Test-Path -LiteralPath $ctestLog) {
        Get-Content -LiteralPath $ctestLog -Tail 65 | ForEach-Object { Write-Host $_ }
    }
    # ClickNova is built as a Windows GUI subsystem executable. Some Qt
    # diagnostics are not attached to stderr, so the GUI test records failed
    # assertions in a dedicated file for reliable Windows troubleshooting.
    foreach ($testLogName in @('gui-layout-smoke.log', 'gui-workflows-smoke.log', 'gui-wheel-routing-smoke.log', 'gui-action-menu-smoke.log', 'gui-manual-step-groups-smoke.log', 'gui-sequence-smoke.log')) {
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
    $nativeExit = -1
    Invoke-ClickNovaNative 'cmake' @('--install', $build, '--config', 'Release', '--prefix', $dist) ([ref]$nativeExit)
    if ($nativeExit -ne 0) { throw "Installation staging failed (exit $nativeExit)." }
    Write-Host "Deployment staging: $dist" -ForegroundColor Green
    Write-Host 'Before public release, verify official VC++ runtime redistributable and test on a clean machine.'
}
