"""Reads the strings reachable from Link's AS component (actor + 0x1F0, the component ExecuteASPlay
hands to the play call) standing and walking, to find the field naming the playing AS.
python as_names.py <main base>"""
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
base = int(sys.argv[1], 16)


def read(a, n):
    h = call(op="mem", address=hex(a), size=n).get("hex", "")
    return bytes.fromhex(h) if len(h) == 2 * n else None


def u64(a):
    b = read(a, 8)
    return struct.unpack("<Q", b)[0] if b else 0


def text_at(p):
    b = read(p, 48)
    m = re.match(rb"[\x20-\x7e]{3,}", b) if b else None
    return m.group().decode() if m else None


actor = base + 0x4E98438
for off in (0xB8, 0x40, 0x0):
    actor = u64(actor) + off
component = u64(actor + 0x1F0)
print("Link actor", hex(actor), "AS component", hex(component))


def snapshot(depth_bytes=0x400):
    found = {}
    data = read(component, depth_bytes) or b""
    for off in range(0, len(data) - 7, 8):
        v = struct.unpack_from("<Q", data, off)[0]
        if not (0x2100000000 <= v < 0x2600000000 or base <= v < base + 0x5000000):
            continue
        t = text_at(v)
        if t and len(t) >= 3:
            found[f"+{off:#x}"] = t
            continue
        inner = read(v, 0x100) or b""
        for o2 in range(0, len(inner) - 7, 8):
            w = struct.unpack_from("<Q", inner, o2)[0]
            if 0x2100000000 <= w < 0x2600000000 or base <= w < base + 0x5000000:
                t = text_at(w)
                if t and len(t) >= 3:
                    found[f"+{off:#x}->+{o2:#x}"] = t
    return found


stand = snapshot()
walker = subprocess.Popen([PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call",
                           json.dumps({"method": "input", "params": {"left_stick": [0, 1], "frames": 200}})],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(1.0)
walk = snapshot()
walker.wait(timeout=120)
for key in sorted(set(stand) | set(walk)):
    a, b = stand.get(key), walk.get(key)
    mark = "  <- changes" if a != b else ""
    print(f"{key:18s} stand {str(a)[:40]:42s} walk {str(b)[:40]}{mark}")
