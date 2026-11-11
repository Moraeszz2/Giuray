# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

<#
.SYNOPSIS
Starts a Lichii package for a benchmark run with the debug server on and one A/B variant's
environment, keeping the previous session's log.

.DESCRIPTION
Every variant listens on 127.0.0.1:6545 so bench.ps1 can measure it. Variants only set
environment switches the emulator already reads at start; settings that live in the game's
configuration (reactive flushing, GPU accuracy) are changed in the emulator's own UI instead.
Refuses to start while another lichii.exe or eden-cli.exe runs, so runs never overlap.

.EXAMPLE
pwsh -File tools/nx2/perf/launch.ps1 -Exe "C:\Users\me\Downloads\lichii\lichii.exe" -Variant base
pwsh -File tools/nx2/perf/launch.ps1 -Exe ... -Variant nolayers -GpuProfile
pwsh -File tools/nx2/perf/launch.ps1 -Exe ... -Variant base -ExtraEnv EDEN_NX2_IDLE_SPIN_US=25
#>
param(
    [Parameter(Mandatory)] [string] $Exe,
    [ValidateSet('base', 'nolayers', 'spin0', 'spin25', 'spin50', 'spin200', 'noevict', 'poll250', 'poll1000', 'tcpressure', 'skipnative', 'play', 'budget', 'preflush', 'nostale', 'trace')] [string] $Variant = 'base',
    [string[]] $ExtraEnv = @(),
    [switch] $GpuProfile,
    [string] $Game = '',
    [string] $OutDir = (Join-Path $env:USERPROFILE 'lichii-perf\results')
)

$ErrorActionPreference = 'Stop'
if (Get-Process -Name lichii, eden-cli -ErrorAction SilentlyContinue) {
    throw 'Lichii is already running: close it first so runs do not overlap'
}
if (-not (Test-Path $Exe)) { throw "not found: $Exe" }

$variants = @{
    base     = @()
    # Implicit Vulkan layers of capture tools load into every Vulkan program and wrap its calls.
    nolayers = @('DISABLE_VULKAN_MEDAL_CAPTURE_V2=1', 'DISABLE_VULKAN_MEDAL_OBS_CAPTURE=1',
                 'DISABLE_VULKAN_OBS_CAPTURE=1')
    # Guest cores poll this long before blocking when idle (physical_core.cpp, default 100 us).
    spin0    = @('EDEN_NX2_IDLE_SPIN_US=0')
    spin25   = @('EDEN_NX2_IDLE_SPIN_US=25')
    spin50   = @('EDEN_NX2_IDLE_SPIN_US=50')
    spin200  = @('EDEN_NX2_IDLE_SPIN_US=200')
    # Local builds only: graphics pipelines are never evicted (default after 9000 unused frames).
    noevict  = @('EDEN_NX2_PIPELINE_EVICT_FRAMES=0')
    # Local builds only: syncpoint page poll interval (default 500 us, 2,000 timer wake-ups a second).
    poll250  = @('EDEN_NX2_SYNCPT_POLL_US=250')
    poll1000 = @('EDEN_NX2_SYNCPT_POLL_US=1000')
    # The texture cache thresholds of a session where other programs left ~3.4 GB of VRAM (the
    # collector evicts and downloads ~3 images a frame), whatever VRAM is free now.
    tcpressure = @('EDEN_NX2_TC_EXPECTED_MIB=1600', 'EDEN_NX2_TC_CRITICAL_MIB=2370')
    # Local builds only: Switch 2 draws skip pipelines the driver is still compiling (may leave
    # an effect missing) instead of stopping the game until it is done.
    skipnative = @('EDEN_NX2_SKIP_UNBUILT_NATIVE=1')
    # Everyday play, not a benchmark: the experiments that measured a gain so far, together.
    play     = @('EDEN_NX2_ASYNC_DISCARD=1')
    # Local builds only: play, and new pipelines past 8 ms of translation a frame wait for the next
    # frame (their draws are skipped) instead of stopping it.
    budget   = @('EDEN_NX2_ASYNC_DISCARD=1', 'EDEN_NX2_PIPELINE_BUDGET=1')
    # Round 2 (open PRs #259 and #463): play, with CPU reads of GPU-written pages preflushed at
    # fences; or with stale guest memory uploaded again. Both are also debug server switches.
    preflush = @('EDEN_NX2_ASYNC_DISCARD=1', 'EDEN_NX2_PREFLUSH=1')
    nostale  = @('EDEN_NX2_ASYNC_DISCARD=1', 'EDEN_NX2_TC_SKIP_STALE=0')
    # Diagnostics, not a speed comparison: GPU time per submission and per draw/dispatch with
    # shader hashes (once a second), GPU-written command list reads and waits, texture cache
    # collector counts.
    trace    = @('EDEN_DKB_FRAME_TRACE=1', 'EDEN_NX2_CMDPAGE_DIAG=1')
}
# Every run logs device memory against its budget and the texture cache collector's work every
# 600 frames (EDEN_NX2_TC_LOG, logging only): VRAM left by other programs changes the results.
# pwsh -File passes -ExtraEnv A=1,B=2 as one string: split it.
$ExtraEnv = @($ExtraEnv | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$settings = @('EDEN_NX2_DEBUG_SERVER=127.0.0.1:6545', 'EDEN_NX2_TC_LOG=1') + $variants[$Variant] + $ExtraEnv
if ($GpuProfile) {
    $settings += 'EDEN_NX2_GPU_PROFILE=1'
}

$log = Join-Path $env:APPDATA 'eden\log\eden_log.txt'
if (Test-Path $log) {
    $keep = Join-Path $OutDir 'logs'
    New-Item -ItemType Directory -Force -Path $keep | Out-Null
    Copy-Item $log (Join-Path $keep ("eden_log-{0}.txt" -f (Get-Item $log).LastWriteTime.ToString('yyyyMMdd-HHmmss')))
}

foreach ($setting in $settings) {
    $name, $value = $setting.Split('=', 2)
    Set-Item -Path "env:$name" -Value $value
}
$arguments = if ($Game) { @('-g', "`"$Game`"") } else { @() }
$process = Start-Process -FilePath $Exe -ArgumentList $arguments -WorkingDirectory (Split-Path $Exe) -PassThru
Write-Host "Started $(Split-Path -Leaf $Exe) (pid $($process.Id)), variant '$Variant': $($settings -join ' ')"
# round.ps1 reuses an open Lichii only when it runs the variant the next run needs.
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Set-Content -Path (Join-Path $OutDir 'running.txt') -Value "$($process.Id) $Variant"
Write-Host 'Load the save and go to the benchmark spot.'
