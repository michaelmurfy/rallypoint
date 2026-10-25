//! Wire protocol between the launcher's tunnel agent and the relay server. One UDP datagram per packet.
//! Layout: tag byte, then per-variant fields. Hand-rolled because the payload is opaque game traffic.
//!
//! Two layers. `Packet` is the content: what the engine sends to the local agent, and what the agent and relay
//! say to each other. `Wire` is what actually crosses the internet: a handshake, then `Packet`s encrypted by
//! `secure` (so a `Packet` never travels in the clear between agent and relay).
//!
//! Session flow: [KeyRequest -> KeyReply] (first time with a server, to learn its key) -> Hs1 [-> Challenge -> Hs1]
//! -> Hs2 carrying Issued (first run only, `Register`) -> Hs1 carrying Hello -> Hs2 carrying Welcome -> Transport.
pub mod secure;

use sha2::{Digest, Sha256};

/// Largest game datagram the engine builds (`MAX_UDP_PAYLOAD_SIZE`). Nothing bigger is relayed.
pub const MAX_PAYLOAD: usize = 1100;
/// The only logical UDP ports the tunnel carries: the engine's lobby socket and its in-game socket.
pub const LOBBY_PORT: u16 = 8086;
pub const GAME_PORTS: [u16; 2] = [LOBBY_PORT, 8088];
pub const MAX_SERIAL: usize = 64;
pub const MAX_ROOM: usize = 32;
pub const MAX_VERSION: usize = 24;

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
    /// Agent only, never sent: the server answered with a different identity key than the one we saved.
    ServerKeyChanged = 11,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Packet {
    /// Client -> server. Joins `room` (created on first join, removed when empty). `verifier` proves the
    /// room password without sending it (see `room_verifier`).
    Hello { serial: String, room: String, verifier: Verifier },
    /// Server -> client: accepted; `vip` is this client's virtual address in the room.
    Welcome { vip: Vip },
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
    /// Client -> server: sign-up, asking for a serial.
    Register,
    /// Server -> client: the serial issued for a Register. The client must store it.
    Issued { serial: String },
    /// Client -> server: leave the room now (otherwise the session times out).
    Leave,
    /// Client -> server: ask how the relay is doing.
    StatsRequest,
    /// Server -> client: the answer to `StatsRequest`. Aggregates only; nothing about other rooms.
    Stats(ServerStats),
}

/// What a relay reports about itself to a connected client.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ServerStats {
    pub uptime_secs: u32,
    /// Rooms in use and players in all rooms (a room is a game or a lobby waiting for one).
    pub rooms: u16,
    pub players: u16,
    pub max_rooms: u16,
    pub max_players: u16,
    /// How many are in the asking client's own room.
    pub room_players: u8,
    /// Highest of room use and player use, 0-100.
    pub load_pct: u8,
    /// Packets and kilobits relayed during the last full second.
    pub pps: u32,
    pub kbps: u32,
    pub version: String,
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
            Packet::Welcome { vip } => [&[2][..], vip].concat(),
            Packet::Reject(r) => vec![3, *r as u8],
            Packet::Bcast { vip, src_port, dst_port, payload } => data(4, vip, *src_port, *dst_port, payload),
            Packet::Data { vip, src_port, dst_port, payload } => data(5, vip, *src_port, *dst_port, payload),
            Packet::Ping => vec![6],
            Packet::Bind { port } => [&[7][..], &port.to_be_bytes()].concat(),
            Packet::Register => vec![8],
            Packet::Issued { serial } => [&[9, serial.len() as u8][..], serial.as_bytes()].concat(),
            Packet::Leave => vec![10],
            Packet::StatsRequest => vec![13],
            Packet::Stats(s) => [
                &[14][..],
                &s.uptime_secs.to_be_bytes(),
                &s.rooms.to_be_bytes(),
                &s.players.to_be_bytes(),
                &s.max_rooms.to_be_bytes(),
                &s.max_players.to_be_bytes(),
                &[s.room_players, s.load_pct],
                &s.pps.to_be_bytes(),
                &s.kbps.to_be_bytes(),
                &[s.version.len().min(MAX_VERSION) as u8],
                &s.version.as_bytes()[..s.version.len().min(MAX_VERSION)],
            ]
            .concat(),
        }
    }

    pub fn decode(b: &[u8]) -> Option<Packet> {
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
            2 if rest.len() == 4 => Packet::Welcome { vip: rest.try_into().ok()? },
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
                11 => Reason::ServerKeyChanged,
                _ => return None,
            }),
            4 => vip(rest).map(|(vip, src_port, dst_port, payload)| Packet::Bcast { vip, src_port, dst_port, payload })?,
            5 => vip(rest).map(|(vip, src_port, dst_port, payload)| Packet::Data { vip, src_port, dst_port, payload })?,
            6 if rest.is_empty() => Packet::Ping,
            7 => Packet::Bind { port: u16::from_be_bytes(rest.try_into().ok()?) },
            8 if rest.is_empty() => Packet::Register,
            9 => {
                let (serial, r) = take_str(rest, MAX_SERIAL)?;
                r.is_empty().then_some(Packet::Issued { serial })?
            }
            10 if rest.is_empty() => Packet::Leave,
            13 if rest.is_empty() => Packet::StatsRequest,
            14 if rest.len() >= 23 => {
                let u32_at = |i: usize| u32::from_be_bytes(rest[i..i + 4].try_into().unwrap());
                let u16_at = |i: usize| u16::from_be_bytes(rest[i..i + 2].try_into().unwrap());
                let (version, tail) = take_str(&rest[22..], MAX_VERSION)?;
                tail.is_empty().then_some(Packet::Stats(ServerStats {
                    uptime_secs: u32_at(0),
                    rooms: u16_at(4),
                    players: u16_at(6),
                    max_rooms: u16_at(8),
                    max_players: u16_at(10),
                    room_players: rest[12],
                    load_pct: rest[13],
                    pps: u32_at(14),
                    kbps: u32_at(18),
                    version,
                }))?
            }
            _ => return None,
        })
    }
}

/// A datagram between agent and relay. Only the handshake pieces are readable on the wire (and carry nothing
/// secret); everything else is inside `Transport`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Wire {
    /// Client -> server: "what is your identity key?" Padded to the size of the reply's neighbour so a spoofed
    /// request cannot make the server send more than it received.
    KeyRequest,
    KeyReply { key: [u8; 32] },
    /// Client -> server: first handshake message (see `secure`). `cookie` proves the sender can receive at its
    /// address; all zero asks for a `Challenge`.
    Hs1 { cookie: Cookie, msg: Vec<u8> },
    /// Server -> client: the sender must prove it can receive at its address by echoing this in `Hs1`.
    Challenge { cookie: Cookie },
    /// Server -> client: second handshake message, carrying Welcome, Issued or Reject.
    Hs2 { msg: Vec<u8> },
    /// Either way, after the handshake: `counter || ciphertext`.
    Transport { data: Vec<u8> },
}

/// A `KeyRequest` is this long, so the 33-byte `KeyReply` is smaller than what asked for it.
const KEY_REQUEST_LEN: usize = 64;
const MAX_WIRE: usize = 1500;

impl Wire {
    pub fn encode(&self) -> Vec<u8> {
        match self {
            Wire::KeyRequest => {
                let mut b = vec![0u8; KEY_REQUEST_LEN];
                b[0] = 20;
                b
            }
            Wire::KeyReply { key } => [&[21][..], key].concat(),
            Wire::Hs1 { cookie, msg } => [&[22][..], cookie, msg].concat(),
            Wire::Challenge { cookie } => [&[23][..], cookie].concat(),
            Wire::Hs2 { msg } => [&[24][..], msg].concat(),
            Wire::Transport { data } => [&[25][..], data].concat(),
        }
    }

    pub fn decode(b: &[u8]) -> Option<Wire> {
        if b.len() > MAX_WIRE {
            return None;
        }
        let (&tag, rest) = b.split_first()?;
        Some(match tag {
            20 if b.len() == KEY_REQUEST_LEN => Wire::KeyRequest,
            21 => Wire::KeyReply { key: rest.try_into().ok()? },
            22 if rest.len() > 16 => Wire::Hs1 { cookie: rest[..16].try_into().ok()?, msg: rest[16..].to_vec() },
            23 => Wire::Challenge { cookie: rest.try_into().ok()? },
            24 if !rest.is_empty() => Wire::Hs2 { msg: rest.to_vec() },
            25 if !rest.is_empty() => Wire::Transport { data: rest.to_vec() },
            _ => return None,
        })
    }
}

/// Does `p` look like a datagram the game engine made? The engine obfuscates each packet (XOR with a rolling mask
/// over whole 32-bit words) and starts it with a CRC and the magic 0xF00D (`Transport.cpp`). The relay only
/// carries such packets, so the tunnel cannot be used for anything but the game.
pub fn is_game_packet(p: &[u8]) -> bool {
    const HEADER: usize = 6; // crc (4) + magic (2)
    if p.len() <= HEADER || p.len() > MAX_PAYLOAD {
        return false;
    }
    let mut plain = p.to_vec();
    let mut mask: u32 = 0xFADE;
    for w in plain.chunks_exact_mut(4) {
        let v = u32::from_be_bytes([w[0], w[1], w[2], w[3]]) ^ mask;
        w.copy_from_slice(&v.to_le_bytes());
        mask = mask.wrapping_add(0x321);
    }
    let crc = u32::from_le_bytes([plain[0], plain[1], plain[2], plain[3]]);
    let magic = u16::from_le_bytes([plain[4], plain[5]]);
    let sum = plain[4..].iter().fold(0u32, |c, &b| (c << 1).wrapping_add(b as u32).wrapping_add(c >> 31));
    magic == 0xF00D && sum == crc
}

/// A datagram the way the engine builds it: CRC and magic in front of `payload`, then obfuscated. For tests and
/// tools; the game does this itself.
pub fn game_packet(payload: &[u8]) -> Vec<u8> {
    let mut p = vec![0u8; 4];
    p.extend_from_slice(&0xF00Du16.to_le_bytes());
    p.extend_from_slice(payload);
    let crc = p[4..].iter().fold(0u32, |c, &b| (c << 1).wrapping_add(b as u32).wrapping_add(c >> 31));
    p[..4].copy_from_slice(&crc.to_le_bytes());
    let mut mask: u32 = 0xFADE;
    for w in p.chunks_exact_mut(4) {
        let v = u32::from_le_bytes([w[0], w[1], w[2], w[3]]) ^ mask;
        w.copy_from_slice(&v.to_be_bytes());
        mask = mask.wrapping_add(0x321);
    }
    p
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
            Packet::Welcome { vip: [10, 77, 0, 3] },
            Packet::Reject(Reason::WrongPassword),
            Packet::Bcast { vip: [0; 4], src_port: 8086, dst_port: 8086, payload: vec![1, 2, 3] },
            Packet::Data { vip: [10, 77, 0, 9], src_port: 8088, dst_port: 8088, payload: vec![] },
            Packet::Bind { port: 8088 },
            Packet::Register,
            Packet::Issued { serial: "AAAA-BBBB-CCCC-DDDD".into() },
            Packet::Ping,
            Packet::Leave,
            Packet::StatsRequest,
            Packet::Stats(ServerStats { uptime_secs: 86_400, rooms: 3, players: 9, max_rooms: 256, max_players: 2048, room_players: 4, load_pct: 12, pps: 1500, kbps: 800, version: "0.1.0-beta.4".into() }),
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
    fn wire_roundtrip_and_limits() {
        for w in [
            Wire::KeyRequest,
            Wire::KeyReply { key: [3; 32] },
            Wire::Hs1 { cookie: [0; 16], msg: vec![1; 80] },
            Wire::Challenge { cookie: [9; 16] },
            Wire::Hs2 { msg: vec![2; 60] },
            Wire::Transport { data: vec![4; 40] },
        ] {
            assert_eq!(Wire::decode(&w.encode()), Some(w));
        }
        // a spoofed key request cannot buy more than it costs: the reply is smaller than the request
        assert!(Wire::KeyReply { key: [0; 32] }.encode().len() < Wire::KeyRequest.encode().len());
        assert_eq!(Wire::decode(&[20; 10]), None);
        assert_eq!(Wire::decode(&[22; 10]), None, "cookie alone is not a handshake");
        assert_eq!(Wire::decode(&[25]), None);
        assert_eq!(Wire::decode(&[]), None);
        assert_eq!(Wire::decode(&[99, 1, 2]), None);
        assert_eq!(Wire::decode(&vec![25; 1600]), None, "oversized");
        // the content layer is never accepted on the wire, nor the wire layer as content
        assert_eq!(Wire::decode(&Packet::Ping.encode()), None);
        assert_eq!(Packet::decode(&Wire::KeyRequest.encode()), None);
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
        // trailing junk where none is allowed
        assert_eq!(Packet::decode(&[8; 3]), None);
        assert_eq!(Packet::decode(&[2, 1, 2, 3]), None); // Welcome is exactly 4 bytes of address
        assert_eq!(Packet::decode(&[6, 0]), None);
    }

    /// Packets built by the engine's own Transport code (its CRC class and encryptBuf), captured with a small C++ program.
    #[test]
    fn recognises_real_game_packets_and_nothing_else() {
        let hex = |s: &str| (0..s.len()).step_by(2).map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap()).collect::<Vec<u8>>();
        let a = hex("00007f2902010df20604052307");
        let b = hex("00019bf165470df26173644e6c73");
        assert!(is_game_packet(&a) && is_game_packet(&b));
        assert_eq!(game_packet(&[1, 2, 3, 4, 5, 6, 7]), a);
        assert_eq!(game_packet(b"Generals"), b);
        // any change breaks the CRC; so does the wrong size, or something that is not a game packet at all
        for i in 0..a.len() {
            let mut bad = a.clone();
            bad[i] ^= 1;
            assert!(!is_game_packet(&bad), "flipped byte {i}");
        }
        assert!(!is_game_packet(&a[..6]) && !is_game_packet(&[]) && !is_game_packet(&[0; 1101]));
        assert!(!is_game_packet(b"GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"));
        assert!(!is_game_packet(&[0u8; 64]));
    }

    #[test]
    fn stats_reject_malformed() {
        let ok = Packet::Stats(ServerStats { uptime_secs: 1, rooms: 1, players: 1, max_rooms: 1, max_players: 1, room_players: 1, load_pct: 1, pps: 1, kbps: 1, version: "v".into() }).encode();
        assert!(Packet::decode(&ok).is_some());
        assert_eq!(Packet::decode(&ok[..ok.len() - 1]), None);
        assert_eq!(Packet::decode(&[&ok[..], &[0][..]].concat()), None);
        assert_eq!(Packet::decode(&[13, 0]), None);
        let long = Packet::Stats(ServerStats { uptime_secs: 0, rooms: 0, players: 0, max_rooms: 0, max_players: 0, room_players: 0, load_pct: 0, pps: 0, kbps: 0, version: "x".repeat(200) });
        assert_eq!(Packet::decode(&long.encode()), Some(Packet::Stats(ServerStats { uptime_secs: 0, rooms: 0, players: 0, max_rooms: 0, max_players: 0, room_players: 0, load_pct: 0, pps: 0, kbps: 0, version: "x".repeat(MAX_VERSION) })));
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
