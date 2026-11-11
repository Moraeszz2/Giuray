"""Keeps one GDB remote-protocol connection to Lichii's GDB stub (127.0.0.1:6543) and serves
commands on 127.0.0.1:6560, one JSON object per line (see rsp.py). Run it once per session."""
import json
import queue
import socket
import socketserver
import threading
import time

STUB = ("127.0.0.1", 6543)
SERVE = ("127.0.0.1", 6560)


def checksum(data: bytes) -> bytes:
    return b"%02x" % (sum(data) & 0xFF)


class Stub:
    def __init__(self):
        # Wait (up to 15 min) for Lichii to boot a game and open the stub.
        for _ in range(900):
            try:
                self.sock = socket.create_connection(STUB)
                break
            except OSError:
                time.sleep(1)
        else:
            raise SystemExit("the GDB stub never listened")
        self.replies = queue.Queue()
        self.stops = queue.Queue()
        self.lock = threading.Lock()
        self.no_ack = False
        self.buffer = b""
        threading.Thread(target=self._reader, daemon=True).start()
        self.request("QStartNoAckMode")
        self.no_ack = True

    def _reader(self):
        while True:
            data = self.sock.recv(65536)
            if not data:
                self.replies.put(None)
                return
            self.buffer += data
            while True:
                self.buffer = self.buffer.lstrip(b"+-")
                start = self.buffer.find(b"$")
                end = self.buffer.find(b"#", start + 1)
                if start < 0 or end < 0 or len(self.buffer) < end + 3:
                    break
                payload = self.buffer[start + 1:end]
                self.buffer = self.buffer[end + 3:]
                if not self.no_ack:
                    self.sock.sendall(b"+")
                text = payload.decode("latin-1")
                if text[:1] in ("T", "S", "W", "X"):
                    self.stops.put(text)
                else:
                    self.replies.put(text)

    def send(self, packet: str):
        data = packet.encode("latin-1")
        self.sock.sendall(b"$" + data + b"#" + checksum(data))

    def request(self, packet: str, timeout=10.0):
        with self.lock:
            while not self.replies.empty():
                self.replies.get_nowait()
            self.send(packet)
            return self.replies.get(timeout=timeout)

    def stop_reply(self, timeout):
        try:
            return self.stops.get(timeout=timeout)
        except queue.Empty:
            return None


# The process starts suspended for the debugger; let it run. Connected before the game's process
# exists, the stub reports a thread with all registers zero, or stops answering: wait for a real
# thread, and connect again when a connection gets no answer for a while.
first = None
while True:
    stub = Stub()
    for _ in range(5):
        stub.send("?")
        first = stub.stop_reply(3)
        if first and "20:0000000000000000" not in first:
            break
        time.sleep(1)
    if first and "20:0000000000000000" not in first:
        break
    stub.sock.close()
    time.sleep(2)
stub.send("c")
print("connected; initial status", first, flush=True)


def regs():
    raw = bytes.fromhex(stub.request("g"))
    values = [int.from_bytes(raw[i * 8:i * 8 + 8], "little") for i in range(33)]
    result = {f"x{i}": hex(values[i]) for i in range(31)}
    result["sp"] = hex(values[31])
    result["pc"] = hex(values[32])
    return result


def handle(cmd):
    op = cmd["op"]
    if op == "raw":
        return {"reply": stub.request(cmd["packet"])}
    if op == "interrupt":
        stub.sock.sendall(b"\x03")
        return {"stop": stub.stop_reply(cmd.get("timeout", 10))}
    if op == "cont":
        stub.send("c")
        return {"ok": True}
    if op == "wait":
        return {"stop": stub.stop_reply(cmd.get("timeout", 10))}
    if op == "regs":
        return regs()
    if op == "write":
        data = cmd["hex"]
        return {"reply": stub.request(f"M{int(cmd['address'], 16):x},{len(data) // 2:x}:{data}")}
    if op == "mem":
        return {"hex": stub.request(f"m{int(cmd['address'], 16):x},{cmd['size']:x}")}
    if op in ("watch", "unwatch"):
        kind = {"write": 2, "read": 3, "access": 4}[cmd.get("kind", "write")]
        letter = "Z" if op == "watch" else "z"
        return {"reply": stub.request(f"{letter}{kind},{int(cmd['address'], 16):x},{cmd.get('size', 4):x}")}
    raise ValueError(f"unknown op {op}")


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        for line in self.rfile:
            try:
                result = handle(json.loads(line))
            except Exception as error:
                result = {"error": repr(error)}
            self.wfile.write((json.dumps(result) + "\n").encode())
            self.wfile.flush()


# No address reuse: on Windows it lets a second daemon bind the same port and steal requests.
socketserver.ThreadingTCPServer.allow_reuse_address = False
with socketserver.ThreadingTCPServer(SERVE, Handler) as server:
    print("serving on", SERVE, flush=True)
    server.serve_forever()
