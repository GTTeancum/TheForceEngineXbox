param(
    [Parameter(Mandatory=$true)]
    [string]$Label,
    [Parameter(Mandatory=$true)]
    [string]$ConfigPath,
    [Parameter(Mandatory=$true)]
    [string]$InstanceDir,
    [int]$MonitorPort = 4593,
    [int]$DurationSeconds = 600,
    [int]$PollIntervalSeconds = 30,
    [int]$ScreenshotAtSeconds = 540,
    [string]$RunRoot = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$MapPath = Join-Path $RepoRoot "build\xbox\release\default.exe.map"
$XbePath = Join-Path $RepoRoot "build\xbox\release\default.xbe"
$PollScript = Join-Path $RepoRoot "tools\poll_tfe_xemu_ram_log.py"
$NativeScreenshotScript = "C:\Programming\GitHub\OpenJKDF2ogx\scripts\xbox\xemu_native_screenshot.py"

if ([string]::IsNullOrWhiteSpace($RunRoot)) {
    $RunRoot = Join-Path $RepoRoot "build\xemu\tfe_soak_runs"
}

$Stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$SafeLabel = $Label -replace '[^A-Za-z0-9_.-]', '_'
$RunDir = Join-Path $RunRoot "$Stamp-$SafeLabel"
$PollDir = Join-Path $RunDir "ram"
$ScreenshotDir = Join-Path $RunDir "screenshots"
$SummaryPath = Join-Path $RunDir "summary.txt"
$XemuExe = Join-Path $InstanceDir "xemu.exe"
$ScreenshotConfigLine = Get-Content -LiteralPath $ConfigPath |
    Where-Object { $_ -match '^\s*screenshot_dir\s*=' } |
    Select-Object -First 1
if (!$ScreenshotConfigLine) {
    throw "XEMU screenshot_dir missing from config: $ConfigPath"
}
$XemuScreenshotDir = ($ScreenshotConfigLine -replace '^\s*screenshot_dir\s*=\s*[''"]', '') -replace '[''"]\s*$', ''

function Require-Path([string]$Path, [string]$Name) {
    if (!(Test-Path -LiteralPath $Path)) {
        throw "$Name not found: $Path"
    }
}

function Get-XemuProcessForConfig([string]$Path) {
    $needle = [System.IO.Path]::GetFullPath($Path)
    Get-CimInstance Win32_Process -Filter "Name = 'xemu.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 }
}

function Stop-XemuForConfig([string]$Path) {
    Get-XemuProcessForConfig $Path | ForEach-Object {
        Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
    }
}

function Count-Matches([string[]]$Paths, [string[]]$Patterns) {
    $count = 0
    foreach ($path in $Paths) {
        if (Test-Path -LiteralPath $path) {
            $count += @(Select-String -Path $path -Pattern $Patterns -ErrorAction SilentlyContinue).Count
        }
    }
    return $count
}

function Test-AnyPattern([string[]]$Paths, [string]$Pattern) {
    foreach ($path in $Paths) {
        if ((Test-Path -LiteralPath $path) -and
            (Select-String -Path $path -Pattern $Pattern -Quiet -ErrorAction SilentlyContinue)) {
            return $true
        }
    }
    return $false
}

function Invoke-NativeScreenshot([int]$ProcId, [string]$StablePath) {
    $out = & python $NativeScreenshotScript --pid ([string]$ProcId) --xemu-exe $XemuExe --screenshot-dir $XemuScreenshotDir --timeout 8 2>&1
    $text = ($out | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw $text
    }
    $json = $text | ConvertFrom-Json
    if (!$json.ok -or !$json.path -or !(Test-Path -LiteralPath $json.path)) {
        throw $text
    }
    Copy-Item -LiteralPath $json.path -Destination $StablePath -Force
    return "captured $StablePath via native XEMU screenshot ($($json.detail))"
}

Require-Path $ConfigPath "XEMU config"
Require-Path $InstanceDir "XEMU instance"
Require-Path $XemuExe "XEMU executable"
Require-Path $MapPath "TFE map"
Require-Path $XbePath "TFE XBE"
Require-Path $PollScript "TFE RAM poller"
Require-Path $NativeScreenshotScript "XEMU native screenshot helper"

New-Item -ItemType Directory -Force -Path $RunDir, $PollDir, $ScreenshotDir | Out-Null

$start = Get-Date
$proc = $null
$pollIndex = 0
$pollOkCount = 0
$lastPollPath = ""
$lastPollText = ""
$screenshotPath = ""
$screenshotError = ""
$aliveAtEnd = $false

try {
    Stop-XemuForConfig $ConfigPath
    Start-Sleep -Milliseconds 500

    $args = @("-config_path", $ConfigPath, "-monitor", "tcp:127.0.0.1:$MonitorPort,server,nowait")
    $proc = Start-Process -FilePath $XemuExe -ArgumentList $args -WorkingDirectory $InstanceDir -WindowStyle Hidden -PassThru

    $deadline = $start.AddSeconds($DurationSeconds)
    $screenshotDue = if ($ScreenshotAtSeconds -gt 0) { $start.AddSeconds($ScreenshotAtSeconds) } else { [DateTime]::MaxValue }
    $screenshotTaken = $false

    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds $PollIntervalSeconds
        if ($proc.HasExited) {
            break
        }

        $pollIndex++
        $elapsed = [int]((Get-Date) - $start).TotalSeconds
        $pollOut = Join-Path $RunDir ("poll_{0:D3}_{1:D4}s.txt" -f $pollIndex, $elapsed)
        $pollErr = Join-Path $RunDir ("poll_{0:D3}_{1:D4}s.err.txt" -f $pollIndex, $elapsed)
        $pollArgs = @(
            $PollScript,
            "--ports", ([string]$MonitorPort),
            "--map", $MapPath,
            "--xbe", $XbePath,
            "--out-dir", $PollDir,
            "--phys-delta", "0",
            "--timeout", "5"
        )
        $pollStartedAt = Get-Date
        $pollProc = Start-Process -FilePath "python" -ArgumentList $pollArgs -NoNewWindow -PassThru `
            -RedirectStandardOutput $pollOut -RedirectStandardError $pollErr
        if (!$pollProc.WaitForExit(20000)) {
            Stop-Process -Id $pollProc.Id -Force -ErrorAction SilentlyContinue
            Set-Content -LiteralPath $pollErr -Value "poll timed out after 20 seconds" -Encoding ASCII
        }
        $pollProc.Refresh()
        $latestPath = Join-Path $PollDir ("port{0}_tfe_ram_log.txt" -f $MonitorPort)
        if (Test-Path -LiteralPath $latestPath) {
            $latestItem = Get-Item -LiteralPath $latestPath
            $freshEnough = $latestItem.LastWriteTime -ge $pollStartedAt.AddSeconds(-5)
            if ($latestItem.Length -gt 0 -and $freshEnough) {
                $pollOkCount++
                $snapshot = Join-Path $RunDir ("ram_poll_{0:D3}_{1:D4}s.txt" -f $pollIndex, $elapsed)
                Copy-Item -LiteralPath $latestPath -Destination $snapshot -Force
                $lastPollPath = $snapshot
                $lastPollText = Get-Content -LiteralPath $snapshot -Raw -ErrorAction SilentlyContinue
            }
        }

        if (!$screenshotTaken -and (Get-Date) -ge $screenshotDue) {
            try {
                $screenshotPath = Join-Path $ScreenshotDir ("shot_{0:D4}s.png" -f $elapsed)
                $detail = Invoke-NativeScreenshot $proc.Id $screenshotPath
                Set-Content -LiteralPath (Join-Path $RunDir "screenshot.txt") -Value $detail -Encoding ASCII
                $screenshotTaken = $true
            }
            catch {
                $screenshotError = $_.Exception.Message
                Set-Content -LiteralPath (Join-Path $RunDir "screenshot.err.txt") -Value $screenshotError -Encoding ASCII
                $screenshotTaken = $true
            }
        }
    }

    $aliveAtEnd = $proc -and !$proc.HasExited
}
finally {
    if ($proc -and !$proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500
    }
    Stop-XemuForConfig $ConfigPath
}

$end = Get-Date
$duration = [int](($end - $start).TotalSeconds)
$ramLogs = @(Get-ChildItem -LiteralPath $RunDir -Filter "ram_poll_*.txt" -File -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName)
$fatalPatterns = @(
    "Received Exception",
    "FATAL",
    "Critical",
    "Out of memory",
    "E_OUTOFMEMORY",
    "Unhandled",
    "D3D Error",
    "Cannot initialise",
    "Cannot run game",
    "Game data not found",
    "failed hr=0x"
)
$fatalCount = Count-Matches $ramLogs $fatalPatterns
$saveOk = Test-AnyPattern $ramLogs "transition save end .* result=1"
$loadOk = Test-AnyPattern $ramLogs "transition load end .* result=1"
$levelTransitionOk = Test-AnyPattern $ramLogs "level transition complete"
$secbaseOk = Test-AnyPattern $ramLogs "level ready cycle=0 level='SECBASE'"
$talayOk = Test-AnyPattern $ramLogs "level ready cycle=1 level='TALAY'"
$selected4x3 = Test-AnyPattern $ramLogs "selected=4:3"
$selectedWide = Test-AnyPattern $ramLogs "selected=16:9"

$summary = @(
    "label=$Label",
    "runDir=$RunDir",
    "start=$($start.ToString('s'))",
    "durationSeconds=$duration",
    "targetDurationSeconds=$DurationSeconds",
    "pollIntervalSeconds=$PollIntervalSeconds",
    "monitorPort=$MonitorPort",
    "config=$ConfigPath",
    "xemuExe=$XemuExe",
    "aliveAtEnd=$aliveAtEnd",
    "pollCount=$pollIndex",
    "pollOkCount=$pollOkCount",
    "fatalCount=$fatalCount",
    "saveOk=$saveOk",
    "loadOk=$loadOk",
    "levelTransitionOk=$levelTransitionOk",
    "secbaseOk=$secbaseOk",
    "talayOk=$talayOk",
    "selected4x3=$selected4x3",
    "selectedWide=$selectedWide",
    "screenshotPath=$screenshotPath",
    "screenshotError=$screenshotError",
    "lastPollPath=$lastPollPath",
    ""
)
if ($lastPollText) {
    $summary += "lastPollTail:"
    $summary += (($lastPollText -split "`r?`n") | Select-Object -Last 80)
}

Set-Content -LiteralPath $SummaryPath -Value $summary -Encoding UTF8
Get-Content -LiteralPath $SummaryPath

if ($pollOkCount -eq 0 -or $fatalCount -gt 0 -or !$saveOk -or !$loadOk -or !$levelTransitionOk) {
    exit 1
}
