# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

<#
.SYNOPSIS
Runs one round of an A/B plan: starts the round's package and variant (launch.ps1), waits while
you go to the benchmark spot, measures (bench.ps1) and says when to close the emulator.

.DESCRIPTION
The plan is a JSON file listing the packages and the runs in order (see README.md). A run counts
as done once <OutDir>\<label>-*\summary.md exists, so a failed run is offered again.

.EXAMPLE
pwsh -File tools/nx2/perf/round.ps1           # the next run not done yet
pwsh -File tools/nx2/perf/round.ps1 -List
pwsh -File tools/nx2/perf/round.ps1 -Label msvc-2
#>
param(
    [string] $Label = '',
    [switch] $List,
    [string] $Plan = (Join-Path $env:USERPROFILE 'lichii-perf\plan.json'),
    [double] $Seconds = 60,
    [double] $Settle = 20,
    [string] $OutDir = (Join-Path $env:USERPROFILE 'lichii-perf\results')
)

$ErrorActionPreference = 'Stop'
$config = Get-Content -Raw $Plan | ConvertFrom-Json

function Test-Done([string] $name) {
    $runs = Get-ChildItem -Path $OutDir -Directory -Filter "$name-*" -ErrorAction SilentlyContinue
    return [bool]($runs | Where-Object { Test-Path (Join-Path $_.FullName 'summary.md') })
}

if ($List) {
    $index = 0
    $config.runs | ForEach-Object {
        $index++
        [pscustomobject]@{ '#' = $index; label = $_.label; package = $_.package; variant = $_.variant
                           sample = [bool]$_.sample; done = (Test-Done $_.label) }
    } | Format-Table -AutoSize
    return
}

$run = if ($Label) {
    $config.runs | Where-Object label -eq $Label | Select-Object -First 1
} else {
    $config.runs | Where-Object { -not (Test-Done $_.label) } | Select-Object -First 1
}
if ($null -eq $run) {
    if ($Label) { throw "no run labelled '$Label' in $Plan" }
    Write-Host 'Every run in the plan is done.' -ForegroundColor Green
    return
}
$exe = $config.packages.($run.package)
if (-not $exe) { throw "the plan has no package named '$($run.package)'" }
# Runs that must not share the machine with some programs (their VRAM and capture hooks).
if ($run.require_closed) {
    $open = Get-Process -Name $run.require_closed -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty Name -Unique
    if ($open) {
        Write-Host "Run $($run.label) needs these programs closed first: $($open -join ', ')" -ForegroundColor Red
        Write-Host 'Quit them (also from the tray icons next to the clock), then run round.ps1 again.' -ForegroundColor Red
        exit 1
    }
}

Write-Host ("Run {0}: {1} ({2}), variant {3}{4}" -f $run.label, $run.package, $exe, $run.variant,
            $(if ($run.sample) { ', with thread samples' } else { '' })) -ForegroundColor Cyan
if ($run.before) {
    # Lichii's window covers this terminal once it starts: read the note first.
    Write-Host "Before opening the game: $($run.before)" -ForegroundColor Yellow
    Read-Host 'Press Enter to start Lichii'
}

# Game settings changed in the UI: every run expects the plan's defaults unless it says otherwise.
$expect = @{}
if ($config.expect) {
    $config.expect.PSObject.Properties | ForEach-Object { $expect[$_.Name] = $_.Value }
}
if ($run.expect) {
    $run.expect.PSObject.Properties | ForEach-Object { $expect[$_.Name] = $_.Value }
}
$bench = Join-Path $PSScriptRoot 'bench.ps1'
# The plan's reference_view is a view.png of the benchmark spot.
$reference = if ($config.reference_view) { [string]$config.reference_view } else { '' }
# An in-session A/B compares blocks of one session: any spot where Link stands still will do.
# A moving run measures ordinary play instead: no spot, and more, shorter A/B blocks.
if ($run.any_spot -or $run.moving) { $reference = '' }
# A run with "ab": "<switch>" alternates that experiment switch in blocks inside the session.
$ab = if ($run.ab) { [string]$run.ab } else { '' }
$check = {
    param([string] $view)
    try {
        & $bench -Label $run.label -OutDir $OutDir -Expect $expect -ReferenceView $view -AB $ab -CheckOnly
    } catch {
        Write-Host "Run $($run.label) cannot be measured: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host 'Close Lichii and run round.ps1 again to repeat it.' -ForegroundColor Red
        exit 1
    }
}

if (-not ('LichiiPerf.DebugClient' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'LichiiPerf.cs')
}
# A base run can reuse the Lichii a refused run left open (launch.ps1 started it: the debug
# server answers), so a wrong spot costs a walk, not a restart.
$running = Get-Process -Name lichii, eden-cli -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq (Resolve-Path $exe).Path }
$reuse = $false
$started = Join-Path $OutDir 'running.txt'
$startedAs = if (Test-Path $started) { (Get-Content $started -Raw).Trim() } else { '' }
if ($running -and $run.variant -eq 'base' -and $startedAs -eq "$($running[0].Id) base") {
    try {
        [LichiiPerf.DebugClient]::new('127.0.0.1', 6545, 1000).Dispose()
        $reuse = $true
        Write-Host 'Using the Lichii that is already open.' -ForegroundColor Cyan
    } catch {}
}
if (-not $reuse) {
    & (Join-Path $PSScriptRoot 'launch.ps1') -Exe $exe -Variant $run.variant
}

# The debug server starts with the game: check its settings at once, before the walk to the spot.
Write-Host 'Waiting for the game to start...'
while ($true) {
    try {
        [LichiiPerf.DebugClient]::new('127.0.0.1', 6545, 1000).Dispose()
        break
    } catch {
        if (-not (Get-Process -Name lichii, eden-cli -ErrorAction SilentlyContinue)) {
            Write-Host 'Lichii closed before the game started.' -ForegroundColor Red
            exit 1
        }
        Start-Sleep -Seconds 2
    }
}
Start-Sleep -Seconds 3 # the boot lines reach the log file
& $check ''
Write-Host 'Game settings are right for this run.' -ForegroundColor Green

if ($run.free_play) {
    # Free play: no spot, the session log is the measurement (log-summary.ps1). With
    # sample_new_area, the GPU threads are sampled while the game builds new shaders.
    $dir = Join-Path $OutDir ("{0}-{1}" -f $run.label, (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    if ($run.sample_new_area) {
        Read-Host 'Press Enter right before Link walks into the new area'
        $client = [LichiiPerf.DebugClient]::new('127.0.0.1', 6545, 3000)
        try {
            Write-Host 'Sampling the GPU threads for 30 s: walk in and look around...'
            $params = @{ threads = 'GPU,GPUBack,VkPipelineBuilder'; seconds = 30; max_chars = 600000 } | ConvertTo-Json -Compress
            $reply = $client.Call('sample', $params) | ConvertFrom-Json -Depth 8
            Set-Content -Path (Join-Path $dir 'samples.txt') -Value $reply.result.report
        } finally {
            $client.Dispose()
        }
        Write-Host 'Sampled.' -ForegroundColor Green
    }
    $playStart = Get-Date
    Read-Host ("Play for about {0} minutes, then press Enter here (keep Lichii open)" -f $run.free_play)
    # An Enter pressed right away would end the run before it measured anything.
    while (((Get-Date) - $playStart).TotalMinutes -lt 0.8 * $run.free_play) {
        Read-Host ("Only {0:N0} of {1} minutes played: keep playing, then press Enter again" -f
                   ((Get-Date) - $playStart).TotalMinutes, $run.free_play)
    }
    $log = Join-Path $env:APPDATA 'eden\log\eden_log.txt'
    $stream = [IO.FileStream]::new($log, 'Open', 'Read', 'ReadWrite, Delete')
    $target = [IO.File]::Create((Join-Path $dir 'eden_log.txt'))
    try { $stream.CopyTo($target) } finally { $target.Dispose(); $stream.Dispose() }
    & (Join-Path $PSScriptRoot 'log-summary.ps1') -Log (Join-Path $dir 'eden_log.txt') |
        Set-Content (Join-Path $dir 'summary.md')
    Get-Content (Join-Path $dir 'summary.md')
    if ($run.after) {
        Write-Host "After this run: $($run.after)" -ForegroundColor Yellow
    }
    Write-Host 'Done: close Lichii now.' -ForegroundColor Green
    exit 0
}

$benchArgs = @{ Label = $run.label; Seconds = $Seconds; OutDir = $OutDir; Sample = [bool]$run.sample
                Expect = $expect; ReferenceView = $reference; AB = $ab }
if ($run.moving) {
    # Scenes change while you play: many short blocks average them out between the two arms.
    $benchArgs.Blocks = 16
    $benchArgs.BlockSeconds = 10
    Read-Host 'Press Enter, switch back to the game and keep playing normally until the green Done'
    Start-Sleep -Seconds 5
} else {
    Read-Host 'Press Enter here once Link stands at the spot'
    & $check $reference
    Write-Host "Switch back to the game window and leave the controller alone. Measuring starts in $Settle s."
    Start-Sleep -Seconds $Settle
}
try {
    & $bench @benchArgs
} catch {
    Write-Host "Run $($run.label) failed: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'Close Lichii and run round.ps1 again to repeat it.' -ForegroundColor Red
    exit 1
}
if ($run.after) {
    Write-Host "After this run: $($run.after)" -ForegroundColor Yellow
}
Write-Host 'Done: close Lichii now.' -ForegroundColor Green
