"""Looks for the field that makes the second Navi visible: for every small field where the scene's
Navi and the second one differ (the actor and its components), copies the scene Navi's value into
the second one for a moment and compares a screenshot with a reference one. Prints the fields
whose copy changed the picture the most. Needs the debug server; Link and the camera must stay
still. python find_visible.py <main Navi> <second Navi>"""
import base64
import json
import socket
import struct
import sys
import time
import zlib

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
    text = call("read_memory", {"address": hex(address), "size": size}).get("hex", "")
    return bytes.fromhex(text) if len(text) == 2 * size else None


def pixels(data):
    """Decodes an 8-bit RGB or RGBA PNG into rows of bytes (filters applied)."""
    pos = 8
    idat = b""
    width = height = channels = 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", chunk[:10])
            channels = 4 if color == 6 else 3
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * channels
    rows = []
    previous = bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = previous[x]
            c = previous[x - channels] if x >= channels else 0
            if kind == 1:
                line[x] = (line[x] + a) & 255
            elif kind == 2:
                line[x] = (line[x] + b) & 255
            elif kind == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        previous = line
    return rows, channels


def screenshot():
    result = call("screenshot", {"height": 144})
    return pixels(base64.b64decode(result["png_base64"]))


def difference(a, b):
    rows_a, ch = a
    rows_b, _ = b
    changed = 0
    for ra, rb in zip(rows_a[::4], rows_b[::4]):
        for x in range(0, min(len(ra), len(rb)), ch * 4):
            if abs(ra[x] - rb[x]) + abs(ra[x + 1] - rb[x + 1]) + abs(ra[x + 2] - rb[x + 2]) > 60:
                changed += 1
    return changed


main = int(sys.argv[1], 16)
second = int(sys.argv[2], 16)
# Candidate places: the actors, and the components their table (actor + 0x270) points at.
places = [(main, second, 0x800)]
table_a = struct.unpack("<Q", read(main + 0x270, 8))[0]
table_b = struct.unpack("<Q", read(second + 0x270, 8))[0]
ta, tb = read(table_a, 0x400), read(table_b, 0x400)
for off in range(0, 0x400, 8):
    pa = struct.unpack_from("<Q", ta, off)[0]
    pb = struct.unpack_from("<Q", tb, off)[0]
    if 0x2100000000 <= pa < 0x2600000000 and 0x2100000000 <= pb < 0x2600000000:
        places.append((pa, pb, 0x200))
candidates = []
seen = set()
for pa, pb, size in places:
    a, b = read(pa, size), read(pb, size)
    if not a or not b:
        continue
    for i in range(8, size, 4):
        x = struct.unpack_from("<I", a, i)[0]
        y = struct.unpack_from("<I", b, i)[0]
        if x != y and x < 0x100000 and y < 0x100000 and (pb + i) not in seen:
            seen.add(pb + i)
            candidates.append((pb + i, x, y))
print(len(candidates), "candidates", flush=True)
reference = screenshot()
results = []
for address, value, original in candidates:
    call("write_memory", {"address": hex(address), "hex": struct.pack("<I", value).hex()})
    time.sleep(0.25)
    after = screenshot()
    call("write_memory", {"address": hex(address), "hex": struct.pack("<I", original).hex()})
    change = difference(reference, after)
    results.append((change, hex(address), hex(value), hex(original)))
    if change > 20:
        print("change", change, hex(address), hex(original), "->", hex(value), flush=True)
results.sort(reverse=True)
print("top", results[:10])
