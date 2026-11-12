"""Offline disassembly of the main module dump (offsets from the module start).
python dis_off.py <offset> [before] [after]           around an offset
python dis_off.py --func <offset>                     from the function prologue before it
python dis_off.py --callers <offset>                  bl instructions that call that offset"""
import struct
import sys

import capstone

DUMP = "C:/Users/gui62/giuray-work/mp/private/main.bin"
code = open(DUMP, "rb").read()
md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)


def show(start, end, mark=None):
    for insn in md.disasm(code[start:end], start):
        print(f"{'=>' if insn.address == mark else '  '} +{insn.address:#08x}: {insn.mnemonic} {insn.op_str}")


def is_prologue(offset):
    word = struct.unpack_from("<I", code, offset)[0]
    # stp x29, x30, [sp, #-N]! (pre-index) or sub sp, sp, #N
    return (word & 0xFFC07FFF) == 0xA9807BFD or (word & 0xFF8003FF) == 0xD10003FF


if sys.argv[1] == "--func":
    target = int(sys.argv[2], 16)
    start = target
    while start > 0 and not is_prologue(start):
        start -= 4
    # A function often saves more registers before x29/x30: step back over stp/sub.
    print(f"function starts near +{start:#x}")
    show(start, target + 0x20, target)
elif sys.argv[1] == "--callers":
    target = int(sys.argv[2], 16)
    for offset in range(0, len(code) - 4, 4):
        word = struct.unpack_from("<I", code, offset)[0]
        if word >> 26 == 0x25:  # bl
            imm = word & 0x3FFFFFF
            if imm & 0x2000000:
                imm -= 0x4000000
            if offset + imm * 4 == target:
                print(f"+{offset:#x}")
else:
    offset = int(sys.argv[1], 16)
    before = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x80
    after = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x40
    show(offset - before, offset + after, offset)
