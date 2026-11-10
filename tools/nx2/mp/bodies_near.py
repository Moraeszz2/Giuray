"""Lists physics body records (world vtable main + 0x48B51B0, chunk list at +0x48, records of 0xC0
bytes, X/Y/Z at +0xF0) within `radius` of a point. python bodies_near.py <x> <y> <z> [radius]"""
import re
import struct
import sys

sys.path.insert(0, "C:/Users/gui62/lichii-perf/mp")
from rsp import call  # noqa: E402

import json
import subprocess

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Lichii/tools/nx2/debug_mcp.py"
WORLD_VTABLE_OFFSET = 0x856B91B0 - 0x80E04000


def dbg(method, params):
    text = json.dumps({"method": method, "params": params})
    out = subprocess.run([PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call", text],
                         capture_output=True, text=True, timeout=300).stdout
    return json.loads(out)


def main_base():
    text = bytes.fromhex(call(op="raw", packet="qRcmd," + b"get mappings".hex())["reply"]).decode("latin-1")
    return [int(m.group(1), 16) for m in re.finditer(r"0x([0-9a-f]+) - 0x[0-9a-f]+ r-x Code", text)][1]


def read(a, n):
    return bytes.fromhex(dbg("read_memory", {"address": hex(a), "size": n})["hex"])


point = [float(v) for v in sys.argv[1:4]]
radius = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0
base = main_base()
r = dbg("search_memory", {"pattern": struct.pack("<Q", base + WORLD_VTABLE_OFFSET).hex(),
                          "max_hits": 16, "budget_ms": 10000})
for world in [int(h, 16) for h in r["hits"]]:
    chunk_list = struct.unpack("<Q", read(world + 0x48, 8))[0]
    for chunk in [c for c in struct.unpack("<16Q", read(chunk_list, 128)) if c]:
        data = read(chunk, 0xC000)
        for i in range(0xC000 // 0xC0 - 2):
            p = struct.unpack_from("<3f", data, i * 0xC0 + 0xF0)
            d = sum((p[k] - point[k]) ** 2 for k in range(3)) ** 0.5
            if d < radius:
                handle = struct.unpack_from("<Q", data, i * 0xC0 + 0xB0)[0]
                print(f"world {world:#x} record {chunk + i * 0xC0:#x} pos@{chunk + i * 0xC0 + 0xF0:#x} "
                      f"{[round(v, 3) for v in p]} dist {d:.2f} handle {handle:#x}")
