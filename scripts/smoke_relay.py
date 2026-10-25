"""Handshake against a running relay: smoke_relay.py HOST PORT SERIAL. Exits non-zero unless Welcome comes back."""
import socket, sys

host, port, serial = sys.argv[1], int(sys.argv[2]), sys.argv[3]
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(5)
s.sendto(bytes([1, len(serial)]) + serial.encode(), (host, port))
reply = s.recv(100)
assert reply[0] == 2 and len(reply) == 5, f"expected Welcome, got {list(reply)}"
print("welcome, vip", ".".join(map(str, reply[1:])))
