"""Finds the code that references a string (adrp + add/ldr pairs) in the main module dump.
python xref.py <text> [more texts...]"""
import struct
import sys

DUMP = "C:/Users/gui62/lichii-perf/mp/private/main.bin"
data = open(DUMP, "rb").read()
code_end = 0x3C47000  # .text size; .rodata follows


def find_all(needle):
    start = 0
    while True:
        index = data.find(needle, start)
        if index < 0:
            return
        yield index
        start = index + 1


targets = {}
for text in sys.argv[1:]:
    needle = text.encode() + b"\0"
    for offset in find_all(needle):
        # Only whole strings (preceded by a NUL).
        if offset == 0 or data[offset - 1] == 0:
            targets[offset] = text
print({hex(k): v for k, v in targets.items()})
pages = {}
for target in targets:
    pages.setdefault(target & ~0xFFF, []).append(target)
for pc in range(0, code_end, 4):
    word = struct.unpack_from("<I", data, pc)[0]
    if word & 0x9F000000 != 0x90000000:  # adrp
        continue
    rd = word & 0x1F
    imm = ((word >> 29) & 3) | (((word >> 5) & 0x7FFFF) << 2)
    if imm & (1 << 20):
        imm -= 1 << 21
    page = (pc & ~0xFFF) + (imm << 12)
    if page not in pages:
        continue
    for next_pc in range(pc + 4, min(pc + 24, code_end), 4):
        nxt = struct.unpack_from("<I", data, next_pc)[0]
        if nxt & 0xFF800000 == 0x91000000 and (nxt >> 5) & 0x1F == rd:  # add rX, rd, #imm
            low = (nxt >> 10) & 0xFFF
            if page + low in targets:
                print(f"+{pc:#x}: {targets[page + low]!r}")
            break
