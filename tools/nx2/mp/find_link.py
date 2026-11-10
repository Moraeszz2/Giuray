"""Finds Link's live X/Y/Z copies: unknown-value scan driven by injected stick input."""
import json
import subprocess
import sys

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Lichii/tools/nx2/debug_mcp.py"


def call(method, params=None, out=None):
    text = json.dumps({"method": method, "params": params or {}})
    args = [PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call", text]
    if out:
        args += ["--out", out]
    result = subprocess.run(args, capture_output=True, text=True, timeout=300).stdout
    return json.loads(result)


def walk(stick, frames=30):
    call("input", {"left_stick": stick, "frames": frames})
    call("input", {"frames": 25})


def filt(**kw):
    r = call("scan_memory", {"op": "filter", **kw})
    print(kw.get("mode"), r["count"], flush=True)
    return r


status = call("status")
print("build", status["build"], "fps", round(status["fps"], 1))
print(call("scan_memory", {"op": "start"}))
walk([0, 1])
filt(mode="changed", min_change=0.5, min=-100000, max=100000, show=0)
filt(mode="unchanged", tolerance=0.0001, show=0)
for stick in ([0, -1], [0, 1], [0, -1], [1, 0]):
    walk(stick)
    filt(mode="changed", min_change=0.5, show=0)
    call("input", {"frames": 25})
    r = filt(mode="unchanged", tolerance=0.0001, show=1000)
json.dump(r, open("C:/Users/gui62/lichii-perf/mp/found.json", "w"))
