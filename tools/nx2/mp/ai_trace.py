"""Catches AINB node dispatches (the 155 execute virtual calls) until one runs for a given actor:
a register or a stack word near the frame pointer holds the actor's address. Prints the registers
and the frame-pointer backtrace of that hit, to find the actor's AI update path.
python ai_trace.py <main base> <actor> [max hits]"""
import struct
import subprocess
import sys

sys.path.insert(0, "C:/Users/gui62/lichii-perf/mp")
from rsp import call  # noqa: E402

PY = "B:/msys64/ucrt64/bin/python.exe"
base = int(sys.argv[1], 16)
actor = int(sys.argv[2], 16)
limit = int(sys.argv[3]) if len(sys.argv) > 3 else 40
sites = subprocess.run([PY, "-I", "B:/projetos/Lichii/tools/nx2/mp/vcalls.py", "0xe8"],
                       capture_output=True, text=True).stdout.split()[5:]
sites = [int(s.lstrip("+"), 16) for s in sites if s.startswith("+")]


def raw(packet):
    return call(op="raw", packet=packet).get("reply")


def u64(address):
    data = call(op="mem", address=hex(address), size=8).get("hex")
    return struct.unpack("<Q", bytes.fromhex(data))[0] if data else 0


for site in sites:
    raw(f"Z0,{base + site:x},4")
found = None
try:
    for _ in range(limit):
        stop = call(op="wait", timeout=20).get("stop")
        if not stop:
            break
        regs = struct.unpack_from("<33Q", bytes.fromhex(raw("g")))
        sp, pc, fp = regs[31], regs[32], regs[29]
        stack = bytes.fromhex(call(op="mem", address=hex(sp), size=0x400).get("hex", ""))
        words = struct.unpack_from(f"<{len(stack) // 8}Q", stack) if stack else ()
        where = [f"x{i}" for i in range(31) if regs[i] == actor] + \
                [f"[sp+{i * 8:#x}]" for i, w in enumerate(words) if w == actor]
        if where:
            found = (regs, where)
            break
        raw("c")  # all-stop: let everything run to the next dispatch
finally:
    for site in sites:
        raw(f"z0,{base + site:x},4")
if not found:
    print("no dispatch for that actor seen")
    call(op="cont")
    sys.exit(1)
regs, where = found
print("actor in", where, "pc main+%#x" % (regs[32] - base))
print(" ".join(f"x{i}={regs[i]:#x}" for i in range(31)))
frame = regs[29]
for depth in range(16):
    if not frame:
        break
    ret = u64(frame + 8)
    print(f"  #{depth} return main+{ret - base:#x}" if base <= ret < base + 0x5000000 else f"  #{depth} {ret:#x}")
    frame = u64(frame)
call(op="cont")
