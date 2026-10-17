# SPDX-License-Identifier: Apache-2.0
# Explicit opt-in developer dependency installer. No system-wide installation.
param([string]$VcpkgRoot = '', [switch]$WaitForApplicationExit, [string]$ResultFile = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($WaitForApplicationExit) { Start-Sleep -Seconds 4 }

function Send-Stage([int]$Index, [string]$Message) {
    Write-Host ('::CLICKNOVA_STAGE::{0}::{1}' -f $Index, $Message)
}
function Check-File([string]$Root, [string]$Relative) {
    return (Test-Path -LiteralPath (Join-Path $Root $Relative) -PathType Leaf)
}
function Test-CompleteCheckout([string]$Root) {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return $false }
    foreach ($file in @('bootstrap-vcpkg.bat', 'scripts\bootstrap.ps1', 'scripts\buildsystems\vcpkg.cmake')) {
        if (-not (Check-File $Root $file)) { return $false }
    }
    return $true
}
function Test-ReadyVcpkg([string]$Root) {
    if (-not (Test-CompleteCheckout $Root)) { return $false }
    if (-not (Check-File $Root 'vcpkg.exe')) { return $false }
    # Existing binaries must actually run, not merely exist on disk.
    try {
        $null = & (Join-Path $Root 'vcpkg.exe') version 2>&1
        if ($LASTEXITCODE -ne 0) { return $false }
    } catch { return $false }
    return $true
}
function Invoke-Native([string]$Executable, [string[]]$Arguments) {
    $previousPreference = $ErrorActionPreference
    try {
        # PowerShell 5.1 may promote native stderr to NativeCommandError.
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments 2>&1 | ForEach-Object { Write-Host ([string]$_) }
        return $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
}
function Get-Tool([string]$Name) {
    $tool = Get-Command $Name -ErrorAction SilentlyContinue
    if ($tool) { return $tool.Source }
    return ''
}
function Get-ManagedRoot([string]$Project) {
    $toolsDir = Join-Path $Project 'tools'
    if (-not (Test-Path -LiteralPath $toolsDir -PathType Container)) {
        New-Item -Path $toolsDir -ItemType Directory -Force | Out-Null
    }
    for ($n = 0; $n -lt 100; ++$n) {
        $name = if ($n -eq 0) { 'clicknova-visual-vcpkg' } else { 'clicknova-visual-vcpkg-' + $n }
        $candidate = Join-Path $toolsDir $name
        if (Test-ReadyVcpkg $candidate) { return $candidate }
        if (-not (Test-Path -LiteralPath $candidate)) { return $candidate }
        Write-Host "Preserving an incomplete existing folder: $candidate" -ForegroundColor Yellow
    }
    throw 'Too many incomplete managed vcpkg folders. Inspect tools manually.'
}
function Download-ManagedVcpkg([string]$Destination) {
    if (Test-Path -LiteralPath $Destination) { throw "Refusing to overwrite an existing folder: $Destination" }
    $git = Get-Tool 'git.exe'
    if ($git) {
        Write-Host 'Downloading official Microsoft vcpkg using Git...' -ForegroundColor Cyan
        $code = Invoke-Native $git @('clone', '--depth', '1', '--progress', 'https://github.com/microsoft/vcpkg.git', $Destination)
        if ($code -ne 0) { throw "Git download failed ($code). The partial folder is preserved for diagnosis; retry chooses another folder." }
    } else {
        # The GUI installer also works on development machines without Git.
        Write-Host 'Git not found; downloading the official vcpkg source ZIP...' -ForegroundColor Cyan
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        $archive = Join-Path ([IO.Path]::GetTempPath()) ('clicknova-vcpkg-' + [guid]::NewGuid().ToString('N') + '.zip')
        $unpacked = Join-Path ([IO.Path]::GetTempPath()) ('clicknova-vcpkg-' + [guid]::NewGuid().ToString('N'))
        try {
            $client = New-Object System.Net.WebClient
            try { $client.DownloadFile('https://github.com/microsoft/vcpkg/archive/refs/heads/master.zip', $archive) }
            finally { $client.Dispose() }
            [IO.Compression.ZipFile]::ExtractToDirectory($archive, $unpacked)
            $source = Join-Path $unpacked 'vcpkg-master'
            if (-not (Test-CompleteCheckout $source)) { throw 'Downloaded vcpkg archive lacks bootstrap files.' }
            Move-Item -LiteralPath $source -Destination $Destination
        } finally {
            if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
            if (Test-Path -LiteralPath $unpacked) { Remove-Item -LiteralPath $unpacked -Recurse -Force }
        }
    }
    if (-not (Test-CompleteCheckout $Destination)) {
        throw 'Downloaded vcpkg is incomplete (scripts/bootstrap.ps1 missing). No existing folders were modified.'
    }
    Write-Host 'vcpkg source checkout verified.' -ForegroundColor Green
}
function Install-Packages([string]$Exe) {
    $deadline = [DateTime]::UtcNow.AddMinutes(10)
    for ($attempt = 1; ; ++$attempt) {
        Write-Host ("Installing OpenCV/Tesseract (attempt $attempt; this can take a long time)...") -ForegroundColor Cyan
        $installState = @{ LockBusy = $false }
        $previousPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & $Exe install opencv4:x64-windows tesseract:x64-windows 2>&1 | ForEach-Object {
                $line = [string]$_
                Write-Host $line
                if ($line -match '(?i)(failed to take lock|failed to take filesystem lock|another vcpkg may be running|calling try_take_exclusive_file_lock failed|device or resource busy)') {
                    $installState.LockBusy = $true
                }
            }
            $code = $LASTEXITCODE
        } finally { $ErrorActionPreference = $previousPreference }
        if ($code -eq 0) { return }
        if (-not $installState.LockBusy) { throw "vcpkg install failed (exit $code). See the detailed installer log." }
        if ([DateTime]::UtcNow -ge $deadline) {
            throw 'vcpkg is busy for more than 10 minutes. Finish the other install and retry; do not delete lock files.'
        }
        Write-Host 'Another vcpkg process holds the installed-directory lock; waiting 15 seconds.' -ForegroundColor Yellow
        Start-Sleep -Seconds 15
    }
}

function Install-OcrModels([string]$Project) {
    $languageDir = Join-Path $Project 'data\tessdata'
    if (-not (Test-Path -LiteralPath $languageDir -PathType Container)) {
        New-Item -Path $languageDir -ItemType Directory -Force | Out-Null
    }
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    foreach ($name in @('eng', 'rus')) {
        $destination = Join-Path $languageDir ($name + '.traineddata')
        if (Test-Path -LiteralPath $destination) {
            Write-Host "OCR language '$name' already exists. Preserving existing file and ownership." -ForegroundColor Cyan
            continue
        }
        $temporary = Join-Path $languageDir ('.clicknova-' + [guid]::NewGuid().ToString('N') + '.partial')
        try {
            $url = 'https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/main/' + $name + '.traineddata'
            Write-Host "Downloading OCR model: $name" -ForegroundColor Cyan
            $client = New-Object System.Net.WebClient
            try { $client.DownloadFile($url, $temporary) } finally { $client.Dispose() }
            $length = (Get-Item -LiteralPath $temporary).Length
            if ($length -lt 100000 -or $length -gt 31457280) { throw "Invalid model size for $name ($length bytes)." }
            # File.Move cannot overwrite; if another program created it, preserve theirs.
            if (Test-Path -LiteralPath $destination) {
                Write-Host "Another process installed $name while downloading; keeping its version." -ForegroundColor Yellow
                continue
            }
            [IO.File]::Move($temporary, $destination)
            $checksum = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
            $ledger = Join-Path $Project 'data\owned-components.tsv'
            $records = @()
            if (Test-Path -LiteralPath $ledger -PathType Leaf) {
                $records = @(Get-Content -LiteralPath $ledger -Encoding UTF8 | Where-Object { $_.Trim() })
            }
            $path = 'tessdata/' + $name + '.traineddata'
            $records = @($records | Where-Object { -not $_.EndsWith("`t$path") })
            $records += ($checksum + "`t" + $path)
            $ledgerTemp = $ledger + '.clicknova-' + [guid]::NewGuid().ToString('N')
            try {
                [IO.File]::WriteAllLines($ledgerTemp, [string[]]$records, ([System.Text.UTF8Encoding]::new($false)))
                # Replacing the local ledger is permitted only for model downloads owned by ClickNova.
                Move-Item -LiteralPath $ledgerTemp -Destination $ledger -Force
            } finally {
                if (Test-Path -LiteralPath $ledgerTemp) { Remove-Item -LiteralPath $ledgerTemp -Force }
            }
        } finally {
            if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
        }
    }
}

 $instanceLock = $null
try {
    $localTools = Join-Path (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path 'tools'
    if (-not (Test-Path -LiteralPath $localTools -PathType Container)) {
        New-Item -ItemType Directory -Path $localTools -Force | Out-Null
    }
    $instanceLock = [IO.FileStream]::new((Join-Path $localTools 'clicknova-visual-installer.lock'),
        [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    Send-Stage 1 'Checking development tools and existing dependencies'
    $project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
    $cmake = Get-Tool 'cmake.exe'
    if (-not $cmake) { $cmake = Get-Tool 'cmake' }
    if (-not $cmake) { throw 'CMake is not installed or not in PATH. Install CMake first.' }
    if (-not (Check-File $project 'CMakeLists.txt')) { throw 'ClickNova source tree is missing CMakeLists.txt.' }
    # Fail before downloading large packages if Qt is unavailable.
    $qtCandidates = @()
    foreach ($envName in @('CLICKNOVA_QT_ROOT','QTDIR','QT_ROOT','Qt6_DIR')) {
        $value = [Environment]::GetEnvironmentVariable($envName)
        if ($value) {
            if ($envName -eq 'Qt6_DIR' -and $value -match '[\\/]lib[\\/]cmake[\\/]Qt6$') {
                $value = [IO.Path]::GetFullPath((Join-Path $value '..\..\..'))
            }
            $qtCandidates += $value
        }
    }
    foreach ($drive in @(Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Root)) {
        $qtBase = Join-Path $drive 'Qt'
        if (Test-Path -LiteralPath $qtBase -PathType Container) {
            Get-ChildItem -LiteralPath $qtBase -Directory -ErrorAction SilentlyContinue |
                ForEach-Object { $qtCandidates += (Join-Path $_.FullName 'msvc2022_64') }
        }
    }
    $qtFound = $false
    foreach ($candidate in $qtCandidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'lib\cmake\Qt6\Qt6Config.cmake') -PathType Leaf) {
            $qtFound = $true
            break
        }
    }
    if (-not $qtFound) {
        throw 'Qt 6 MSVC kit not found. Install Qt msvc2022_64 or set CLICKNOVA_QT_ROOT before downloading dependencies.'
    }
    $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -ne 0 -or -not $vs) {
            throw 'MSVC C++ x64 build tools are missing. Install the C++ desktop workload in Visual Studio Installer.'
        }
    }

    # Default to a private, project-local vcpkg. A shared/system vcpkg is
    # reused ONLY if its location was explicitly passed by the developer.
    # In particular, never modify VCPKG_ROOT from another application.
    $provided = -not [string]::IsNullOrWhiteSpace($VcpkgRoot)
    $chosen = ''
    if ($provided) {
        if (-not (Test-ReadyVcpkg $VcpkgRoot)) {
            throw "Explicitly selected vcpkg is missing or incomplete: $VcpkgRoot"
        }
        $chosen = (Resolve-Path -LiteralPath $VcpkgRoot).Path
    } else {
        $localOld = Join-Path $project 'tools\vcpkg'
        if (Test-Path -LiteralPath $localOld) {
            Write-Host 'Preserving the pre-existing tools/vcpkg checkout; ClickNova will use an isolated copy.' -ForegroundColor Yellow
        }
        $managedCandidate = Get-ManagedRoot $project
        if (Test-ReadyVcpkg $managedCandidate) {
            $chosen = (Resolve-Path -LiteralPath $managedCandidate).Path
        }
    }
    if (-not $chosen) {
        $chosen = Get-ManagedRoot $project
        Send-Stage 2 'Downloading a clean official vcpkg checkout'
        if (-not (Test-ReadyVcpkg $chosen)) { Download-ManagedVcpkg $chosen }
        # vcpkg itself honors VCPKG_ROOT; override it for this process only.
        $env:VCPKG_ROOT = $chosen
        Send-Stage 3 'Bootstrapping vcpkg'
        if (-not (Test-CompleteCheckout $chosen)) { throw 'vcpkg checkout is incomplete; cannot bootstrap.' }
        $code = Invoke-Native (Join-Path $chosen 'bootstrap-vcpkg.bat') @('-disableMetrics')
        if ($code -ne 0 -or -not (Test-ReadyVcpkg $chosen)) { throw 'vcpkg bootstrap failed. Review the log; no pre-existing libraries were removed.' }
    } else {
        Send-Stage 2 'Reusing an existing complete vcpkg installation'
        Send-Stage 3 'Validating vcpkg executable'
    }
    # Even when another app has set VCPKG_ROOT, never silently use its tree.
    $env:VCPKG_ROOT = $chosen
    # Confirm that vcpkg.exe can actually execute, not just that a file exists.
    $code = Invoke-Native (Join-Path $chosen 'vcpkg.exe') @('version')
    if ($code -ne 0) { throw 'vcpkg.exe failed its version check.' }
    Write-Host "Using vcpkg at $chosen" -ForegroundColor Green
    Send-Stage 4 'Downloading and compiling OpenCV and Tesseract'
    Install-Packages (Join-Path $chosen 'vcpkg.exe')
    Send-Stage 5 'Rebuilding ClickNova with visual recognition'
    $buildResult = @{ OpenCV = $false; OCR = $false }
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        # Isolate native-tool warnings and script exit status inside a dedicated
        # PowerShell process. PowerShell 5.1 can promote a harmless CMake
        # stderr warning to a terminating error if a .ps1 is called directly.
        & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build-windows.ps1') -VcpkgRoot $chosen 2>&1 | ForEach-Object {
            $line = [string]$_
            Write-Host $line
            if ($line -match 'ClickNova visual recognition: OpenCV enabled') { $buildResult.OpenCV = $true }
            if ($line -match 'ClickNova visual recognition: Tesseract OCR enabled') { $buildResult.OCR = $true }
        }
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($code -ne 0) { throw 'Windows build or CTest failed. See build-vs2026-qt logs.' }
    if (-not $buildResult.OpenCV -or -not $buildResult.OCR) {
        throw 'Build finished but OpenCV/Tesseract were not both confirmed enabled. Do not report installation success.'
    }
    Send-Stage 6 'Downloading OCR language models and verifying installation'
    $exe = Join-Path $project 'build-vs2026-qt\Release\ClickNova.exe'
    $marker = Join-Path $project 'build-vs2026-qt\.clicknova-tests-passed'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf) -or -not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        throw 'Build or CTest success marker is missing.'
    }
    Install-OcrModels $project
    # Confirm completion independently from redirected stdout. Use a unique
    # result filename per GUI run, after all build/CTest/OCR checks passed.
    if ($ResultFile) {
        $allowed = [IO.Path]::GetFullPath((Join-Path $project 'data\installer')).TrimEnd('\', '/') + '\'
        $resultPath = [IO.Path]::GetFullPath($ResultFile)
        if (-not $resultPath.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Installer result file must be located in the private data/installer directory.'
        }
        Set-Content -LiteralPath $resultPath -Value 'SUCCESS' -Encoding ASCII
    }
    Write-Host '::CLICKNOVA_RESULT::SUCCESS' -ForegroundColor Green
    Write-Host 'Visual recognition enabled. OCR language models can be downloaded in Settings > Components.' -ForegroundColor Green
    exit 0
} catch {
    Write-Host ('::CLICKNOVA_RESULT::FAILED::' + $_.Exception.Message) -ForegroundColor Red
    Write-Host ('VISUAL SETUP ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
} finally {
    if ($null -ne $instanceLock) { $instanceLock.Dispose() }
}
