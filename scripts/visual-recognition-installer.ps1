# SPDX-License-Identifier: Apache-2.0
# Windows-only visual installer UI for optional developer dependencies.
param([switch]$WaitForApplicationExit)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$worker = Join-Path $PSScriptRoot 'enable-visual-recognition.ps1'
if (-not (Test-Path -LiteralPath $worker -PathType Leaf)) {
    [System.Windows.Forms.MessageBox]::Show('Не найден скрипт установки компонентов.', 'ClickNova') | Out-Null
    exit 1
}

# Local diagnostic output is application data, NEVER part of the source package.
$logDir = Join-Path $project 'data\installer'
if (-not (Test-Path -LiteralPath $logDir -PathType Container)) {
    New-Item -Path $logDir -ItemType Directory -Force | Out-Null
}

$form = New-Object System.Windows.Forms.Form
$form.Text = 'ClickNova — установка OpenCV / Tesseract'
$form.Size = [System.Drawing.Size]::new(760, 575)
$form.MinimumSize = [System.Drawing.Size]::new(650, 475)
$form.StartPosition = 'CenterScreen'
$form.Font = [System.Drawing.Font]::new('Segoe UI', 9)
$form.MaximizeBox = $false

$heading = New-Object System.Windows.Forms.Label
$heading.Text = 'Установка дополнительных компонентов'
$heading.AutoSize = $false
$heading.Font = [System.Drawing.Font]::new('Segoe UI', 13, [System.Drawing.FontStyle]::Bold)
$heading.SetBounds(18, 14, 710, 32)
$heading.Anchor = 'Top,Left,Right'
$form.Controls.Add($heading)

$intro = New-Object System.Windows.Forms.Label
$intro.Text = 'ClickNova автоматически проверит vcpkg, загрузит недостающие файлы, соберёт OpenCV / OCR и запустит тесты. Это настройка ИСХОДНОГО ПРОЕКТА: нужны CMake, Qt и MSVC. Процесс может занять долгое время и много места на диске.'
$intro.SetBounds(18, 53, 706, 68)
$intro.Anchor = 'Top,Left,Right'
$form.Controls.Add($intro)

$stage = New-Object System.Windows.Forms.Label
$stage.Text = 'Подготовка...'
$stage.Font = [System.Drawing.Font]::new('Segoe UI', 10, [System.Drawing.FontStyle]::Bold)
$stage.SetBounds(18, 120, 700, 26)
$stage.Anchor = 'Top,Left,Right'
$form.Controls.Add($stage)

$stepProgress = New-Object System.Windows.Forms.ProgressBar
$stepProgress.Minimum = 0
$stepProgress.Maximum = 6
$stepProgress.Value = 0
$stepProgress.Style = 'Continuous'
$stepProgress.SetBounds(18, 150, 705, 20)
$stepProgress.Anchor = 'Top,Left,Right'
$form.Controls.Add($stepProgress)

$activityProgress = New-Object System.Windows.Forms.ProgressBar
$activityProgress.Style = 'Marquee'
$activityProgress.MarqueeAnimationSpeed = 25
$activityProgress.SetBounds(18, 179, 705, 10)
$activityProgress.Anchor = 'Top,Left,Right'
$form.Controls.Add($activityProgress)

$note = New-Object System.Windows.Forms.Label
$note.Text = 'Индикатор показывает этапы. Загрузка и компиляция не имеют надёжного общего процента.'
$note.ForeColor = [System.Drawing.Color]::DimGray
$note.SetBounds(18, 195, 705, 24)
$note.Anchor = 'Top,Left,Right'
$form.Controls.Add($note)

$output = New-Object System.Windows.Forms.TextBox
$output.Multiline = $true
$output.ReadOnly = $true
$output.WordWrap = $false
$output.ScrollBars = 'Both'
$output.Font = [System.Drawing.Font]::new('Consolas', 8.5)
$output.SetBounds(18, 228, 705, 255)
$output.Anchor = 'Top,Bottom,Left,Right'
$form.Controls.Add($output)

$retry = New-Object System.Windows.Forms.Button
$retry.Text = 'Повторить'
$retry.SetBounds(18, 495, 110, 30)
$retry.Anchor = 'Bottom,Left'
$retry.Enabled = $false
$form.Controls.Add($retry)

$cancel = New-Object System.Windows.Forms.Button
$cancel.Text = 'Отменить'
$cancel.SetBounds(615, 495, 108, 30)
$cancel.Anchor = 'Bottom,Right'
$form.Controls.Add($cancel)

$openLog = New-Object System.Windows.Forms.Button
$openLog.Text = 'Открыть журнал'
$openLog.SetBounds(140, 495, 135, 30)
$openLog.Anchor = 'Bottom,Left'
$form.Controls.Add($openLog)

$phases = @{
    1 = 'Проверка среды и существующих библиотек'
    2 = 'Загрузка / выбор vcpkg'
    3 = 'Подготовка vcpkg'
    4 = 'Загрузка и сборка OpenCV / Tesseract (может занять часы)'
    5 = 'Пересборка ClickNova и проверка GUI'
    6 = 'Проверка успешной установки'
}
$state = [pscustomobject]@{
    Process = $null; Log = ''; Err = ''; Result = ''
    ReadOffset = [long]0; Buffer = ''; Completed = $false
    Success = $false; Cancelled = $false; SeenSuccess = $false; SeenFailure = $false
    ExitObservedAt = [DateTime]::MinValue
}
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 500

function Append-Lines([string]$Text) {
    if ([string]::IsNullOrEmpty($Text)) { return }
    $output.AppendText($Text)
    if ($output.TextLength -gt 55000) {
        $output.Text = $output.Text.Substring($output.TextLength - 42000)
    }
    $output.SelectionStart = $output.TextLength
    $output.ScrollToCaret()
}
function Poll-InstallerLog {
    if (-not (Test-Path -LiteralPath $state.Log -PathType Leaf)) { return }
    $stream = [IO.FileStream]::new($state.Log, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        if ($stream.Length -lt $state.ReadOffset) { $state.ReadOffset = 0 }
        $stream.Seek($state.ReadOffset, [IO.SeekOrigin]::Begin) | Out-Null
        $remaining = [int][Math]::Min(131072, $stream.Length - $state.ReadOffset)
        if ($remaining -le 0) { return }
        $bytes = New-Object byte[] $remaining
        $count = $stream.Read($bytes, 0, $remaining)
        $state.ReadOffset += $count
        $newText = [System.Text.Encoding]::UTF8.GetString($bytes, 0, $count)
    } finally { $stream.Dispose() }
    $combined = $state.Buffer + $newText
    $lines = $combined -split "`n"
    $completeCount = $lines.Length
    if ($combined.EndsWith("`n")) { $state.Buffer = '' }
    else { $state.Buffer = $lines[$lines.Length - 1]; $completeCount-- }
    $visibleLines = @()
    for ($i = 0; $i -lt $completeCount; ++$i) {
        $line = $lines[$i].TrimEnd("`r")
        if ($line -match '^::CLICKNOVA_STAGE::([1-6])::') {
            $index = [int]$Matches[1]
            $stage.Text = ('Этап {0}/6 — {1}' -f $index, $phases[$index])
            $stepProgress.Value = [Math]::Min(5, $index - 1)
        } elseif ($line -match '^::CLICKNOVA_RESULT::SUCCESS\s*$') {
            $state.SeenSuccess = $true
        } elseif ($line -match '^::CLICKNOVA_RESULT::FAILED(?:::|$)') {
            $state.SeenFailure = $true
            $visibleLines += $line
        } elseif ($line -notmatch '^::CLICKNOVA_RESULT::') {
            $visibleLines += $line
        }
    }
    if ($visibleLines.Count -gt 0) { Append-Lines (($visibleLines -join "`r`n") + "`r`n") }
}
# The worker writes a per-run result file after all post-build checks. It is
# independent of stdout redirection, which can finish flushing after HasExited.
function Test-InstallerSuccess {
    if ($state.Cancelled -or $state.SeenFailure) { return $false }
    if (Test-Path -LiteralPath $state.Result -PathType Leaf) {
        $result = (Get-Content -LiteralPath $state.Result -Raw -ErrorAction Stop).Trim()
        if ($result -ceq 'SUCCESS') { return $true }
        if ($result -ceq 'FAILED') { $state.SeenFailure = $true }
    }
    if ($state.SeenFailure) { return $false }
    if ($state.SeenSuccess) { return $true }
    # An explicit worker SUCCESS can be in the last redirected bytes even if
    # Poll-InstallerLog has not consumed them yet (Windows PowerShell 5.1).
    if (Test-Path -LiteralPath $state.Log -PathType Leaf) {
        $tail = @(Get-Content -LiteralPath $state.Log -Tail 24 -ErrorAction SilentlyContinue)
        foreach ($line in $tail) {
            if ($line -match '^::CLICKNOVA_RESULT::FAILED(?:::|$)') { $state.SeenFailure = $true; return $false }
            if ($line -match '^::CLICKNOVA_RESULT::SUCCESS\s*$') { return $true }
        }
    }
    return $false
}
function Finish-Installer([bool]$Success, [string]$Message) {
    $state.Completed = $true
    $state.Success = $Success
    $timer.Stop()
    $activityProgress.MarqueeAnimationSpeed = 0
    $activityProgress.Style = 'Continuous'
    $activityProgress.Value = if ($Success) { 100 } else { 0 }
    $retry.Enabled = -not $Success
    $cancel.Text = 'Закрыть'
    if ($Success) { $stepProgress.Value = 6 }
    $stage.Text = $Message
    Append-Lines ("`r`n" + $Message + "`r`n")
}
function Start-Installer {
    $state.Process = $null
    $state.ReadOffset = 0
    $state.Buffer = ''
    $state.Completed = $false
    $state.Success = $false
    $state.Cancelled = $false
    $state.SeenSuccess = $false
    $state.SeenFailure = $false
    $state.ExitObservedAt = [DateTime]::MinValue
    $retry.Enabled = $false
    $cancel.Text = 'Отменить'
    $output.Clear()
    $stepProgress.Value = 0
    $activityProgress.Style = 'Marquee'
    $activityProgress.MarqueeAnimationSpeed = 25
    $stage.Text = 'Запуск установщика...'
    $unique = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
    $state.Log = Join-Path $logDir ('visual-install-' + $unique + '.stdout.log')
    $state.Err = Join-Path $logDir ('visual-install-' + $unique + '.stderr.log')
    $state.Result = Join-Path $logDir ('visual-install-' + $unique + '.result')
    $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $worker.Replace('"', '""') + '" -ResultFile "' + $state.Result.Replace('"', '""') + '"'
    if ($WaitForApplicationExit) { $arguments += ' -WaitForApplicationExit' }
    try {
        $state.Process = Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -PassThru -WindowStyle Hidden -RedirectStandardOutput $state.Log -RedirectStandardError $state.Err
        $timer.Start()
    } catch {
        Finish-Installer $false ('Не удалось запустить установку: ' + $_.Exception.Message)
    }
}
$timer.Add_Tick({
    try {
        Poll-InstallerLog
        if ($null -ne $state.Process) {
            $state.Process.Refresh()
            if ($state.Process.HasExited) {
                if ($state.ExitObservedAt -eq [DateTime]::MinValue) {
                    $state.ExitObservedAt = [DateTime]::UtcNow
                }
                # Start-Process redirects through streams which can be flushed
                # shortly after the child process exits. Do not fail early.
                Poll-InstallerLog
                $exitCode = $state.Process.ExitCode
                $confirmed = Test-InstallerSuccess
                if ($confirmed) {
                    if ($exitCode -ne 0) {
                        Append-Lines ("`r`nПримечание: процесс вернул код $exitCode, но проверенный этап установки сообщил SUCCESS.`r`n")
                    }
                    Finish-Installer $true 'Готово! OpenCV и Tesseract включены в ClickNova, тесты пройдены.'
                } elseif ($state.SeenFailure -or
                          ([DateTime]::UtcNow - $state.ExitObservedAt).TotalSeconds -ge 8) {
                    if (Test-Path -LiteralPath $state.Err) {
                        $tail = @(Get-Content -LiteralPath $state.Err -Tail 14 -ErrorAction SilentlyContinue)
                        if ($tail.Count -gt 0) { Append-Lines ((@('Ошибки установщика:') + $tail) -join "`r`n") }
                    }
                    Append-Lines ("`r`nДиагностика: код процесса $exitCode, SUCCESS получен: $($state.SeenSuccess), FAILED получен: $($state.SeenFailure).`r`n")
                    $reason = if ($state.Cancelled) { 'Установка отменена. Существующие файлы сохранены.' }
                              else { 'Установка не завершена. Проверьте журнал и нажмите «Повторить».' }
                    Finish-Installer $false $reason
                }
            }
        }
    } catch {
        Finish-Installer $false ('Ошибка отображения журнала: ' + $_.Exception.Message)
    }
})
$retry.Add_Click({ Start-Installer })
$openLog.Add_Click({
    if (Test-Path -LiteralPath $state.Log) { Start-Process notepad.exe -ArgumentList ('"' + $state.Log + '"') | Out-Null }
})
$cancel.Add_Click({
    if ($state.Completed) { $form.Close(); return }
    $answer = [System.Windows.Forms.MessageBox]::Show(
        'Остановить установку? Уже установленные зависимости останутся на диске. Будут завершены только процессы, запущенные этим установщиком.',
        'ClickNova', [System.Windows.Forms.MessageBoxButtons]::YesNo, [System.Windows.Forms.MessageBoxIcon]::Question)
    if ($answer -ne [System.Windows.Forms.DialogResult]::Yes) { return }
    $state.Cancelled = $true
    if ($null -ne $state.Process -and -not $state.Process.HasExited) {
        & taskkill.exe /PID $state.Process.Id /T /F | Out-Null
    }
    Finish-Installer $false 'Установка остановлена по запросу пользователя.'
})
$form.Add_FormClosing({
    param($sender, $args)
    if (-not $state.Completed) {
        $args.Cancel = $true
        $cancel.PerformClick()
    }
})
$form.Add_Shown({ Start-Installer })
[void]$form.ShowDialog()
$timer.Dispose()
if ($state.Success) { exit 0 }
exit 1
