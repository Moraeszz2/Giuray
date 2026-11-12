"""Asks the game to move an actor through its pending-position field (actor + 0x358, NaN = none;
the transform function copies it to actor + 0x31C and clears it). Uses only the debug server.
python request_pos.py <main base> link <dx> <dy> <dz>
python request_pos.py <main base> 0x<actor> <dx> <dy> <dz>"""
import json
import math
import struct
import subprocess
import sys
import time

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Giuray/tools/nx2/debug_mcp.py"
LINK_CHAIN = (0x4E98438, (0xB8, 0x40, 0x0))


def dbg(method, params, out=None):
    args = [PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call",
            json.dumps({"method": method, "params": params})]
    if out:
        args += ["--out", out]
    return json.loads(subprocess.run(args, capture_output=True, text=True, timeout=120).stdout)


def u64(a):
    return struct.unpack("<Q", bytes.fromhex(dbg("read_memory", {"address": hex(a), "size": 8})["hex"]))[0]


def f3(a):
    return dbg("read_memory", {"address": hex(a), "size": 12, "as": "f32"})["f32"]


base = int(sys.argv[1], 16)
if sys.argv[2] == "link":
    actor = base + LINK_CHAIN[0]
    for offset in LINK_CHAIN[1]:
        actor = u64(actor) + offset
else:
    actor = int(sys.argv[2], 16)
delta = [float(v) for v in sys.argv[3:6]]
now = f3(actor + 0x31C)
pending = f3(actor + 0x358)
print("actor", hex(actor), "position", [round(v, 3) for v in now], "pending", pending)
dbg("screenshot", {}, out="C:/Users/gui62/giuray-work/mp/req_before.png")
target = [now[i] + delta[i] for i in range(3)]
dbg("write_memory", {"address": hex(actor + 0x358), "f32": target})
time.sleep(0.5)
print("pending after", f3(actor + 0x358), "position after", [round(v, 3) for v in f3(actor + 0x31C)])
dbg("screenshot", {}, out="C:/Users/gui62/giuray-work/mp/req_after.png")
