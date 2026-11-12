# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

<#
.SYNOPSIS
Summarizes what an giuray_log.txt already records about performance: frame times per 600
presents, runtime pipeline builds and the draws they held, pipeline evictions, and guest CPU
reads of GPU-written memory (reactive flushing / read traps).

.EXAMPLE
pwsh -File tools/nx2/perf/log-summary.ps1 "$env:APPDATA\eden\log\giuray_log.txt" -From 600 -To 1800
#>
param(
    [Parameter(Mandatory)] [string] $Log,
    [double] $From = 0,
    [double] $To = [double]::MaxValue
)

$ErrorActionPreference = 'Stop'
$time = '^\[\s*([0-9.]+)\]'
$frames = [System.Collections.Generic.List[object]]::new()
$pipelines = [System.Collections.Generic.List[object]]::new()
$slowBuilds = [System.Collections.Generic.List[double]]::new()
$evicted = 0
$reads = [System.Collections.Generic.List[object]]::new()

# The running emulator keeps the log open for writing.
$stream = [System.IO.FileStream]::new((Resolve-Path $Log), 'Open', 'Read', 'ReadWrite, Delete')
$reader = [System.IO.StreamReader]::new($stream)
while ($null -ne ($line = $reader.ReadLine())) {
    if ($line -notmatch $time) { continue }
    $t = [double]$Matches[1]
    if ($t -lt $From -or $t -gt $To) { continue }
    if ($line -match 'last 600 presents: frame (\d+) us \(max (\d+), (\d+) over 100 ms\), blocked on the GPU fence (\d+) us') {
        $frames.Add([pscustomobject]@{ t = $t; frame_us = [double]$Matches[1]; max_us = [double]$Matches[2]
                                       hitches = [int]$Matches[3]; fence_us = [double]$Matches[4] })
    } elseif ($line -match 'Runtime pipelines in the last 10 s: (\d+) built, ([0-9.]+) ms on the GPU thread .* ([0-9.]+) ms of draws held') {
        $entry = [pscustomobject]@{ t = $t; built = [int]$Matches[1]; gpu_ms = [double]$Matches[2]; held_ms = [double]$Matches[3]
                                    rebuilt = 0; rebuilt_ms = 0.0 }
        # Newer builds also report pipelines rebuilt after an eviction.
        if ($line -match '(\d+) rebuilt after eviction \(([0-9.]+) ms') {
            $entry.rebuilt = [int]$Matches[1]
            $entry.rebuilt_ms = [double]$Matches[2]
        }
        $entry | Add-Member skipped 0
        if ($line -match '(\d+) Switch 2 draws skipped while building') {
            $entry.skipped = [int]$Matches[1]
        }
        $pipelines.Add($entry)
    } elseif ($line -match 'pipeline built on the GPU thread in ([0-9.]+) ms') {
        $slowBuilds.Add([double]$Matches[1])
    } elseif ($line -match 'Evicted (\d+) graphics pipelines') {
        $evicted += [int]$Matches[1]
    } elseif ($line -match 'CPU read of GPU-written memory .*\((\d+) so far\)') {
        $reads.Add([pscustomobject]@{ t = $t; n = [double]$Matches[1] })
    }
}
$reader.Dispose()

if ($frames.Count -eq 0) { throw 'no "last 600 presents" lines in that range' }
# Each line averages the 600 presents before it, so weight by frame count (all equal) and
# derive fps from the mean frame time.
$meanFrame = ($frames | Measure-Object frame_us -Average).Average / 1000
$sorted = $frames | Sort-Object frame_us
$worst = $sorted | Select-Object -Last ([math]::Max(1, [math]::Ceiling($frames.Count * 0.1)))
"Log: $Log"
"Window: {0:N0}-{1:N0} s ({2} blocks of 600 presents)" -f $frames[0].t, $frames[-1].t, $frames.Count
"Frame time: mean {0:N1} ms ({1:N1} fps), best block {2:N1} ms, worst block {3:N1} ms, worst 10% of blocks {4:N1} ms" -f `
    $meanFrame, (1000 / $meanFrame), ($sorted[0].frame_us / 1000), ($sorted[-1].frame_us / 1000),
    (($worst | Measure-Object frame_us -Average).Average / 1000)
"Frames over 100 ms: {0} ({1:N2} per 600); longest frame {2:N0} ms; blocked on the present fence: mean {3:N0} us" -f `
    ($frames | Measure-Object hitches -Sum).Sum, ($frames | Measure-Object hitches -Average).Average,
    (($frames | Measure-Object max_us -Maximum).Maximum / 1000), ($frames | Measure-Object fence_us -Average).Average
if ($pipelines.Count -gt 0) {
    $built = ($pipelines | Measure-Object built -Sum).Sum
    $gpu = ($pipelines | Measure-Object gpu_ms -Sum).Sum
    $held = ($pipelines | Measure-Object held_ms -Sum).Sum
    $pipeSpan = [math]::Max(1, $pipelines[-1].t - $pipelines[0].t + 10)
    "Runtime pipelines: {0} built; {1:N1} s on the GPU thread ({2:N1}% of the window); {3:N1} s of draws held ({4:N1}%)" -f `
        $built, ($gpu / 1000), ($gpu / 10 / $pipeSpan), ($held / 1000), ($held / 10 / $pipeSpan)
    $rebuilt = ($pipelines | Measure-Object rebuilt -Sum).Sum
    if ($rebuilt -gt 0) {
        "  rebuilt after eviction: {0} ({1:N1} s on the GPU thread)" -f $rebuilt,
            (($pipelines | Measure-Object rebuilt_ms -Sum).Sum / 1000)
    }
    $skipped = ($pipelines | Measure-Object skipped -Sum).Sum
    if ($skipped -gt 0) {
        "  Switch 2 draws skipped while their pipeline built: $skipped"
    }
    $top = $pipelines | Sort-Object gpu_ms -Descending | Select-Object -First 3
    "  worst 10 s windows (GPU-thread ms): " + (($top | ForEach-Object { '{0:N0} ms at {1:N0} s' -f $_.gpu_ms, $_.t }) -join ', ')
    $held = $pipelines | Sort-Object held_ms -Descending | Select-Object -First 3
    "  worst 10 s windows (draws held waiting for the driver): " + (($held | ForEach-Object { '{0:N0} ms at {1:N0} s' -f $_.held_ms, $_.t }) -join ', ')
}
if ($slowBuilds.Count -gt 0) {
    $s = $slowBuilds | Sort-Object
    "Logged GPU-thread builds (>2 ms, first 400 and all >100 ms): {0}, median {1:N1} ms, p90 {2:N1} ms, max {3:N0} ms" -f `
        $s.Count, $s[[int]($s.Count * 0.5)], $s[[int]([math]::Min($s.Count - 1, $s.Count * 0.9))], $s[-1]
}
"Pipelines evicted: $evicted"
if ($reads.Count -gt 1) {
    "CPU reads of GPU-written memory: {0:N0} in {1:N0} s ({2:N1}/s)" -f ($reads[-1].n - $reads[0].n),
        ($reads[-1].t - $reads[0].t), (($reads[-1].n - $reads[0].n) / [math]::Max(1, $reads[-1].t - $reads[0].t))
}
