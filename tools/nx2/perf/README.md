# Performance measurement kit (Windows, PowerShell 7)

Measures a live session through the debug server (`docs/nx2/debug-mcp.md`) without Python or
other tools. Nothing here starts, stops or pauses the emulator on its own: `launch.ps1` starts
it once for a run, and `bench.ps1` only reads from it (and sends input only with `-Route`).

| Script | What it does |
|---|---|
| `launch.ps1` | Starts a package with `EDEN_NX2_DEBUG_SERVER=127.0.0.1:6545` and one A/B variant's switches, keeps the previous `eden_log.txt`, refuses to start a second instance |
| `round.ps1` | One round of an A/B plan: runs `launch.ps1` with the round's package and variant, waits for Enter once you stand at the spot, waits `-Settle` seconds, runs `bench.ps1` with the round's label; `-List` shows which rounds are done |
| `bench.ps1` | Frame times (average fps, 1% and 0.1% lows, median/p95/p99/max), CPU per host thread by name, process CPU, host GPU busy and VRAM (`nvidia-smi`), process memory, the emulator's own summaries logged during the window; `-Sample` adds the in-process sampler on `CPUCore_*`, `GPU`, `GPUBack` and the Vulkan/pipeline threads in one report (`samples.txt`) |
| `log-summary.ps1` | What any `eden_log.txt` already records: frame time per 600 presents, frames over 100 ms, runtime pipeline builds and the draws they held, evictions, CPU reads of GPU-written memory |
| `LichiiPerf.cs` | The helpers the scripts compile with `Add-Type` |

Frame times come from polling the present counter every `-PollMs` (default 4) milliseconds,
so each one is accurate to about a poll interval; several presents between two polls share the
time evenly. 1% low is the average fps of the slowest 1% of frames.

## A/B protocol

1. Close other heavy programs (wallpaper engines, recorders, browsers with video), the same way
   for every run.
2. `pwsh -File tools/nx2/perf/launch.ps1 -Exe <package>\lichii.exe -Variant base`
3. Load the save and go to the spot. Stand still and do not move the camera, so every run renders
   the same view. Wait ~20 s for streaming and pipeline builds to settle.
4. `pwsh -File tools/nx2/perf/bench.ps1 -Label base-1 -Seconds 60` (add `-Sample` once per
   variant for the hotspot breakdown; sampling pauses threads ~1000 times a second, so it runs
   after the timed window).
5. Close the emulator, start the next variant, repeat. Run each variant at least twice in the
   order A, B, A, B; a difference below the spread of the repeats is noise.

Settings stored in the game's configuration (reactive flushing, GPU accuracy, the built-in Max
Resolution add-on) are changed in the emulator's own UI between runs, not through `launch.ps1`.

## Plan file for round.ps1

`round.ps1` reads `%USERPROFILE%\lichii-perf\plan.json` (or `-Plan <file>`). Runs are taken in
order; `before` and `after` are reminders printed around a run, for settings changed in the UI.
`require_closed` lists process names that must not run during that run (round.ps1 refuses to
start it while one does). `ab` names an experiment switch to A/B inside the session (`bench.ps1 -AB`: blocks of 15 s with
the switch off, on, on, off, ..., and the change with a 95% interval from neighbouring blocks);
sessions of one build differed by a third in Ocarina of Time, so a candidate is judged this way.
`moving` measures while you keep playing (no spot or view check; an A/B gets 16 blocks of 10 s
so scene changes average out between the arms).
`free_play` (minutes) skips the spot: you play, and the run keeps the session log and its
log-summary.ps1 report (pipeline builds, rebuilds after eviction, frames over 100 ms); with
`sample_new_area` it samples the GPU threads for 30 s as you enter an area with new shaders.
`expect` (for the whole plan and per run, the run's keys win) names
game settings checked against the emulator's boot log before measuring: `reactive_flushing` and
`max_resolution` (the Ocarina of Time built-in add-on).

```json
{
  "packages": { "a": "C:\\builds\\a\\lichii.exe", "b": "C:\\builds\\b\\lichii.exe" },
  "runs": [
    { "label": "a-1", "package": "a", "variant": "base", "sample": true },
    { "label": "b-1", "package": "b", "variant": "base" },
    { "label": "b-noreactive-1", "package": "b", "variant": "base",
      "before": "Turn off Enable Reactive Flushing.", "after": "Turn it back on." }
  ]
}
```
