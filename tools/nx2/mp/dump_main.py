"""Dumps the main module's code (and read-only data) to a file for offline disassembly; the dump
stays on this PC. python dump_main.py <out.bin>"""
import json
import re
import subprocess
import sys

sys.path.insert(0, "C:/Users/gui62/lichii-perf/mp")
from rsp import call  # noqa: E402

PY = "B:/msys64/ucrt64/bin/python.exe"
TOOL = "B:/projetos/Lichii/tools/nx2/debug_mcp.py"

text = bytes.fromhex(call(op="raw", packet="qRcmd," + b"get mappings".hex())["reply"]).decode("latin-1")
code = [(int(m.group(1), 16), int(m.group(2), 16) + 1)
        for m in re.finditer(r"0x([0-9a-f]+) - 0x([0-9a-f]+) r-x Code", text)]
ro = [(int(m.group(1), 16), int(m.group(2), 16) + 1)
      for m in re.finditer(r"0x([0-9a-f]+) - 0x([0-9a-f]+) r-- Code", text)]
start = code[1][0]
end = next(e for s, e in ro if s == code[1][1])  # the main module's .rodata follows its code
print(f"main module {start:#x} - {end:#x} ({(end - start) >> 20} MiB)", flush=True)
with open(sys.argv[1], "wb") as out:
    for address in range(start, end, 0x10000):
        size = min(0x10000, end - address)
        args = [PY, "-I", TOOL, "--server", "127.0.0.1:6545", "--call",
                json.dumps({"method": "read_memory", "params": {"address": hex(address), "size": size}})]
        result = json.loads(subprocess.run(args, capture_output=True, text=True, timeout=120).stdout)
        out.write(bytes.fromhex(result["hex"]))
json.dump({"base": hex(start), "end": hex(end)}, open(sys.argv[1] + ".json", "w"))
print("done")
