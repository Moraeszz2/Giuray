"""Finds the camera: f32 values that change when only the camera turns (right stick, Link still)
and stay put otherwise; lists X/Y/Z triples a few units from Link (the eye) and near him (the
point the camera looks at)."""
import json
import struct
import subprocess
import sys

sys.path.insert(0, "C:/Users/gui62/lichii-perf/mp")
from rsp import call  # noqa: E402

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Lichii/tools/nx2/debug_mcp.py"


def dbg(method, params):
    text = json.dumps({"method": method, "params": params})
    out = subprocess.run([PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call", text],
                         capture_output=True, text=True, timeout=300).stdout
    return json.loads(out)


def filt(**kw):
    r = dbg("scan_memory", {"op": "filter", **kw})
    print(kw.get("mode"), r["count"], flush=True)
    return r


actor = int(sys.argv[1], 16)
link = struct.unpack("<3f", bytes.fromhex(call(op="mem", address=hex(actor + 0x31C), size=12)["hex"]))
print("Link", link, flush=True)
print(dbg("scan_memory", {"op": "start"}), flush=True)
dbg("input", {"right_stick": [1, 0], "frames": 30})
dbg("input", {"frames": 20})
filt(mode="changed", min_change=0.05, min=-100000, max=100000, show=0)
dbg("input", {"frames": 30})
filt(mode="unchanged", tolerance=0.0001, show=0)
for stick in ([-1, 0], [1, 0], [-1, 0]):
    dbg("input", {"right_stick": stick, "frames": 30})
    dbg("input", {"frames": 20})
    filt(mode="changed", min_change=0.05, show=0)
    dbg("input", {"frames": 30})
    r = filt(mode="unchanged", tolerance=0.0001, show=1000)
values = {int(c["address"], 16): c["value"] for c in r["candidates"]}
triples = []
for a in sorted(values):
    if a + 4 in values or a + 8 in values:
        xyz = struct.unpack("<3f", bytes.fromhex(call(op="mem", address=hex(a), size=12)["hex"]))
        d = sum((xyz[i] - link[i]) ** 2 for i in range(3)) ** 0.5
        if d < 40 and abs(xyz[1] - link[1]) < 20:
            triples.append((d, a, xyz))
for d, a, xyz in sorted(triples)[:40]:
    print(f"{a:#x} dist {d:6.2f} xyz {[round(v, 3) for v in xyz]}")
json.dump(r, open("C:/Users/gui62/lichii-perf/mp/camera.json", "w"))
