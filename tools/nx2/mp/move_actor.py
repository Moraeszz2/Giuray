"""Tries to move an actor: keeps writing X/Y/Z at `address` (+dx, +dy, +dz) through the GDB stub for
`seconds`, takes a screenshot meanwhile, then reports whether the value stuck.
python move_actor.py <address> <dx> <dy> <dz> [seconds] [shot name]"""
import json
import struct
import subprocess
import sys
import threading
import time

sys.path.insert(0, "C:/Users/gui62/lichii-perf/mp")
from rsp import call  # noqa: E402

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Lichii/tools/nx2/debug_mcp.py"

address = int(sys.argv[1], 16)
delta = [float(v) for v in sys.argv[2:5]]
seconds = float(sys.argv[5]) if len(sys.argv) > 5 else 2.0
shot = sys.argv[6] if len(sys.argv) > 6 else "move"


def read3(a):
    return struct.unpack("<3f", bytes.fromhex(call(op="mem", address=hex(a), size=12)["hex"]))


start = read3(address)
target = [start[i] + delta[i] for i in range(3)]
data = struct.pack("<3f", *target).hex()
stop = threading.Event()


def writer():
    while not stop.is_set():
        call(op="write", address=hex(address), hex=data)


thread = threading.Thread(target=writer)
thread.start()
time.sleep(seconds / 2)
subprocess.run([PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call", '{"method":"screenshot"}',
                "--out", f"C:/Users/gui62/lichii-perf/mp/{shot}.png"], capture_output=True, timeout=120)
time.sleep(seconds / 2)
stop.set()
thread.join()
during = read3(address)
time.sleep(0.5)
after = read3(address)
print("start ", [round(v, 3) for v in start])
print("target", [round(v, 3) for v in target])
print("after writes stop:", [round(v, 3) for v in during], "0.5 s later:", [round(v, 3) for v in after])
