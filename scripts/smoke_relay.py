"""Handshake against a running relay: smoke_relay.py HOST PORT [SERIAL].
Without SERIAL it registers first (auto-issue). Prints the serial used. Exits non-zero unless Welcome comes back."""
import socket, sys

host, port = sys.argv[1], int(sys.argv[2])
serial = sys.argv[3] if len(sys.argv) > 3 else None
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(5)
if serial is None:
    s.sendto(bytes([8]), (host, port))
    r = s.recv(100)
    assert r[0] == 9, f"expected Issued, got {list(r)}"
    serial = r[2:2 + r[1]].decode()
s.sendto(bytes([1, len(serial)]) + serial.encode(), (host, port))
reply = s.recv(100)
assert reply[0] == 2 and len(reply) == 5, f"expected Welcome, got {list(reply)}"
print(serial)
