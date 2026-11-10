"""Stands in for the second player: receives the local Lichii's packets on 127.0.0.1:7778 and sends
back a "P2" walking in a circle around the local Link (radius 3, one lap every 8 s), facing the
way it walks. Lichii runs with EDEN_NX2_MP_PEER=127.0.0.1:7778 and listens on 7777.
python fake_peer.py [radius] [name]"""
import math
import socket
import struct
import sys
import time

PACKET = struct.Struct("<II3f2f16s")
MAGIC = 0x4D544F4F
radius = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
name = (sys.argv[2] if len(sys.argv) > 2 else "Teste").encode()[:15]

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", 7778))
sock.settimeout(0.05)
lichii = ("127.0.0.1", 7777)
center = None
sequence = 0
received = 0
start = time.time()
last_report = start
print("fake peer on 127.0.0.1:7778, answering 127.0.0.1:7777", flush=True)
while True:
    try:
        data, _ = sock.recvfrom(256)
        if len(data) == PACKET.size:
            magic, _, x, y, z, fx, fz, local_name = PACKET.unpack(data)
            if magic == MAGIC:
                received += 1
                center = (x, y, z)
    except socket.timeout:
        pass
    now = time.time()
    if center:
        angle = (now - start) * 2 * math.pi / 8
        px = center[0] + radius * math.cos(angle)
        pz = center[2] + radius * math.sin(angle)
        # Walking counterclockwise: the tangent is the facing direction.
        facing = (-math.sin(angle), math.cos(angle))
        sequence += 1
        sock.sendto(PACKET.pack(MAGIC, sequence, px, center[1], pz, facing[0], facing[1], name), lichii)
    if now - last_report > 5:
        print(f"received {received} packets from Lichii, center {center}", flush=True)
        last_report = now
    time.sleep(0.033)
