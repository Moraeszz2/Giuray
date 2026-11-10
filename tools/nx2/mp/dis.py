"""Disassembles guest code through the RSP daemon: python dis.py <address> [bytes before] [bytes after]."""
import sys

import capstone

sys.path.insert(0, "C:/Users/gui62/lichii-perf/mp")
from rsp import call  # noqa: E402

pc = int(sys.argv[1], 16)
before = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x80
after = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x40
start = pc - before
code = b""
for offset in range(0, before + after, 0x400):
    size = min(0x400, before + after - offset)
    code += bytes.fromhex(call(op="mem", address=hex(start + offset), size=size)["hex"])
md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
for insn in md.disasm(code, start):
    mark = "=>" if insn.address == pc else "  "
    print(f"{mark} {insn.address:#x}: {insn.mnemonic} {insn.op_str}")
