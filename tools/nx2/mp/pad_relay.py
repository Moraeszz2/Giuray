"""Copies the local player controller's pad state into the fake controller the copy Link reads
(the live test before the Giuray build does it), as fast as the debug server answers.
python pad_relay.py <seconds>  (reads ctrl.json and fake.json next to rsp.py)"""
import json
import socket
import sys
import time

MP = "C:/Users/gui62/giuray-work/mp/"
ctrl = json.load(open(MP + "ctrl.json"))["ctrl"]
fake = json.load(open(MP + "fake.json"))["fake"]
sock = socket.create_connection(("127.0.0.1", 6545), timeout=5)
buffer = b""
next_id = 1


def call(method, params):
    global buffer, next_id
    sock.sendall(json.dumps({"id": next_id, "method": method, "params": params}).encode() + b"\n")
    next_id += 1
    while b"\n" not in buffer:
        buffer += sock.recv(1 << 20)
    raw, _, buffer = buffer.partition(b"\n")
    return json.loads(raw).get("result", {})


end = time.time() + float(sys.argv[1] if len(sys.argv) > 1 else 60)
count = 0
start = time.time()
while time.time() < end:
    state = call("read_memory", {"address": hex(ctrl + 8), "size": 0x138}).get("hex")
    if state:
        call("write_memory", {"address": hex(fake + 8), "hex": state})
        count += 1
print(f"{count} copies, {count / (time.time() - start):.0f} per second")
