"""Stands in for another player: a Link walking in a circle around the local one (radius 3, one
lap every 8 s), facing the way it walks; with "stops", it stands still for 4 s after every lap;
with "mirror", it copies the local Link 2 units to his side (to compare their runs).

Without a port it is the host on 127.0.0.1:7778 (Giuray joins with GIURAY_NX2_MP_PEER=127.0.0.1:7778).
With a port it joins Giuray (hosting on 7777) from that port, so several can run at once:
python fake_peer.py [radius] [name] [circle|stops|mirror] [port]"""
import math
import socket
import struct
import sys
import time

# C layout: the player id and 3 bytes after it, 3 bytes of padding before the camera.
PACKET = struct.Struct("<IIB3x3f2f16s32sB312s3x2f")
MAGIC = 0x32544F4F  # "OOT2"
radius = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
name = (sys.argv[2] if len(sys.argv) > 2 else "Teste").encode()[:15]
stops = len(sys.argv) > 3 and sys.argv[3] == "stops"
mirror = len(sys.argv) > 3 and sys.argv[3] == "mirror"
port = int(sys.argv[4]) if len(sys.argv) > 4 else 7778
own_id = 0xFF if len(sys.argv) > 4 else 0  # joining: Giuray (the host) gives the id
local_facing = (0.0, 1.0)
local_action = b""
local_input = (0, b"", 0.0, 1.0)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", port))
sock.settimeout(0.05)
giuray = ("127.0.0.1", 7777)
center = None
last = None
sequence = 0
received = 0
start = time.time()
last_report = start
print(f"fake peer on 127.0.0.1:{port}, answering 127.0.0.1:7777", flush=True)
while True:
    try:
        data, _ = sock.recvfrom(2048)
        if len(data) == PACKET.size:
            magic, _, _, x, y, z, fx, fz, local_name, action, *local_pad = PACKET.unpack(data)
            if magic == MAGIC:
                received += 1
                center = (x, y, z)
                local_facing = (fx, fz)
                local_action = action
                local_input = tuple(local_pad)
    except (socket.timeout, ConnectionResetError):
        # Windows reports a closed Giuray (port unreachable) as a reset on the next receive.
        pass
    now = time.time()
    if center and mirror:
        fx, fz = local_facing
        sequence += 1
        sock.sendto(PACKET.pack(MAGIC, sequence, own_id, center[0] + 2 * fz, center[1], center[2] - 2 * fx,
                                fx, fz, name, local_action, *local_input), giuray)
    elif center:
        walking_time = now - start
        if stops:
            lap, into = divmod(walking_time, 12)
            walking_time = lap * 8 + min(into, 8)
        angle = walking_time * 2 * math.pi / 8
        if last and stops and (now - start) % 12 > 8:
            px, pz, facing = last  # standing where the lap ended
        else:
            px = center[0] + radius * math.cos(angle)
            pz = center[2] + radius * math.sin(angle)
            # Walking counterclockwise: the tangent is the facing direction.
            facing = (-math.sin(angle), math.cos(angle))
            last = (px, pz, facing)
        sequence += 1
        sock.sendto(PACKET.pack(MAGIC, sequence, own_id, px, center[1], pz, facing[0], facing[1], name, b"", 0, b"", 0.0, 1.0), giuray)
    if now - last_report > 5:
        print(f"received {received} packets from Giuray, center {center}", flush=True)
        last_report = now
    time.sleep(0.033)
