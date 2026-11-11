"""Calls a game function from a game thread through the GDB stub (as spawn_actor.py does): stop a
thread where AINB dispatches a node's execute, run main + <function> with up to eight arguments,
stop at a return breakpoint, put the thread back. Arguments: integers (0x...), or
"blob:<hex>" for bytes written into the .text padding (the argument is their address).
python call_fn.py <main base> <function offset> [args...]"""
import os
import struct
import subprocess
import sys

sys.path.insert(0, "C:/Users/gui62/lichii-perf/mp")
from rsp import call  # noqa: E402

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Lichii/tools/nx2/debug_mcp.py"
CAVE = 0x3C46400
STOP = CAVE + 0x1F0
DATA = CAVE + 0x400  # call_fn's own blobs, after spawn_actor's data


def raw(packet):
    return call(op="raw", packet=packet).get("reply")


base = int(sys.argv[1], 16)
function = int(sys.argv[2], 16)
args = []
cursor = base + DATA
for text in sys.argv[3:]:
    if text.startswith("blob:"):
        data = bytes.fromhex(text[5:])
        call(op="write", address=hex(cursor), hex=data.hex())
        args.append(cursor)
        cursor = (cursor + len(data) + 15) & ~15
    else:
        args.append(int(text, 0))
sites = subprocess.run([PY, "-I", "B:/projetos/Lichii/tools/nx2/mp/vcalls.py", "0xe8"],
                       capture_output=True, text=True).stdout.split()
sites = [int(s.lstrip("+"), 16) for s in sites if s.startswith("+")]
for site in sites:
    raw(f"Z0,{base + site:x},4")
stop = call(op="wait", timeout=60).get("stop")
for site in sites:
    raw(f"z0,{base + site:x},4")
if not stop:
    print("no thread reached a dispatch")
    sys.exit(1)
saved = raw("g")
open("C:/Users/gui62/lichii-perf/mp/call_saved_regs.txt", "w").write(saved)
regs = bytearray(bytes.fromhex(saved))
sp = struct.unpack_from("<Q", regs, 31 * 8)[0]
frame = (sp - 0x200) & ~0xF
call(op="write", address=hex(frame), hex="00" * 0x40)
for index in range(8):
    struct.pack_into("<Q", regs, index * 8, args[index] if index < len(args) else 0)
struct.pack_into("<Q", regs, 30 * 8, base + STOP)
struct.pack_into("<Q", regs, 31 * 8, frame)
struct.pack_into("<Q", regs, 32 * 8, base + function)
raw("G" + regs.hex())
raw(f"Z0,{base + STOP:x},4")
call(op="cont")
back = call(op="wait", timeout=30).get("stop")
# A second thread that hit a dispatch breakpoint with the first leaves a stale stop: our thread is
# still at the function's entry then, so let it go on.
for _ in range(5):
    if not back or struct.unpack_from("<Q", bytes.fromhex(raw("g")), 32 * 8)[0] != base + function:
        break
    call(op="cont")
    back = call(op="wait", timeout=30).get("stop")
raw(f"z0,{base + STOP:x},4")
if not back:
    print("the call did not return within 30 s")
    sys.exit(1)
back_regs = bytes.fromhex(raw("g"))
pc = struct.unpack_from("<Q", back_regs, 32 * 8)[0]
if pc != base + STOP:
    print(f"stopped elsewhere: main + {pc - base:#x}; restoring the caught thread anyway")
else:
    print("returned x0 =", hex(struct.unpack_from("<Q", back_regs, 0)[0]))
print("restore", raw("G" + saved))
call(op="cont")
