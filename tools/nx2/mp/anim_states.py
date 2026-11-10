"""Reads the animation-state candidates (anim_candidates.json) standing, walking slowly (half
stick) and running (full stick), reading through the GDB stub while the debug server holds the
stick, and lists those with one value per state.
python anim_states.py"""
import json
import struct
import subprocess
import sys
import time

sys.path.insert(0, "C:/Users/gui62/lichii-perf/mp")
from rsp import call  # noqa: E402

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Lichii/tools/nx2/debug_mcp.py"
candidates = [int(a, 16) for a in json.load(open("C:/Users/gui62/lichii-perf/mp/anim_candidates.json"))]


def read_all():
    values = {}
    for a in candidates:
        data = call(op="mem", address=hex(a), size=4).get("hex", "")
        if len(data) == 8:
            values[a] = struct.unpack("<I", bytes.fromhex(data))[0]
    return values


def during(stick, frames=150):
    walker = subprocess.Popen([PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call",
                               json.dumps({"method": "input", "params": {"left_stick": stick, "frames": frames}})],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.8)
    values = read_all()
    walker.wait(timeout=120)
    return values


stand1 = read_all()
walk = during([0, 0.35])
time.sleep(1.5)
stand2 = read_all()
run = during([0, -1.0])
time.sleep(1.5)
stand3 = read_all()
rows = []
for a in candidates:
    try:
        s1, w, s2, r, s3 = stand1[a], walk[a], stand2[a], run[a], stand3[a]
    except KeyError:
        continue
    if s1 == s2 == s3 and w != s1 and r != s1:
        rows.append((a, s1, w, r))
print(len(rows), "words with one standing value and other values walking and running:")
for a, s, w, r in rows[:60]:
    print(f"{a:#x}: stand {s:#010x} walk {w:#010x} run {r:#010x}{'  (walk == run)' if w == r else ''}")
json.dump([{"address": hex(a), "stand": s, "walk": w, "run": r} for a, s, w, r in rows],
          open("C:/Users/gui62/lichii-perf/mp/anim_states.json", "w"), indent=1)
