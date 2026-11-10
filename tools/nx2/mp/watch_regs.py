"""Write-watches one address, waits for the hit, prints all registers and lets the game run."""
import json
import sys

sys.path.insert(0, "C:/Users/gui62/lichii-perf/mp")
from rsp import call  # noqa: E402

address = sys.argv[1]
call(op="watch", address=address, size=4, kind="write")
stop = call(op="wait", timeout=10).get("stop")
regs = call(op="regs") if stop else None
extra = {}
if regs:
    for spec in sys.argv[2:]:
        # "x20+8": read the 8 bytes at that register plus offset while the game is stopped.
        reg, offset = spec.split("+")
        where = int(regs[reg], 16) + int(offset, 0)
        extra[spec] = call(op="mem", address=hex(where), size=8)["hex"]
call(op="unwatch", address=address, size=4, kind="write")
if stop:
    call(op="cont")
print(json.dumps({"stop": stop, "regs": regs, "extra": extra}, indent=1))
