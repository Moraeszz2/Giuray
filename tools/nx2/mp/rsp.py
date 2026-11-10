"""Client for rsp_daemon.py: python rsp.py '{"op":"regs"}' (several JSON arguments run in order)."""
import json
import socket
import sys


def call(**cmd):
    with socket.create_connection(("127.0.0.1", 6560), timeout=120) as sock:
        sock.sendall((json.dumps(cmd) + "\n").encode())
        data = b""
        while not data.endswith(b"\n"):
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
    return json.loads(data)


if __name__ == "__main__":
    for argument in sys.argv[1:]:
        print(json.dumps(call(**json.loads(argument)), indent=1))
