"""Snapshots the GameDataMgr's value tables ([main + 0x4E842A8], tables of 0x60 bytes from + 0x8:
count at + 0x8, entry array pointer at + 0x10, hash array pointer at + 0x40) and, run again,
prints which entries changed: table, index, hash and old -> new bytes.
python gamedata_diff.py <main base> snap|diff"""
import json
import pickle
import socket
import struct
import sys

SNAP = "C:/Users/gui62/lichii-perf/mp/gamedata_snap.pkl"
sock = socket.create_connection(("127.0.0.1", 6545), timeout=30)
buffer = b""
next_id = 1


def call(method, params):
    global buffer, next_id
    sock.sendall(json.dumps({"id": next_id, "method": method, "params": params}).encode() + b"\n")
    next_id += 1
    while b"\n" not in buffer:
        buffer += sock.recv(1 << 20)
    raw, _, buffer = buffer.partition(b"\n")
    reply = json.loads(raw)
    return reply.get("result", reply)


def read(address, size):
    out = b""
    while size > 0:
        chunk = min(size, 0x10000)
        text = call("read_memory", {"address": hex(address), "size": chunk}).get("hex", "")
        if len(text) != 2 * chunk:
            return None
        out += bytes.fromhex(text)
        address += chunk
        size -= chunk
    return out


def u64(address):
    return struct.unpack("<Q", read(address, 8))[0]


base = int(sys.argv[1], 16)
mgr = u64(base + 0x4E842A8)
tables = {}
for t in range(0x8, 0x900, 0x60):
    head = read(mgr + t, 0x60)
    if not head:
        break
    count = struct.unpack_from("<I", head, 0x8)[0]
    values = struct.unpack_from("<Q", head, 0x10)[0]  # the entry objects (value inside)
    hashes = struct.unpack_from("<Q", head, 0x40)[0]
    if not (0 < count < 0x100000) or not (0x2100000000 <= values < 0x2600000000):
        continue
    size = min(count * 0x30, 0x100000)
    tables[t] = (count, values, hashes, read(values, size), read(hashes, count * 4))
if sys.argv[2] == "snap":
    pickle.dump(tables, open(SNAP, "wb"))
    print("snapshot of", {hex(t): v[0] for t, v in tables.items()})
else:
    old = pickle.load(open(SNAP, "rb"))
    for t, (count, values, hashes, data, hash_data) in tables.items():
        if t not in old or not data or not old[t][3]:
            continue
        before = old[t][3]
        # Entries are objects of one size: the distance between the first two vtable pointers.
        first = data[:8]
        stride = next((k for k in range(8, 0x100, 8) if data[k:k + 8] == first), 0x18)
        end = min(len(data), len(before), count * stride)
        changed = sorted({i // stride for i in range(end) if data[i] != before[i]})
        for index in changed:
            h = struct.unpack_from("<I", hash_data, index * 4)[0] if hash_data else 0
            a = before[index * stride:(index + 1) * stride].hex()
            b = data[index * stride:(index + 1) * stride].hex()
            print(f"table {t:#x} stride {stride:#x} index {index} hash {h:#010x}")
            print(f"  {a}")
            print(f"  {b}")
