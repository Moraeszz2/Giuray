"""Looks for Link's skeleton: arrays of 3x4 world matrices (48 bytes each) whose translations are
all within a couple of units of Link. A candidate translation X sits at matrix + 12, with Y 16
bytes and Z 32 bytes after it.
python find_bones.py <main base>"""
import json
import struct
import subprocess
import sys
from collections import Counter

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Lichii/tools/nx2/debug_mcp.py"


def dbg(method, params):
    text = json.dumps({"method": method, "params": params})
    return json.loads(subprocess.run([PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call", text],
                                     capture_output=True, text=True, timeout=600).stdout)


def u64(address):
    return struct.unpack("<Q", bytes.fromhex(dbg("read_memory", {"address": hex(address), "size": 8})["hex"]))[0]


base = int(sys.argv[1], 16)
actor = base + 0x4E98438
for offset in (0xB8, 0x40, 0x0):
    actor = u64(actor) + offset
x, y, z = dbg("read_memory", {"address": hex(actor + 0x31C), "size": 12, "as": "f32"})["f32"]
print("Link", x, y, z, flush=True)
dbg("scan_memory", {"op": "start"})
r = dbg("scan_memory", {"op": "filter", "mode": "any", "min": x - 1.5, "max": x + 1.5, "show": 0})
print("floats near X:", r["count"], flush=True)
# Animated bones move a little every frame even while Link stands (idle breathing).
dbg("input", {"frames": 10})
r = dbg("scan_memory", {"op": "filter", "mode": "changed", "min": x - 1.5, "max": x + 1.5, "show": 1000})
print("near X and moving:", r["count"], flush=True)
rows = []
for c in r["candidates"]:
    a = int(c["address"], 16)
    v = dbg("read_memory", {"address": hex(a - 12), "size": 48, "as": "f32"}).get("f32")
    if not v or any(isinstance(t, str) for t in v):
        continue
    if abs(v[7] - y) < 2.5 and abs(v[11] - z) < 2.5:
        rows.append(a - 12)
print(len(rows), "matrices with Link's X, Y and Z near")
blocks = Counter(a & ~0xFFF for a in rows)
for page, count in blocks.most_common(8):
    print(f"  page {page:#x}: {count} matrices")
json.dump([hex(a) for a in rows], open("C:/Users/gui62/lichii-perf/mp/bones.json", "w"))
