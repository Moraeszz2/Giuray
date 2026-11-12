"""Lists actor-like objects (Link's actor vtable) near Link: address, distance and X/Y/Z."""
import json
import re
import struct
import subprocess
import sys

sys.path.insert(0, "C:/Users/gui62/giuray-work/mp")
from rsp import call  # noqa: E402

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Giuray/tools/nx2/debug_mcp.py"
ACTOR_VTABLE_OFFSET = 0x85929E18 - 0x80E04000
LINK_CHAIN = (0x4E98438, (0xB8, 0x40, 0x0))


def dbg(method, params):
    text = json.dumps({"method": method, "params": params})
    out = subprocess.run([PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call", text],
                         capture_output=True, text=True, timeout=300).stdout
    return json.loads(out)


def main_base():
    text = bytes.fromhex(call(op="raw", packet="qRcmd," + b"get mappings".hex())["reply"]).decode("latin-1")
    return [int(m.group(1), 16) for m in re.finditer(r"0x([0-9a-f]+) - 0x[0-9a-f]+ r-x Code", text)][1]


def u64(a):
    return struct.unpack("<Q", bytes.fromhex(call(op="mem", address=hex(a), size=8)["hex"]))[0]


def pos(a):
    return struct.unpack("<3f", bytes.fromhex(call(op="mem", address=hex(a + 0x31C), size=12)["hex"]))


base = main_base()
node = base + LINK_CHAIN[0]
for offset in LINK_CHAIN[1]:
    node = u64(node) + offset
link_actor = node
link = pos(link_actor)
print("main", hex(base), "Link actor", hex(link_actor), [round(v, 2) for v in link], flush=True)
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
near = []
for a in actors:
    try:
        p = pos(a)
    except Exception:
        continue
    if not all(abs(v) < 1e5 for v in p):
        continue
    d = sum((p[i] - link[i]) ** 2 for i in range(3)) ** 0.5
    if d < 15:
        near.append((d, a, p))
print(len(actors), "objects with the vtable;", len(near), "within 15 units:")
for d, a, p in sorted(near)[:25]:
    print(f"  {a:#x} dist {d:6.2f} pos {[round(v, 2) for v in p]}")
