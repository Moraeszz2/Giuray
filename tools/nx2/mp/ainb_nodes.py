"""Lists the AINB node classes of this boot: vtables in the main module's data whose slot +0x28 is
the node base class function (main + 0x697EB0), with the name their getName (slot +0x98) returns
and their execute function (slot +0xE8). Names come from the module dump.
python ainb_nodes.py <main base> [filter text]"""
import json
import struct
import subprocess
import sys

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Giuray/tools/nx2/debug_mcp.py"
DUMP = "C:/Users/gui62/giuray-work/mp/private/main.bin"
code = open(DUMP, "rb").read()


def dbg(method, params):
    text = json.dumps({"method": method, "params": params})
    return json.loads(subprocess.run([PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call", text],
                                     capture_output=True, text=True, timeout=300).stdout)


def name_of(getter):
    """getName is adrp x0 / add x0 / ret: decode the string from the dump."""
    try:
        adrp, add = struct.unpack_from("<2I", code, getter)
    except struct.error:
        return None
    if adrp & 0x9F00001F != 0x90000000 or add & 0xFFC003FF != 0x91000000:
        return None
    imm = ((adrp >> 29) & 3) | (((adrp >> 5) & 0x7FFFF) << 2)
    target = (getter & ~0xFFF) + (imm << 12) + ((add >> 10) & 0xFFF)
    end = code.find(b"\0", target)
    return code[target:end].decode(errors="replace") if 0 <= target < len(code) else None


base = int(sys.argv[1], 16)
needle = struct.pack("<Q", base + 0x697EB0).hex()
hits, start = [], hex(base)
while True:
    r = dbg("search_memory", {"pattern": needle, "regions": "all", "start": start,
                              "end": hex(base + 0x5000000), "max_hits": 256, "budget_ms": 8000})
    hits += [int(h, 16) for h in r["hits"]]
    if r["complete"] and len(r["hits"]) < 256:
        break
    start = r.get("next") or hex(hits[-1] + 8)
nodes = []
for h in hits:
    vtable = h - 0x28
    raw = bytes.fromhex(dbg("read_memory", {"address": hex(vtable), "size": 0xF0})["hex"])
    getter = struct.unpack_from("<Q", raw, 0x98)[0] - base
    execute = struct.unpack_from("<Q", raw, 0xE8)[0] - base
    name = name_of(getter)
    if name:
        nodes.append((name, vtable, execute))
text = sys.argv[2] if len(sys.argv) > 2 else ""
print(len(nodes), "node classes")
for name, vtable, execute in sorted(nodes):
    if text in name:
        print(f"{name:60s} vtable {vtable:#x} execute main+{execute:#x}")
json.dump([{"name": n, "vtable": hex(v), "execute": hex(e)} for n, v, e in nodes],
          open("C:/Users/gui62/giuray-work/mp/ainb_nodes.json", "w"), indent=1)
