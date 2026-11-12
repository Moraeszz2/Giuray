"""Lists actor-like objects that move on their own (Link standing still): two reads of every actor's
X/Y/Z, `seconds` apart."""
import json
import re
import struct
import subprocess
import sys
import time

sys.path.insert(0, "C:/Users/gui62/giuray-work/mp")
from rsp import call  # noqa: E402

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Giuray/tools/nx2/debug_mcp.py"
ACTOR_VTABLE_OFFSET = 0x85929E18 - 0x80E04000
seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 2.0


def dbg(method, params):
    text = json.dumps({"method": method, "params": params})
    out = subprocess.run([PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call", text],
                         capture_output=True, text=True, timeout=300).stdout
    return json.loads(out)


def main_base():
    text = bytes.fromhex(call(op="raw", packet="qRcmd," + b"get mappings".hex())["reply"]).decode("latin-1")
    return [int(m.group(1), 16) for m in re.finditer(r"0x([0-9a-f]+) - 0x[0-9a-f]+ r-x Code", text)][1]


def pos(a):
    return struct.unpack("<3f", bytes.fromhex(call(op="mem", address=hex(a + 0x31C), size=12)["hex"]))


base = main_base()
vtable = struct.pack("<Q", base + ACTOR_VTABLE_OFFSET).hex()
actors, start = [], None
while True:
    params = {"pattern": vtable, "max_hits": 256, "budget_ms": 8000}
    if start:
        params["start"] = start
    r = dbg("search_memory", params)
    actors += [int(h, 16) for h in r["hits"]]
    if r["complete"] and len(r["hits"]) < 256:
        break
    start = r.get("next") or hex(int(r["hits"][-1], 16) + 8)
before = {}
for a in actors:
    try:
        p = pos(a)
    except Exception:
        continue
    if all(abs(v) < 1e5 for v in p):
        before[a] = p
time.sleep(seconds)
moved = []
for a, p in before.items():
    q = pos(a)
    d = sum((q[i] - p[i]) ** 2 for i in range(3)) ** 0.5
    if d > 0.2:
        moved.append((d, a, p, q))
print(len(before), "actors;", len(moved), "moved:")
for d, a, p, q in sorted(moved, reverse=True)[:30]:
    print(f"  {a:#x} moved {d:5.2f}: {[round(v, 2) for v in p]} -> {[round(v, 2) for v in q]}")
