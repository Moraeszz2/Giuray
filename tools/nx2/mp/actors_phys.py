"""Lists actors near a point with their physics link (actor + 0x270) and pending position
(actor + 0x358), one debug-server read per actor, printing progress.
python actors_phys.py <main base> <x> <y> <z> [radius]"""
import json
import struct
import subprocess
import sys

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Lichii/tools/nx2/debug_mcp.py"


def dbg(method, params):
    text = json.dumps({"method": method, "params": params})
    return json.loads(subprocess.run([PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call", text],
                                     capture_output=True, text=True, timeout=300).stdout)


base = int(sys.argv[1], 16)
point = [float(v) for v in sys.argv[2:5]]
radius = float(sys.argv[5]) if len(sys.argv) > 5 else 12.0
vtable = base + (0x85929E18 - 0x80E04000)
hits, start = [], None
while True:
    params = {"pattern": struct.pack("<Q", vtable).hex(), "max_hits": 256, "budget_ms": 8000}
    if start:
        params["start"] = start
    r = dbg("search_memory", params)
    hits += [int(h, 16) for h in r["hits"]]
    if r["complete"] and len(r["hits"]) < 256:
        break
    start = r.get("next") or hex(int(r["hits"][-1], 16) + 8)
print(len(hits), "actors", flush=True)
rows = []
for n, a in enumerate(hits):
    if n % 50 == 0:
        print(f"{100 * n // len(hits)}%", flush=True)
    try:
        b = bytes.fromhex(dbg("read_memory", {"address": hex(a + 0x270), "size": 0xF8})["hex"])
    except Exception:
        continue
    phys = struct.unpack_from("<Q", b, 0)[0]
    pos = struct.unpack_from("<3f", b, 0x31C - 0x270)
    pending = struct.unpack_from("<I", b, 0x358 - 0x270)[0]
    if not all(abs(v) < 1e5 for v in pos):
        continue
    d = sum((pos[i] - point[i]) ** 2 for i in range(3)) ** 0.5
    if d < radius:
        rows.append((d, a, phys, pending, pos))
print("100%")
for d, a, phys, pending, pos in sorted(rows)[:40]:
    print(f"{a:#x} d {d:5.2f} physics {phys:#x} pending {pending:#010x} pos {[round(v, 2) for v in pos]}")
