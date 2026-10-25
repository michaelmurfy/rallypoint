"""Full client handshake against a running relay: smoke_relay.py HOST PORT [ROOM [PASSWORD]].
Signs up (cookie challenge), joins the room, checks Welcome. Prints the serial. Exit 0 = success, 3 = no reply
(e.g. blocked by the country limit), 1 = anything else."""
import hashlib, socket, sys

host, port = sys.argv[1], int(sys.argv[2])
room = sys.argv[3] if len(sys.argv) > 3 else "smoke"
password = sys.argv[4] if len(sys.argv) > 4 else "pw"

def verifier(room, password):  # mirrors generals_proto::room_verifier
    sha = lambda *p: hashlib.sha256(b"".join(p)).digest()
    v = sha(b"rallypoint/room/v1\0", room.encode(), b"\0", password.encode())
    for _ in range(50_000):
        v = sha(v, room.encode())
    return v

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(3)
def ask(data):
    s.sendto(data, (host, port))
    return s.recv(200)

try:
    r = ask(bytes([8]) + bytes(16))                       # Register, empty cookie
    assert r[0] == 12, f"expected Challenge, got {list(r)}"
    r = ask(bytes([8]) + r[1:17])                         # Register with the cookie
    assert r[0] == 9, f"expected Issued, got {list(r)}"
    serial = r[2:2 + r[1]].decode()
    hello = bytes([1, len(serial)]) + serial.encode() + bytes([len(room)]) + room.encode() + verifier(room, password)
    r = ask(hello)
    assert r[0] == 2 and len(r) == 13, f"expected Welcome, got {list(r)}"
except socket.timeout:
    print("no reply"); sys.exit(3)
print(serial)
