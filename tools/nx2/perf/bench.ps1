# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

<#
.SYNOPSIS
Measures a running Giuray session through its debug server: frame times (average fps, 1% and
0.1% lows, percentiles), CPU use per host thread, host GPU use and VRAM, process memory, the
emulator's own per-600-present and pipeline summaries, and optionally where the busy threads
spend their time (the in-process sampler).

.DESCRIPTION
Launch the emulator with launch.ps1 (it sets GIURAY_NX2_DEBUG_SERVER), go to the benchmark spot,
then run this script. It never starts, stops or pauses the emulator, and sends no input unless
-Route is given. Results go to <OutDir>\<label>-<time>\ (summary.md, result.json, frames.csv,
gpu.csv, samples-*.txt).

The frame times come from polling the present counter every -PollMs milliseconds, so each is
accurate to about one poll interval. Polling costs well under 1% of one host thread.

.EXAMPLE
pwsh -File tools/nx2/perf/bench.ps1 -Label base-msvc -Seconds 60 -Sample
#>
param(
    [Parameter(Mandatory)] [string] $Label,
    [double] $Seconds = 60,
    [int] $PollMs = 4,
    [string] $Server = '127.0.0.1:6545',
    [switch] $Sample,
    [double] $SampleSeconds = 10,
    [string] $OutDir = (Join-Path $env:USERPROFILE 'giuray-work\results'),
    [string] $Route = '',
    [string[]] $ProcessName = @('giuray', 'giuray-cli'),
    # Game settings the run needs, e.g. @{ reactive_flushing = $false }: checked against what the
    # emulator logged when the game booted. -CheckOnly stops after the checks.
    [hashtable] $Expect = @{},
    # A view.png of the benchmark spot: the run is refused when the game shows something else.
    [string] $ReferenceView = '',
    [double] $MinSimilarity = 0.9,
    [switch] $CheckOnly,
    # In-session A/B of an experiment switch (debug server `experiment`): -Blocks blocks of
    # -BlockSeconds in the order off, on, on, off, ..., each after -BlockSettle s. Replaces the
    # single -Seconds window.
    [string] $AB = '',
    [int] $Blocks = 8,
    [double] $BlockSeconds = 15,
    [double] $BlockSettle = 3
)

$ErrorActionPreference = 'Stop'
if (-not ('GiurayPerf.FramePoller' -as [type])) {
    Add-Type -Path (Join-Path $PSScriptRoot 'GiurayPerf.cs')
}

function Invoke-Nx2([GiurayPerf.DebugClient] $client, [string] $method, $params = @{}) {
    $line = $client.Call($method, ($params | ConvertTo-Json -Compress -Depth 8))
    $reply = $line | ConvertFrom-Json -Depth 32
    if ($null -ne $reply.error) { throw "debug server: $method failed: $($reply.error)" }
    return $reply.result
}

function Read-SharedLines([string] $path) {
    # The running emulator keeps its log open for writing.
    $reader = [IO.StreamReader]::new([IO.FileStream]::new($path, 'Open', 'Read', 'ReadWrite, Delete'))
    try { return $reader.ReadToEnd() -split "`r?`n" } finally { $reader.Dispose() }
}

# 32x18 grey thumbnails compared by correlation: ~0.99 for the same spot from run to run (crowd
# and Link's idle pose move), ~0.3-0.4 for another part of the same town.
function Get-ViewSimilarity([string] $a, [string] $b) {
    Add-Type -AssemblyName System.Drawing
    $thumbs = foreach ($path in $a, $b) {
        $bitmap = [System.Drawing.Bitmap]::new($path)
        $small = [System.Drawing.Bitmap]::new($bitmap, 32, 18)
        $values = [double[]]::new(32 * 18)
        for ($i = 0; $i -lt $values.Length; ++$i) {
            $c = $small.GetPixel($i % 32, [math]::Floor($i / 32))
            $values[$i] = 0.299 * $c.R + 0.587 * $c.G + 0.114 * $c.B
        }
        $small.Dispose(); $bitmap.Dispose()
        , $values
    }
    $x, $y = $thumbs
    $mx = ($x | Measure-Object -Average).Average
    $my = ($y | Measure-Object -Average).Average
    $sxy = 0.0; $sxx = 0.0; $syy = 0.0
    for ($i = 0; $i -lt $x.Length; ++$i) {
        $sxy += ($x[$i] - $mx) * ($y[$i] - $my)
        $sxx += ($x[$i] - $mx) * ($x[$i] - $mx)
        $syy += ($y[$i] - $my) * ($y[$i] - $my)
    }
    return $sxy / [math]::Sqrt($sxx * $syy)
}

$hostName, $port = $Server.Split(':')
$client = [GiurayPerf.DebugClient]::new($hostName, [int]$port, 3000)
$smiProcess = $null
try {
    $status = Invoke-Nx2 $client 'status' @{ fps_window_ms = 1000 }
    if ($status.paused) { throw 'the emulator is paused' }
    Write-Host ("{0} [{1}] build {2} ({3}), {4:N1} fps now" -f $status.title, $status.program_id,
                $status.build, $status.scm_rev, $status.fps)

    $process = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $process) { throw "no $($ProcessName -join ' or ') process found" }

    # Settings changed in the UI between runs, as logged when the game booted (the newest line).
    # Read from the log file: the debug server's ring starts after the boot lines when the game
    # is opened right away.
    $logFiles = @(Join-Path $env:APPDATA 'eden\log\giuray_log.txt')
    if ($process.Path) {
        $logFiles = @(Join-Path (Split-Path $process.Path) 'user\log\giuray_log.txt') + $logFiles
    }
    $logFile = $logFiles | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    $bootLines = if ($logFile) { Read-SharedLines $logFile } else { (Invoke-Nx2 $client 'log_tail' @{ limit = 5000 }).lines }
    $game = [ordered]@{}
    $line = $bootLines | Where-Object { $_ -match 'use_reactive_flushing: (true|false)' } | Select-Object -Last 1
    if ($line -match 'use_reactive_flushing: (true|false)') { $game.reactive_flushing = $Matches[1] -eq 'true' }
    $line = $bootLines | Where-Object { $_.Contains('"Max Resolution (built-in)"') } | Select-Object -Last 1
    if ($line) { $game.max_resolution = $line -match 'Applying built-in patch' }
    foreach ($key in $Expect.Keys) {
        if (-not $game.Contains($key)) {
            Write-Host "Cannot tell $key from the log; not checked."
        } elseif ($game[$key] -ne [bool]$Expect[$key]) {
            throw (("{0} was {1} when the game booted, this run needs {2}: change it, restart the " +
                    "game and run again") -f $key, $game[$key], [bool]$Expect[$key])
        }
    }
    $similarity = $null
    if ($ReferenceView -and (Test-Path -LiteralPath $ReferenceView)) {
        $shot = Invoke-Nx2 $client 'screenshot' @{ height = 360; save = $true; inline = $false }
        if (-not $shot.path -or -not (Test-Path -LiteralPath $shot.path)) {
            Write-Host 'No screenshot: the view is not checked.'
        } else {
            $similarity = Get-ViewSimilarity $ReferenceView $shot.path
        }
        if ($null -ne $similarity -and $similarity -lt $MinSimilarity) {
            throw ("the game shows another view than the benchmark spot (similarity {0:N2}, needs " +
                   "{1:N2}): go back to the usual spot and run again") -f $similarity, $MinSimilarity
        }
        if ($null -ne $similarity) {
            Write-Host ("Same view as the reference ({0:N2})." -f $similarity)
        }
    }
    if ($AB) {
        $switches = try { (Invoke-Nx2 $client 'experiment').switches } catch { $null }
        if ($null -eq $switches -or $null -eq $switches.PSObject.Properties[$AB]) {
            throw "this build has no experiment switch '$AB' (debug server method 'experiment')"
        }
    }
    if ($CheckOnly) {
        return
    }

    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $dir = Join-Path $OutDir "$Label-$stamp"
    New-Item -ItemType Directory -Force -Path $dir | Out-Null

    $logStart = (Invoke-Nx2 $client 'log_tail' @{ limit = 1 }).next

    # Host GPU use and VRAM, once a second, for the measured window only.
    $gpuCsv = Join-Path $dir 'gpu.csv'
    $smi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if ($smi) {
        $smiProcess = Start-Process -FilePath $smi.Source -PassThru -WindowStyle Hidden -RedirectStandardOutput $gpuCsv -ArgumentList @(
                '--query-gpu=utilization.gpu,memory.used,memory.total,clocks.gr,power.draw',
                '--format=csv,noheader,nounits', '-lms', '1000')
    }

    if ($Route) {
        # Optional scripted input started with the measurement, e.g. a fixed walk; see README.md.
        $steps = Get-Content -Raw $Route | ConvertFrom-Json -Depth 8
        $routeJob = Start-ThreadJob -ArgumentList $Server, $steps, $PSScriptRoot -ScriptBlock {
            param($server, $steps, $root)
            if (-not ('GiurayPerf.FramePoller' -as [type])) { Add-Type -Path (Join-Path $root 'GiurayPerf.cs') }
            $h, $p = $server.Split(':')
            $c = [GiurayPerf.DebugClient]::new($h, [int]$p, 3000)
            try { $c.Call('input_sequence', (@{ steps = $steps; timeout_s = 30 } | ConvertTo-Json -Compress -Depth 8)) }
            finally { $c.Dispose() }
        }
    }

    # What the run looked at, to check that every run measured the same view.
    try {
        $shot = Invoke-Nx2 $client 'screenshot' @{ height = 360; save = $true; inline = $false }
        Copy-Item -LiteralPath $shot.path -Destination (Join-Path $dir 'view.png')
    } catch {
        Write-Host "No screenshot: $($_.Exception.Message)"
    }

    $threadsBefore = [GiurayPerf.Threads]::Snapshot($process.Id)
    $cpuBefore = $process.TotalProcessorTime.TotalSeconds
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $abBlocks = @()
    try {
        if ($AB) {
            $original = [bool](Invoke-Nx2 $client 'experiment').switches.$AB
            $all = [System.Collections.Generic.List[double]]::new()
            Write-Host ("A/B of {0}: {1} blocks of {2} s..." -f $AB, $Blocks, $BlockSeconds)
            try {
                for ($i = 0; $i -lt $Blocks; ++$i) {
                    $on = @($false, $true, $true, $false)[$i % 4]
                    Invoke-Nx2 $client 'experiment' @{ name = $AB; value = $on } | Out-Null
                    Start-Sleep -Seconds $BlockSettle
                    $block = [GiurayPerf.FramePoller]::Measure($client, $BlockSeconds, $PollMs)
                    $all.AddRange($block.FrameMs)
                    $abBlocks += [pscustomobject]@{ block = $i + 1; on = $on; fps = $block.AverageFps
                                                    one_percent_low_fps = $block.OnePercentLowFps
                                                    frames = $block.Frames; frame_ms = $block.FrameMs }
                    Write-Host ("  block {0} {1}: {2:N2} fps" -f ($i + 1), $(if ($on) { 'on ' } else { 'off' }), $block.AverageFps)
                }
            } finally {
                Invoke-Nx2 $client 'experiment' @{ name = $AB; value = $original } | Out-Null
            }
            $frames = [GiurayPerf.FramePoller]::Summarize($all, $PollMs)
        } else {
            Write-Host "Measuring $Seconds s (stay at the spot)..."
            $frames = [GiurayPerf.FramePoller]::Measure($client, $Seconds, $PollMs)
        }
    } catch {
        $reason = if ($_.Exception.InnerException) { $_.Exception.InnerException.Message } else { $_.Exception.Message }
        $message = "the measurement stopped after {0:N0} s ({1}); did the emulator close? Nothing was measured." -f $clock.Elapsed.TotalSeconds, $reason
        Set-Content -Path (Join-Path $dir 'error.txt') -Value $message
        throw $message
    }
    $elapsed = $clock.Elapsed.TotalSeconds
    $process.Refresh()
    $cpuAfter = $process.TotalProcessorTime.TotalSeconds
    $threadsAfter = [GiurayPerf.Threads]::Snapshot($process.Id)
    if ($smiProcess) { Stop-Process -Id $smiProcess.Id -Force -ErrorAction SilentlyContinue; $smiProcess = $null }
    if ($Route) { Receive-Job -Wait -AutoRemoveJob $routeJob | Out-Null }

    $busy = [GiurayPerf.Threads]::Busy($threadsBefore, $threadsAfter, $elapsed)
    $logical = [Environment]::ProcessorCount
    $processCpu = ($cpuAfter - $cpuBefore) / $elapsed / $logical * 100

    $gpu = @()
    if (Test-Path $gpuCsv) {
        $gpu = Get-Content $gpuCsv | Where-Object { $_ -match '^\s*\d' } | ForEach-Object {
            $f = $_.Split(',').Trim()
            [pscustomobject]@{ util = [double]$f[0]; mem_used = [double]$f[1]; mem_total = [double]$f[2]
                               clock = [double]$f[3]; power = [double]($f[4] -replace '[^\d.]', '') }
        }
    }

    $logLines = @()
    $after = $logStart
    do {
        $page = Invoke-Nx2 $client 'log_tail' @{ after = $after; limit = 5000 }
        $logLines += $page.lines
        $after = $page.next
    } while ($page.more)
    $interesting = $logLines | Where-Object {
        $_ -match 'last 600 presents|Runtime pipelines in the last|built on the GPU thread|Evicted \d+ graphics|CPU read of GPU-written| per second:|ASTC GPU decodes|Memory margins|DKB gpu_|DKB draws|cmdpage diag|Texture cache'
    }
    $logLines | Set-Content (Join-Path $dir 'log-full.txt')

    $samples = @()
    if ($Sample) {
        # One request for every thread: builds before the sampler's DbgHelp fix name functions
        # only in a session's first report.
        $set = 'CPUCore_*,GPU,GPUBack,VulkanWorker,VulkanPresent,GPUFencingThread,VkPipelineBuilder'
        Write-Host "Sampling the busy threads for $SampleSeconds s..."
        $result = Invoke-Nx2 $client 'sample' @{ threads = $set; seconds = $SampleSeconds; max_chars = 600000 }
        Set-Content -Path (Join-Path $dir 'samples.txt') -Value $result.report
        $samples += [pscustomobject]@{ threads = $set; file = 'samples.txt'; emulator_file = $result.file }
    }

    # A/B: each pair of neighbouring blocks (off/on, on/off, ...) gives one difference; their
    # spread gives a 95% interval (Student's t), so drift during the run cancels out.
    $abResult = $null
    if ($AB) {
        $arm = {
            param([bool] $on)
            $ms = [System.Collections.Generic.List[double]]::new()
            $abBlocks | Where-Object { $_.on -eq $on } | ForEach-Object { $ms.AddRange([double[]]$_.frame_ms) }
            [GiurayPerf.FramePoller]::Summarize($ms, $PollMs)
        }
        $offStats = & $arm $false
        $onStats = & $arm $true
        $diffs = @(for ($i = 0; $i + 1 -lt $abBlocks.Count; $i += 2) {
            $pair = $abBlocks[$i], $abBlocks[$i + 1]
            $offFps = ($pair | Where-Object { -not $_.on }).fps
            $onFps = ($pair | Where-Object { $_.on }).fps
            100 * ($onFps - $offFps) / $offFps
        })
        $mean = ($diffs | Measure-Object -Average).Average
        $ci = $null
        if ($diffs.Count -ge 2) {
            $sd = [math]::Sqrt(($diffs | ForEach-Object { ($_ - $mean) * ($_ - $mean) } | Measure-Object -Sum).Sum / ($diffs.Count - 1))
            $t = @{ 1 = 12.71; 2 = 4.30; 3 = 3.18; 4 = 2.78; 5 = 2.57; 6 = 2.45; 7 = 2.36; 8 = 2.31 }[$diffs.Count - 1]
            $ci = $(if ($t) { $t } else { 2.0 }) * $sd / [math]::Sqrt($diffs.Count)
        }
        $abResult = [ordered]@{
            switch = $AB; block_seconds = $BlockSeconds
            blocks = @($abBlocks | ForEach-Object { [ordered]@{ block = $_.block; on = $_.on; fps = $_.fps; one_percent_low_fps = $_.one_percent_low_fps } })
            off = [ordered]@{ fps = $offStats.AverageFps; one_percent_low_fps = $offStats.OnePercentLowFps; p99_frame_ms = $offStats.P99FrameMs }
            on = [ordered]@{ fps = $onStats.AverageFps; one_percent_low_fps = $onStats.OnePercentLowFps; p99_frame_ms = $onStats.P99FrameMs }
            fps_change_percent = $mean; ci95_percent = $ci
        }
    }

    $process.Refresh()
    $result = [ordered]@{
        label = $Label; time = $stamp; title = $status.title; program_id = $status.program_id
        build = $status.build; scm_rev = $status.scm_rev; game_settings = $game; view_similarity = $similarity
        seconds = $elapsed; poll_ms = $frames.PollIntervalMs
        frames = $frames.Frames; avg_fps = $frames.AverageFps; one_percent_low_fps = $frames.OnePercentLowFps
        point_one_percent_low_fps = $frames.PointOnePercentLowFps; avg_frame_ms = $frames.AverageFrameMs
        median_frame_ms = $frames.MedianFrameMs; p95_frame_ms = $frames.P95FrameMs; p99_frame_ms = $frames.P99FrameMs
        max_frame_ms = $frames.MaxFrameMs; frames_over_100ms = $frames.FramesOver100Ms
        process_cpu_percent_of_machine = $processCpu
        gpu_util_avg = ($gpu | Measure-Object util -Average).Average
        vram_used_mib_max = ($gpu | Measure-Object mem_used -Maximum).Maximum
        vram_total_mib = ($gpu | Select-Object -First 1).mem_total
        private_mib = [math]::Round($process.PrivateMemorySize64 / 1MB)
        working_set_mib = [math]::Round($process.WorkingSet64 / 1MB)
        threads = @($busy | Select-Object -First 40 | ForEach-Object { [ordered]@{ name = $_.Key; cpu_percent = [math]::Round($_.Value, 1) } })
        samples = $samples
        ab = $abResult
    }
    $result | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $dir 'result.json')
    $invariant = [Globalization.CultureInfo]::InvariantCulture
    $frames.FrameMs | ForEach-Object { $_.ToString('F3', $invariant) } | Set-Content (Join-Path $dir 'frames.csv')
    $interesting | Set-Content (Join-Path $dir 'log.txt')

    $grouped = $busy | Group-Object { $_.Key -replace '\d+$', '' } | ForEach-Object {
        [pscustomobject]@{ name = $_.Name; threads = $_.Count; cpu = ($_.Group | Measure-Object Value -Sum).Sum }
    } | Sort-Object cpu -Descending

    $md = @()
    $md += "# $Label ($stamp)"
    $md += ''
    $md += "$($status.title) [$($status.program_id)], build $($status.build) ($($status.scm_rev))"
    $md += ''
    $md += '| metric | value |'
    $md += '|---|---|'
    $md += ('| frames / seconds | {0} / {1:N1} |' -f $frames.Frames, $elapsed)
    $md += ('| average fps | {0:N2} |' -f $frames.AverageFps)
    $md += ('| 1% low fps / 0.1% low fps | {0:N2} / {1:N2} |' -f $frames.OnePercentLowFps, $frames.PointOnePercentLowFps)
    $md += ('| frame time avg / median / p95 / p99 / max (ms) | {0:N1} / {1:N1} / {2:N1} / {3:N1} / {4:N1} |' -f `
            $frames.AverageFrameMs, $frames.MedianFrameMs, $frames.P95FrameMs, $frames.P99FrameMs, $frames.MaxFrameMs)
    $md += ('| frames over 100 ms | {0} |' -f $frames.FramesOver100Ms)
    $md += ('| game settings at boot | {0} |' -f (($game.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ', '))
    $md += ('| view similarity to the reference | {0} |' -f $(if ($null -ne $similarity) { '{0:N3}' -f $similarity } else { 'not checked' }))
    $md += ('| process CPU (% of all {0} logical CPUs) | {1:N1} |' -f $logical, $processCpu)
    $md += ('| host GPU busy avg (%) | {0:N1} |' -f $result.gpu_util_avg)
    $md += ('| VRAM used max / total (MiB) | {0} / {1} |' -f $result.vram_used_mib_max, $result.vram_total_mib)
    $md += ('| private / working set (MiB) | {0} / {1} |' -f $result.private_mib, $result.working_set_mib)
    $md += ('| poll interval (ms) | {0:N2} |' -f $frames.PollIntervalMs)
    $md += ''
    if ($abResult) {
        $md += "## A/B of $AB ($Blocks blocks of $BlockSeconds s)"
        $md += ''
        $md += '| block | switch | fps | 1% low fps |'
        $md += '|---|---|---|---|'
        $abBlocks | ForEach-Object { $md += ('| {0} | {1} | {2:N2} | {3:N2} |' -f $_.block, $(if ($_.on) { 'on' } else { 'off' }), $_.fps, $_.one_percent_low_fps) }
        $md += ''
        $md += ('off: {0:N2} fps, 1% low {1:N2}, p99 {2:N1} ms; on: {3:N2} fps, 1% low {4:N2}, p99 {5:N1} ms' -f `
                $offStats.AverageFps, $offStats.OnePercentLowFps, $offStats.P99FrameMs,
                $onStats.AverageFps, $onStats.OnePercentLowFps, $onStats.P99FrameMs)
        $md += ('fps change with the switch on: {0:+0.0;-0.0}% (95% interval +/- {1})' -f $mean,
                $(if ($null -ne $ci) { '{0:N1}%' -f $ci } else { 'n/a' }))
        $md += ''
    }
    $md += '## Host threads (% of one logical CPU)'
    $md += ''
    $md += '| thread | cpu % |'
    $md += '|---|---|'
    $busy | Select-Object -First 24 | ForEach-Object { $md += ('| {0} | {1:N1} |' -f $_.Key, $_.Value) }
    $md += ''
    $md += '| thread group | threads | cpu % |'
    $md += '|---|---|---|'
    $grouped | Select-Object -First 12 | ForEach-Object { $md += ('| {0} | {1} | {2:N1} |' -f $_.name, $_.threads, $_.cpu) }
    $md += ''
    $md += '## Emulator log during the window'
    $md += ''
    $md += '```'
    $md += $interesting
    $md += '```'
    $md | Set-Content (Join-Path $dir 'summary.md')

    Write-Host ''
    Write-Host ('{0}: {1:N2} fps avg, 1% low {2:N2} fps, frame {3:N1} ms avg / {4:N1} ms p99, CPU {5:N0}%, GPU {6:N0}%' -f `
                $Label, $frames.AverageFps, $frames.OnePercentLowFps, $frames.AverageFrameMs, $frames.P99FrameMs,
                $processCpu, $result.gpu_util_avg)
    if ($abResult) {
        Write-Host ('A/B {0}: off {1:N2} fps, on {2:N2} fps, change {3:+0.0;-0.0}% (95% interval +/- {4})' -f `
                    $AB, $offStats.AverageFps, $onStats.AverageFps, $mean,
                    $(if ($null -ne $ci) { '{0:N1}%' -f $ci } else { 'n/a' }))
    }
    Write-Host "Results: $dir"
} finally {
    # A failed run must not leave nvidia-smi polling in the background.
    if ($smiProcess) { Stop-Process -Id $smiProcess.Id -Force -ErrorAction SilentlyContinue }
    $client.Dispose()
}
