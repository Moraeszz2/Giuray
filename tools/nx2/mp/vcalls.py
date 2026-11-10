"""Lists virtual calls through a vtable slot (ldr xN, [xM, #slot] then blr xN) in the dump.
python vcalls.py <slot offset>"""
import struct
import sys

DUMP = "C:/Users/gui62/lichii-perf/mp/private/main.bin"
data = open(DUMP, "rb").read()
slot = int(sys.argv[1], 16)
code_end = 0x3C47000
hits = []
for pc in range(0, code_end, 4):
    word = struct.unpack_from("<I", data, pc)[0]
    # LDR (immediate, unsigned offset), 64-bit: 0xF9400000 | imm12 << 10 | Rn << 5 | Rt
    if word & 0xFFC00000 != 0xF9400000 or ((word >> 10) & 0xFFF) * 8 != slot:
        continue
    rt = word & 0x1F
    for next_pc in range(pc + 4, min(pc + 20, code_end), 4):
        nxt = struct.unpack_from("<I", data, next_pc)[0]
        if nxt & 0xFFFFFC1F == 0xD63F0000 and (nxt >> 5) & 0x1F == rt:  # blr rt
            hits.append(next_pc)
            break
        if nxt & 0xFFFFFC1F == 0xD63F0800 and (nxt >> 5) & 0x1F == rt:  # blraa-ish forms
            hits.append(next_pc)
            break
print(len(hits), "virtual calls through slot", hex(slot))
for h in hits[:400]:
    print(f"+{h:#x}")
