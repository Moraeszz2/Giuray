// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Helpers for bench.ps1, compiled at run time with Add-Type (PowerShell 7, no other dependency):
// a client for the in-process debug server (docs/nx2/debug-mcp.md), per-thread CPU time by
// thread name, and a present-count poller that turns the debug server's counter into frame times.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;

namespace GiurayPerf {

public static class Native {
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr OpenThread(uint access, bool inherit, uint threadId);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")]
    public static extern int GetThreadDescription(IntPtr thread, out IntPtr description);
    [DllImport("kernel32.dll")]
    public static extern IntPtr LocalFree(IntPtr memory);
    [DllImport("kernel32.dll")]
    public static extern bool GetThreadTimes(IntPtr thread, out long creation, out long exit,
                                             out long kernel, out long user);
    [DllImport("winmm.dll")]
    public static extern uint timeBeginPeriod(uint period);
    [DllImport("winmm.dll")]
    public static extern uint timeEndPeriod(uint period);
}

public sealed class DebugClient : IDisposable {
    readonly TcpClient client = new TcpClient();
    readonly StreamReader reader;
    readonly StreamWriter writer;
    int next_id;

    public DebugClient(string host, int port, int connect_timeout_ms) {
        if (!client.ConnectAsync(host, port).Wait(connect_timeout_ms)) {
            throw new TimeoutException("nothing listens on " + host + ":" + port +
                                       " (launch with GIURAY_NX2_DEBUG_SERVER set)");
        }
        client.NoDelay = true;
        NetworkStream stream = client.GetStream();
        stream.ReadTimeout = 180000;
        reader = new StreamReader(stream, new UTF8Encoding(false));
        writer = new StreamWriter(stream, new UTF8Encoding(false)) { AutoFlush = true, NewLine = "\n" };
    }

    /// Sends one request and returns the raw response line ({"id":..,"result":..} or {"error":..}).
    public string Call(string method, string params_json) {
        int id = ++next_id;
        writer.WriteLine("{\"id\":" + id + ",\"method\":\"" + method + "\",\"params\":" +
                         (string.IsNullOrEmpty(params_json) ? "{}" : params_json) + "}");
        string line = reader.ReadLine();
        if (line == null) {
            throw new IOException("the debug server closed the connection");
        }
        return line;
    }

    public void Dispose() {
        client.Dispose();
    }
}

public sealed class ThreadTime {
    public uint Id;
    public string Name = "";
    public long Time100ns;
}

public static class Threads {
    const uint QueryLimitedInformation = 0x0800;

    public static List<ThreadTime> Snapshot(int pid) {
        var result = new List<ThreadTime>();
        using Process process = Process.GetProcessById(pid);
        foreach (ProcessThread thread in process.Threads) {
            uint id = (uint)thread.Id;
            IntPtr handle = Native.OpenThread(QueryLimitedInformation, false, id);
            if (handle == IntPtr.Zero) {
                continue;
            }
            try {
                string name = "";
                if (Native.GetThreadDescription(handle, out IntPtr text) >= 0 && text != IntPtr.Zero) {
                    name = Marshal.PtrToStringUni(text) ?? "";
                    Native.LocalFree(text);
                }
                if (Native.GetThreadTimes(handle, out _, out _, out long kernel, out long user)) {
                    result.Add(new ThreadTime { Id = id, Name = name, Time100ns = kernel + user });
                }
            } finally {
                Native.CloseHandle(handle);
            }
        }
        return result;
    }

    /// CPU use of each thread between two snapshots, in percent of one host thread, busiest first.
    public static List<KeyValuePair<string, double>> Busy(List<ThreadTime> before, List<ThreadTime> after,
                                                          double seconds) {
        var start = before.ToDictionary(t => t.Id, t => t.Time100ns);
        var busy = new List<KeyValuePair<string, double>>();
        foreach (ThreadTime thread in after) {
            long previous = start.TryGetValue(thread.Id, out long value) ? value : 0;
            double percent = (thread.Time100ns - previous) / 1e7 / seconds * 100.0;
            string name = thread.Name.Length != 0 ? thread.Name : "tid " + thread.Id;
            busy.Add(new KeyValuePair<string, double>(name, percent));
        }
        busy.Sort((a, b) => b.Value.CompareTo(a.Value));
        return busy;
    }
}

public sealed class FrameStats {
    public int Frames;
    public double Seconds;
    public double AverageFps;
    public double AverageFrameMs;
    public double MedianFrameMs;
    public double P95FrameMs;
    public double P99FrameMs;
    public double MaxFrameMs;
    public double OnePercentLowFps;
    public double PointOnePercentLowFps;
    public int FramesOver100Ms;
    public double PollIntervalMs;
    public double[] FrameMs = Array.Empty<double>();
}

public static class FramePoller {
    static readonly Regex Presents = new Regex("\"presents\":(\\d+)", RegexOptions.Compiled);

    /// Polls the debug server's present counter every interval_ms for `seconds` and estimates
    /// each frame's time from when the counter moved. Resolution is about one poll interval;
    /// several presents between two polls share that time evenly.
    public static FrameStats Measure(DebugClient client, double seconds, int interval_ms) {
        var times = new List<double>();
        var counts = new List<long>();
        Native.timeBeginPeriod(1);
        try {
            Stopwatch watch = Stopwatch.StartNew();
            while (watch.Elapsed.TotalSeconds < seconds) {
                double before = watch.Elapsed.TotalMilliseconds;
                string reply = client.Call("status", "{\"fps_window_ms\":0}");
                double after = watch.Elapsed.TotalMilliseconds;
                Match match = Presents.Match(reply);
                if (match.Success) {
                    times.Add((before + after) * 0.5);
                    counts.Add(long.Parse(match.Groups[1].Value));
                }
                int sleep = interval_ms - (int)(watch.Elapsed.TotalMilliseconds - before);
                if (sleep > 0) {
                    Thread.Sleep(sleep);
                }
            }
        } finally {
            Native.timeEndPeriod(1);
        }
        var frames = new List<double>();
        double last_change = -1;
        for (int i = 1; i < counts.Count; ++i) {
            long delta = counts[i] - counts[i - 1];
            if (delta <= 0) {
                continue;
            }
            if (last_change >= 0) {
                double each = (times[i] - last_change) / delta;
                for (long k = 0; k < delta; ++k) {
                    frames.Add(each);
                }
            }
            last_change = times[i];
        }
        return Summarize(frames, times.Count > 1 ? (times[^1] - times[0]) / (times.Count - 1) : 0);
    }

    public static FrameStats Summarize(List<double> frames, double poll_ms) {
        var stats = new FrameStats { PollIntervalMs = poll_ms, FrameMs = frames.ToArray() };
        if (frames.Count == 0) {
            return stats;
        }
        double[] sorted = frames.OrderBy(x => x).ToArray();
        double total = sorted.Sum();
        double Percentile(double p) => sorted[Math.Min(sorted.Length - 1, (int)Math.Ceiling(p * sorted.Length) - 1)];
        double LowFps(double fraction) {
            int count = Math.Max(1, (int)Math.Ceiling(sorted.Length * fraction));
            return 1000.0 / sorted.Skip(sorted.Length - count).Average();
        }
        stats.Frames = sorted.Length;
        stats.Seconds = total / 1000.0;
        stats.AverageFps = sorted.Length / stats.Seconds;
        stats.AverageFrameMs = total / sorted.Length;
        stats.MedianFrameMs = Percentile(0.50);
        stats.P95FrameMs = Percentile(0.95);
        stats.P99FrameMs = Percentile(0.99);
        stats.MaxFrameMs = sorted[^1];
        stats.OnePercentLowFps = LowFps(0.01);
        stats.PointOnePercentLowFps = LowFps(0.001);
        stats.FramesOver100Ms = sorted.Count(x => x > 100.0);
        return stats;
    }
}

} // namespace GiurayPerf
