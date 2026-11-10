"""Asks the game to create an actor by name, calling its ActorMgr create request (main + 0x372A70,
the call the AINB node ExecuteGenericRequestCreateActor makes) from a game thread through the GDB
stub: stop a thread at a breakpoint, save its registers, run the call with our arguments, stop at
a return breakpoint, put the registers back. Needs rsp_daemon.py and the puppet cave (mp6 or
later): the name and the create parameters are written into the .text padding.
python spawn_actor.py <main base> <actor name> <x> <y> <z>"""
import json
import struct
import subprocess
import sys
import time

sys.path.insert(0, "C:/Users/gui62/lichii-perf/mp")
from rsp import call  # noqa: E402

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Lichii/tools/nx2/debug_mcp.py"
CREATE = 0x372A70
MGR_GOT = 0x4C7F3E0
SCALE_GOT = 0x4C7E0B8
CAVE = 0x3C46400
DATA = CAVE + 0x200  # name slot, name, parameters
STOP = CAVE + 0x1F0  # return address of the injected call (a breakpoint, never executed)
# Where to catch a game thread: AINB node executes (the context the game creates actors from),
# from HITS (comma-separated module offsets); the default is NPC locomotion and a wait node.
import os
HITS = [int(v, 16) for v in os.environ.get("HITS", "0x26280f0,0x26e7ee0").split(",")]


def dbg(method, params):
    text = json.dumps({"method": method, "params": params})
    return json.loads(subprocess.run([PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call", text],
                                     capture_output=True, text=True, timeout=120).stdout)


def u64(address):
    return struct.unpack("<Q", bytes.fromhex(call(op="mem", address=hex(address), size=8)["hex"]))[0]


def raw(packet):
    return call(op="raw", packet=packet).get("reply")


def thread_of(stop_reply):
    for field in stop_reply.split(";"):
        if field.startswith("thread:"):
            return field[7:]
    return None


base = int(sys.argv[1], 16)
name = sys.argv[2].encode() + b"\0"
position = [float(v) for v in sys.argv[3:6]]

manager = u64(u64(base + MGR_GOT))
scale = bytes.fromhex(call(op="mem", address=hex(u64(base + SCALE_GOT)), size=12)["hex"])
print("ActorMgr", hex(manager), "scale", struct.unpack("<3f", scale))

name_address = base + DATA + 0x10
param_address = base + DATA + 0x80
param = bytearray(0xA0)
struct.pack_into("<3f", param, 0x00, *position)
struct.pack_into("<9f", param, 0x0C, 1, 0, 0, 0, 1, 0, 0, 0, 1)
param[0x30:0x3C] = scale
# The node's InstanceName input: a string the create request reads (a null pointer faults).
instance_address = base + DATA + 0x60
struct.pack_into("<Q", param, 0x40, instance_address)
param[0x98] = 3
blob = struct.pack("<Q", name_address) + b"\0" * 8 + name
blob = blob.ljust(0x60, b"\0") + b"MpPuppet\0"
dbg("write_memory", {"address": hex(base + DATA), "hex": blob.hex()})
dbg("write_memory", {"address": hex(param_address), "hex": bytes(param).hex()})

# Catch a game thread in an AINB node execute.
for hit in HITS:
    print("break at", hex(base + hit), raw(f"Z0,{base + hit:x},4"))
stop = call(op="wait", timeout=60).get("stop")
for hit in HITS:
    raw(f"z0,{base + hit:x},4")
if not stop:
    print("no thread reached the breakpoint")
    sys.exit(1)
saved = raw("g")
open("C:/Users/gui62/lichii-perf/mp/spawn_saved_regs.txt", "w").write(saved)
regs = bytearray(bytes.fromhex(saved))
sp = struct.unpack_from("<Q", regs, 31 * 8)[0]
print("stopped:", stop[:60], "sp", hex(sp))

# The call: x0 manager, x1 &name pointer, x2 parameters, x3 no handle, w4 1, x5-x7 0, and three
# stack arguments (u8 0, u64 0, u64 0) on a new frame below the thread's stack pointer.
frame = (sp - 0x200) & ~0xF
call(op="write", address=hex(frame), hex="00" * 0x40)
args = [manager, base + DATA, param_address, 0, 1, 0, 0, 0]
for index, value in enumerate(args):
    struct.pack_into("<Q", regs, index * 8, value)
struct.pack_into("<Q", regs, 30 * 8, base + STOP)  # x30
struct.pack_into("<Q", regs, 31 * 8, frame)  # sp
struct.pack_into("<Q", regs, 32 * 8, base + CREATE)  # pc
print("set registers", raw("G" + regs.hex()))
print("return break", raw(f"Z0,{base + STOP:x},4"))
call(op="cont")
back = call(op="wait", timeout=30).get("stop")
raw(f"z0,{base + STOP:x},4")
if not back:
    print("the call did not return within 30 s; the thread stays inside it")
    sys.exit(1)
back_regs = bytes.fromhex(raw("g"))
back_pc = struct.unpack_from("<Q", back_regs, 32 * 8)[0]
if back_pc != base + STOP:
    # Something else stopped the game (a fault inside the call, another thread): putting the
    # saved registers back would corrupt whichever thread stopped. Leave it for inspection.
    print(f"stopped elsewhere: pc {back_pc:#x} (main + {back_pc - base:#x}), {back[:80]}")
    if thread_of(back) == thread_of(stop):
        # A fault inside the call on our own thread: unwind it by putting the thread back where
        # it was caught; the request it was building is dropped.
        print("same thread: restoring it", raw("G" + saved))
        call(op="cont")
    sys.exit(1)
result = struct.unpack_from("<Q", back_regs, 0)[0]
print("returned:", back[:60], "x0 =", hex(result))
print("restore registers", raw("G" + saved))
call(op="cont")
print("done")
