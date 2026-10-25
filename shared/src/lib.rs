//! Wire protocol between the launcher's tunnel agent and the relay server. One UDP datagram per packet.
//! Layout: tag byte, then per-variant fields. Hand-rolled because the payload is opaque game traffic.
//!
//! Session flow: [Register -> Challenge -> Register -> Issued] (first run only) -> Hello -> Welcome,
//! then every client packet is wrapped in `Signed` with the token from Welcome.

use sha2::{Digest, Sha256};

pub const MAX_PAYLOAD: usize = 1400;
pub const MAX_SERIAL: usize = 64;
pub const MAX_ROOM: usize = 32;

pub type Vip = [u8; 4];
pub type Cookie = [u8; 16];
pub type Verifier = [u8; 32];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Reason {
    UnknownSerial = 1,
    Revoked = 2,
    InUse = 3,
    Full = 4,
    NotAuthed = 5,
    /// Server only accepts admin-issued serials.
    RegistrationClosed = 6,
    /// Too many attempts from this address.
    RateLimited = 7,
    WrongPassword = 8,
    RoomFull = 9,
    BadRoom = 10,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Packet {
    /// Client -> server. Joins `room` (created on first join, removed when empty). `verifier` proves the
    /// room password without sending it (see `room_verifier`).
    Hello { serial: String, room: String, verifier: Verifier },
    /// Server -> client: accepted. `token` must wrap every later client packet.
    Welcome { vip: Vip, token: u64 },
    Reject(Reason),
    /// LAN discovery broadcast. Client -> server: `vip` ignored. Server -> client: sender's vip.
    /// Ports are the engine's logical UDP ports (8086 lobby, 8088 game), so one tunnel serves both sockets.
    Bcast { vip: Vip, src_port: u16, dst_port: u16, payload: Vec<u8> },
    /// Game traffic. Client -> server: `vip` is the destination. Server -> client: the source.
    Data { vip: Vip, src_port: u16, dst_port: u16, payload: Vec<u8> },
    /// Keepalive, echoed by the server.
    Ping,
    /// Engine -> local agent only: "I own this logical port, send its traffic to me".
    Bind { port: u16 },
    /// Client -> server: sign-up. An all-zero cookie asks for a Challenge; echoing the cookie asks for a serial.
    Register { cookie: Cookie },
    /// Server -> client: prove you can receive at your source address (stops spoofed sign-ups).
    Challenge { cookie: Cookie },
    /// Server -> client: the serial issued for a Register. The client must store it.
    Issued { serial: String },
    /// Client -> server: leave the room now (otherwise the session times out).
    Leave,
    /// Client -> server wrapper for in-session packets (Bcast, Data, Ping, Leave).
    Signed { token: u64, inner: Box<Packet> },
}

fn data(tag: u8, vip: &Vip, src: u16, dst: u16, payload: &[u8]) -> Vec<u8> {
    [&[tag][..], vip, &src.to_be_bytes(), &dst.to_be_bytes(), payload].concat()
}

/// Length-prefixed string; `None` if too long or malformed.
fn take_str(b: &[u8], max: usize) -> Option<(String, &[u8])> {
    let (&n, rest) = b.split_first()?;
    let n = n as usize;
    if n > max || rest.len() < n {
        return None;
    }
    Some((String::from_utf8(rest[..n].to_vec()).ok()?, &rest[n..]))
}

impl Packet {
    pub fn encode(&self) -> Vec<u8> {
        match self {
            Packet::Hello { serial, room, verifier } => {
                [&[1, serial.len() as u8][..], serial.as_bytes(), &[room.len() as u8], room.as_bytes(), verifier].concat()
            }
            Packet::Welcome { vip, token } => [&[2][..], vip, &token.to_be_bytes()].concat(),
            Packet::Reject(r) => vec![3, *r as u8],
            Packet::Bcast { vip, src_port, dst_port, payload } => data(4, vip, *src_port, *dst_port, payload),
            Packet::Data { vip, src_port, dst_port, payload } => data(5, vip, *src_port, *dst_port, payload),
            Packet::Ping => vec![6],
            Packet::Bind { port } => [&[7][..], &port.to_be_bytes()].concat(),
            Packet::Register { cookie } => [&[8][..], cookie].concat(),
            Packet::Issued { serial } => [&[9, serial.len() as u8][..], serial.as_bytes()].concat(),
            Packet::Leave => vec![10],
            Packet::Signed { token, inner } => [&[11][..], &token.to_be_bytes(), &inner.encode()].concat(),
            Packet::Challenge { cookie } => [&[12][..], cookie].concat(),
        }
    }

    pub fn decode(b: &[u8]) -> Option<Packet> {
        Self::decode_inner(b, true)
    }

    fn decode_inner(b: &[u8], allow_signed: bool) -> Option<Packet> {
        let (&tag, rest) = b.split_first()?;
        let vip = |r: &[u8]| -> Option<(Vip, u16, u16, Vec<u8>)> {
            let v: Vip = r.get(..4)?.try_into().ok()?;
            let src = u16::from_be_bytes(r.get(4..6)?.try_into().ok()?);
            let dst = u16::from_be_bytes(r.get(6..8)?.try_into().ok()?);
            let p = r[8..].to_vec();
            (p.len() <= MAX_PAYLOAD).then_some((v, src, dst, p))
        };
        Some(match tag {
            1 => {
                let (serial, r) = take_str(rest, MAX_SERIAL)?;
                let (room, r) = take_str(r, MAX_ROOM)?;
                Packet::Hello { serial, room, verifier: r.try_into().ok()? }
            }
            2 if rest.len() == 12 => Packet::Welcome { vip: rest[..4].try_into().ok()?, token: u64::from_be_bytes(rest[4..].try_into().ok()?) },
            3 => Packet::Reject(match *rest.first()? {
                1 => Reason::UnknownSerial,
                2 => Reason::Revoked,
                3 => Reason::InUse,
                4 => Reason::Full,
                5 => Reason::NotAuthed,
                6 => Reason::RegistrationClosed,
                7 => Reason::RateLimited,
                8 => Reason::WrongPassword,
                9 => Reason::RoomFull,
                10 => Reason::BadRoom,
                _ => return None,
            }),
            4 => vip(rest).map(|(vip, src_port, dst_port, payload)| Packet::Bcast { vip, src_port, dst_port, payload })?,
            5 => vip(rest).map(|(vip, src_port, dst_port, payload)| Packet::Data { vip, src_port, dst_port, payload })?,
            6 if rest.is_empty() => Packet::Ping,
            7 => Packet::Bind { port: u16::from_be_bytes(rest.try_into().ok()?) },
            8 => Packet::Register { cookie: rest.try_into().ok()? },
            9 => {
                let (serial, r) = take_str(rest, MAX_SERIAL)?;
                r.is_empty().then_some(Packet::Issued { serial })?
            }
            10 if rest.is_empty() => Packet::Leave,
            11 if allow_signed && rest.len() > 8 => {
                let token = u64::from_be_bytes(rest[..8].try_into().ok()?);
                Packet::Signed { token, inner: Box::new(Self::decode_inner(&rest[8..], false)?) }
            }
            12 => Packet::Challenge { cookie: rest.try_into().ok()? },
            _ => return None,
        })
    }
}

pub fn sha256(parts: &[&[u8]]) -> [u8; 32] {
    let mut h = Sha256::new();
    for p in parts {
        h.update(p);
    }
    h.finalize().into()
}

/// Proof of knowing a room's password without sending it. Iterated so an on-path eavesdropper who captures
/// it cannot cheaply brute-force a weak password offline. (It is still replayable: the wire is not encrypted.)
pub fn room_verifier(room: &str, password: &str) -> Verifier {
    let mut v = sha256(&[b"rallypoint/room/v1\0", room.as_bytes(), b"\0", password.as_bytes()]);
    for _ in 0..50_000 {
        v = sha256(&[&v, room.as_bytes()]);
    }
    v
}

pub fn ct_eq(a: &[u8], b: &[u8]) -> bool {
    a.len() == b.len() && a.iter().zip(b).fold(0u8, |d, (x, y)| d | (x ^ y)) == 0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip() {
        for p in [
            Packet::Hello { serial: "ABCD-1234".into(), room: "my room".into(), verifier: [7; 32] },
            Packet::Welcome { vip: [10, 77, 0, 3], token: 0xDEADBEEF_12345678 },
            Packet::Reject(Reason::WrongPassword),
            Packet::Bcast { vip: [0; 4], src_port: 8086, dst_port: 8086, payload: vec![1, 2, 3] },
            Packet::Data { vip: [10, 77, 0, 9], src_port: 8088, dst_port: 8088, payload: vec![] },
            Packet::Bind { port: 8088 },
            Packet::Register { cookie: [0; 16] },
            Packet::Challenge { cookie: [9; 16] },
            Packet::Issued { serial: "AAAA-BBBB-CCCC-DDDD".into() },
            Packet::Ping,
            Packet::Leave,
            Packet::Signed { token: 42, inner: Box::new(Packet::Data { vip: [1, 2, 3, 4], src_port: 1, dst_port: 2, payload: vec![5] }) },
            Packet::Signed { token: 42, inner: Box::new(Packet::Ping) },
        ] {
            assert_eq!(Packet::decode(&p.encode()), Some(p));
        }
    }

    /// Golden bytes; the engine's RelayWire.h test checks the same layout.
    #[test]
    fn wire_layout() {
        let p = Packet::Data { vip: [10, 77, 0, 1], src_port: 8088, dst_port: 8086, payload: vec![0xAA] };
        assert_eq!(p.encode(), [5, 10, 77, 0, 1, 0x1F, 0x98, 0x1F, 0x96, 0xAA]);
        assert_eq!(Packet::Bind { port: 8086 }.encode(), [7, 0x1F, 0x96]);
    }

    #[test]
    fn rejects_garbage() {
        assert_eq!(Packet::decode(&[]), None);
        assert_eq!(Packet::decode(&[99]), None);
        assert_eq!(Packet::decode(&[3, 42]), None);
        assert_eq!(Packet::decode(&[4, 1, 2]), None); // short header
        assert_eq!(Packet::decode(&[&[5, 0, 0, 0, 0, 0, 0, 0, 0][..], &vec![0; MAX_PAYLOAD + 1]].concat()), None);
        // Hello: truncated verifier, oversized fields
        let mut h = Packet::Hello { serial: "a".into(), room: "r".into(), verifier: [0; 32] }.encode();
        h.pop();
        assert_eq!(Packet::decode(&h), None);
        assert_eq!(Packet::decode(&[1, 200]), None);
        // Signed must not nest, and must wrap something
        let inner = Packet::Signed { token: 1, inner: Box::new(Packet::Ping) }.encode();
        assert_eq!(Packet::decode(&[&[11, 0, 0, 0, 0, 0, 0, 0, 2][..], &inner].concat()), None);
        assert_eq!(Packet::decode(&[11, 0, 0, 0, 0, 0, 0, 0, 2]), None);
        // trailing junk where none is allowed
        assert_eq!(Packet::decode(&[8; 3]), None);
        assert_eq!(Packet::decode(&[6, 0]), None);
    }

    #[test]
    fn verifier_depends_on_room_and_password() {
        let a = room_verifier("r", "pw");
        assert_eq!(a, room_verifier("r", "pw"));
        assert_ne!(a, room_verifier("r", "pw2"));
        assert_ne!(a, room_verifier("r2", "pw"));
        assert!(ct_eq(&a, &a) && !ct_eq(&a, &[0; 32]) && !ct_eq(&a, &a[..31]));
    }
}
