"""Looks for Link's animation state: 32-bit words that change when he starts walking, stay the same
while he keeps walking, change back when he stops and stay the same while he stands; then keeps
those whose standing value is the same each time (a state id or hash, not a counter).
python find_anim.py"""
import json
import subprocess

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Giuray/tools/nx2/debug_mcp.py"


def dbg(method, params):
    text = json.dumps({"method": method, "params": params})
    return json.loads(subprocess.run([PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call", text],
                                     capture_output=True, text=True, timeout=600).stdout)


def scan(**kw):
    r = dbg("scan_memory", {"op": "filter", "type": "u32", **kw})
    print(kw["mode"], r.get("count"), flush=True)
    return r


def walk(frames):
    dbg("input", {"left_stick": [0, 1], "frames": frames})


def stand(frames):
    dbg("input", {"frames": frames})


stand(40)
print(dbg("scan_memory", {"op": "start"}), flush=True)
standing = None
for round_index in range(3):
    # Start walking and keep walking: the walk animation state.
    dbg("input_sequence", {"steps": [{"left_stick": [0, 1], "frames": 20}]})
    scan(mode="changed")
    walk(20)
    scan(mode="unchanged")
    # Stop and stand: back to the idle state.
    stand(40)
    scan(mode="changed")
    stand(20)
    r = scan(mode="unchanged", show=1000)
    values = {c["address"]: c["value"] for c in r.get("candidates", [])}
    if standing is not None:
        same = {a: v for a, v in values.items() if standing.get(a) == v}
        print(f"round {round_index}: {len(same)} of the first {len(values)} keep their standing value",
              flush=True)
    standing = values
json.dump(standing, open("C:/Users/gui62/giuray-work/mp/anim_candidates.json", "w"), indent=1)
