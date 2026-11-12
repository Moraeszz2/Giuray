"""Follows each saved chain in this boot (main module base from the GDB stub's mappings) and lists
where they land: the chains that still reach an object with Link's actor vtable and a sane
position survive. python validate_chains.py chains_actor_rel.json"""
import json
import re
import struct
import sys
from collections import Counter

sys.path.insert(0, "C:/Users/gui62/giuray-work/mp")
from rsp import call  # noqa: E402

ACTOR_VTABLE_OFFSET = 0x85929E18 - 0x80E04000


def main_base():
    text = bytes.fromhex(call(op="raw", packet="qRcmd," + b"get mappings".hex())["reply"]).decode("latin-1")
    code = [int(m.group(1), 16) for m in re.finditer(r"0x([0-9a-f]+) - 0x[0-9a-f]+ r-x Code", text)]
    return code[1]  # [0] is rtld, [1] the main module


def u64(address):
    return struct.unpack("<Q", bytes.fromhex(call(op="mem", address=hex(address), size=8)["hex"]))[0]


base = main_base()
print("main module at", hex(base))
saved = json.load(open(sys.argv[1]))
landing = Counter()
good = []
for chain in saved["chains"]:
    try:
        node = base + int(chain["main_offset"], 16)
        for offset in chain["offsets"]:
            node = u64(node) + int(offset, 16)
        vtable = u64(node)
        pos = struct.unpack("<3f", bytes.fromhex(call(op="mem", address=hex(node + 0x31C), size=12)["hex"]))
    except Exception:
        continue
    landing[hex(node)] += 1
    if vtable == base + ACTOR_VTABLE_OFFSET and all(abs(v) < 1e5 for v in pos):
        good.append((chain, hex(node), pos))
print("landings:", landing.most_common(5))
print(len(good), "chains reach an actor-like object")
for chain, node, pos in good[:10]:
    print(chain, node, [round(v, 3) for v in pos])
json.dump([c for c, _, _ in good], open("C:/Users/gui62/giuray-work/mp/chains_valid.json", "w"), indent=1)
