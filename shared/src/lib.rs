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
    /// Engine -> local agent only, first thing from each of the engine's sockets: proves it is the process the launcher
    /// started (the token is passed to it in `GEN_RELAY_TOKEN`). Anything from a local address that has not sent it is dropped.
    Auth { token: [u8; 16] },
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
    /// Engine -> agent -> server: "my simulation's checksum at this game frame is `crc`". Every player reports its own;
    /// the server compares them (see `SyncVerdict`). `host` is true for the game host, whose simulation is the master: the
    /// others are measured against it.
    Crc { frame: u32, crc: u32, host: bool },
    /// Server -> agent -> engine: the checksums of a frame did not all agree.
    Desync(SyncVerdict),
    /// Client -> server: set the room's "Stronger AI" level (0 to 3). Only the room's master (the first player in it) is heard.
    SetAi { level: u8 },
    /// Server -> client, with every ping answer: this room's settings. `master` says whether the receiver is the room's master;
    /// `ai` is the level every player's game must start with.
    Room { master: bool, ai: u8 },
    /// Red Alert 2 game traffic (CnCNet tunnel v2, see `MAX_TUNNEL_PAYLOAD`). Agent -> server: `vip` is the destination.
    /// Server -> agent: the source. Carried only between players of the same room, and only if `is_ra2_game_packet`.
    Tunnel { vip: Vip, payload: Vec<u8> },
    /// The launcher's own Red Alert 2 pre-game lobby: an opaque blob. Agent -> server: `vip` ignored; the server hands it
    /// to every other player in the room with `vip` set to the sender's.
    Lobby { vip: Vip, payload: Vec<u8> },
    /// Map sharing between the launchers of one room (see `MapMsg` and docs/send-map-threat-model.md). Agent -> server:
    /// `vip` is the destination (all zero for `Hi`, which goes to the whole room). Server -> agent: the sender, as the server
    /// knows it (never what the payload says); a `Caps` answer comes from the server itself, with `vip` all zero.
    Map { vip: Vip, msg: MapMsg },
}

/// Map sharing, version 1. A transfer is one `Offer` (sender -> one player), an `Accept` from that player, `Data` chunks
/// answered by cumulative `Ack`s, and an `End` from the receiver (or a `Cancel` from the sender). The relay checks every
/// step (`server/src/lib.rs`), so nothing reaches a player who did not accept and nothing is stored on the relay.
pub const MAP_PROTO: u8 = 1;
/// The most one transfer may carry: the packed (compressed) map. The relay refuses a bigger `Offer`.
pub const MAX_MAP_BYTES: u32 = 8 << 20;
/// Bytes of map in one `MapMsg::Data` (the packet stays well below `MAX_WIRE`).
pub const MAP_CHUNK: usize = 900;
pub const MAX_MAP_NAME: usize = 120;
pub const MAX_PEER_NAME: usize = 24;
pub const MAP_GENERALS: u8 = 0;
pub const MAP_RA2: u8 = 1;
/// How an `End` ended (the receiver says; the sender shows it).
pub mod map_end {
    pub const DELIVERED: u8 = 0;
    pub const DECLINED: u8 = 1;
    /// The receiver already has this exact map.
    pub const HAVE_IT: u8 = 2;
    pub const BLOCKED: u8 = 3;
    pub const BUSY: u8 = 4;
    /// The receiver plays the other game.
    pub const OTHER_GAME: u8 = 5;
    /// What arrived was not a good map (hash, structure or install failed).
    pub const BAD: u8 = 6;
    pub const CANCELLED: u8 = 7;
    /// Too big or not allowed.
    pub const REFUSED: u8 = 8;
    pub const LAST: u8 = 8;
}

/// Number of chunks a transfer of `size` bytes has.
pub const fn map_chunks(size: u32) -> u32 {
    size.div_ceil(MAP_CHUNK as u32)
}

/// Exact length chunk `seq` must have (every chunk is `MAP_CHUNK` bytes but the last).
pub const fn map_chunk_len(size: u32, seq: u32) -> usize {
    let rest = size as usize - seq as usize * MAP_CHUNK;
    if rest > MAP_CHUNK { MAP_CHUNK } else { rest }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum MapMsg {
    /// "I am here": which game this player has selected and a name to show. Sent to the whole room every few seconds. The
    /// server fills `master` in on the way (a client's value is ignored), the sender's address replaces `vip`.
    Hi { game: u8, master: bool, name: String },
    /// Server -> client only, answering the first `Hi`: this relay shares maps. An old relay never says it.
    Caps { proto: u8, max_bytes: u32 },
    /// Sender -> one player: "may I send you this map?" `size` is the packed size, `hash` identifies the map's content,
    /// `players` is how many start positions it has (display only). `master` is filled in by the server.
    Offer { id: u32, game: u8, master: bool, size: u32, players: u8, hash: [u8; 32], name: String },
    /// Receiver -> sender: yes, send it.
    Accept { id: u32 },
    /// Receiver -> sender: finished, one of `map_end`. Also the way to say no.
    End { id: u32, code: u8 },
    /// Sender -> receiver: chunk `seq` (see `map_chunk_len`).
    Data { id: u32, seq: u32, bytes: Vec<u8> },
    /// Receiver -> sender: every chunk below `next` has arrived.
    Ack { id: u32, next: u32 },
    /// Sender -> receiver: stop.
    Cancel { id: u32 },
}

impl MapMsg {
    pub fn encode(&self) -> Vec<u8> {
        let id = |n: u32| n.to_be_bytes();
        match self {
            MapMsg::Hi { game, master, name } => [&[1, *game, *master as u8, name.len().min(MAX_PEER_NAME) as u8][..], &name.as_bytes()[..name.len().min(MAX_PEER_NAME)]].concat(),
            MapMsg::Caps { proto, max_bytes } => [&[2, *proto][..], &max_bytes.to_be_bytes()].concat(),
            MapMsg::Offer { id: i, game, master, size, players, hash, name } => {
                [&[3][..], &id(*i), &[*game, *master as u8], &size.to_be_bytes(), &[*players], hash, &[name.len().min(MAX_MAP_NAME) as u8], &name.as_bytes()[..name.len().min(MAX_MAP_NAME)]].concat()
            }
            MapMsg::Accept { id: i } => [&[4][..], &id(*i)].concat(),
            MapMsg::End { id: i, code } => [&[5][..], &id(*i), &[*code]].concat(),
            MapMsg::Data { id: i, seq, bytes } => [&[6][..], &id(*i), &seq.to_be_bytes(), bytes].concat(),
            MapMsg::Ack { id: i, next } => [&[7][..], &id(*i), &next.to_be_bytes()].concat(),
            MapMsg::Cancel { id: i } => [&[8][..], &id(*i)].concat(),
        }
    }

    /// Strict: wrong lengths, unknown values and oversize fields are all `None`.
    pub fn decode(b: &[u8]) -> Option<MapMsg> {
        let (&kind, r) = b.split_first()?;
        let u32_at = |r: &[u8], i: usize| -> Option<u32> { Some(u32::from_be_bytes(r.get(i..i + 4)?.try_into().ok()?)) };
        let game_ok = |g: u8| g == MAP_GENERALS || g == MAP_RA2;
        Some(match kind {
            1 if r.len() >= 3 && game_ok(r[0]) && r[1] <= 1 => {
                let (name, tail) = take_str(&r[2..], MAX_PEER_NAME)?;
                tail.is_empty().then_some(MapMsg::Hi { game: r[0], master: r[1] != 0, name })?
            }
            2 if r.len() == 5 => MapMsg::Caps { proto: r[0], max_bytes: u32_at(r, 1)? },
            3 if r.len() >= 44 && game_ok(r[4]) && r[5] <= 1 => {
                let size = u32_at(r, 6)?;
                if size == 0 || size > MAX_MAP_BYTES {
                    return None;
                }
                let (name, tail) = take_str(&r[43..], MAX_MAP_NAME)?;
                if !tail.is_empty() || name.is_empty() {
                    return None;
                }
                MapMsg::Offer { id: u32_at(r, 0)?, game: r[4], master: r[5] != 0, size, players: r[10], hash: r[11..43].try_into().ok()?, name }
            }
            4 if r.len() == 4 => MapMsg::Accept { id: u32_at(r, 0)? },
            5 if r.len() == 5 && r[4] <= map_end::LAST => MapMsg::End { id: u32_at(r, 0)?, code: r[4] },
            6 if (9..=8 + MAP_CHUNK).contains(&r.len()) => MapMsg::Data { id: u32_at(r, 0)?, seq: u32_at(r, 4)?, bytes: r[8..].to_vec() },
            7 if r.len() == 8 => MapMsg::Ack { id: u32_at(r, 0)?, next: u32_at(r, 4)? },
            8 if r.len() == 4 => MapMsg::Cancel { id: u32_at(r, 0)? },
            _ => return None,
        })
    }
}

/// Largest Red Alert 2 datagram relayed. The CnCNet v2 tunnel server reads into a 1024-byte buffer, 4 of which are the
/// tunnel header (cncnet-server `TunnelV2.SyncReceive`), so 1020 is what any real tunnel carries.
pub const MAX_TUNNEL_PAYLOAD: usize = 1020;
pub const MAX_LOBBY_PAYLOAD: usize = 1024;

/// The highest "Stronger AI" level.
pub const MAX_AI_LEVEL: u8 = 3;

/// The relay's judgement of one game frame whose players' checksums differed.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SyncVerdict {
    pub frame: u32,
    /// `true` when the verdict says who diverged: the host's checksum is the master when the host reported one, otherwise a
    /// strict majority decides. With two players and no host report (or a tie) nobody can say who is right: `false`.
    pub certain: bool,
    /// The host the others were measured against (its virtual address), if the verdict went by the host.
    pub host: Option<Vip>,
    pub players: Vec<SyncEntry>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SyncEntry {
    pub vip: Vip,
    pub crc: u32,
    pub odd: bool,
}

/// A game has at most this many players.
pub const MAX_SYNC_PLAYERS: usize = 8;

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
            Packet::Auth { token } => [&[17][..], token].concat(),
            Packet::Register => vec![8],
            Packet::Issued { serial } => [&[9, serial.len() as u8][..], serial.as_bytes()].concat(),
            Packet::Leave => vec![10],
            Packet::StatsRequest => vec![13],
            Packet::Crc { frame, crc, host } => [&[15][..], &frame.to_be_bytes(), &crc.to_be_bytes(), &[*host as u8]].concat(),
            Packet::Desync(v) => {
                let mut b = vec![16];
                b.extend_from_slice(&v.frame.to_be_bytes());
                b.push(v.certain as u8);
                b.extend_from_slice(&v.host.unwrap_or([0; 4]));
                b.push(v.players.len().min(MAX_SYNC_PLAYERS) as u8);
                for e in v.players.iter().take(MAX_SYNC_PLAYERS) {
                    b.extend_from_slice(&e.vip);
                    b.extend_from_slice(&e.crc.to_be_bytes());
                    b.push(e.odd as u8);
                }
                b
            }
            Packet::SetAi { level } => vec![18, *level],
            Packet::Room { master, ai } => vec![19, *master as u8, *ai],
            Packet::Tunnel { vip, payload } => [&[26][..], vip, payload].concat(),
            Packet::Lobby { vip, payload } => [&[27][..], vip, payload].concat(),
            Packet::Map { vip, msg } => [&[28][..], vip, &msg.encode()].concat(),
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
            17 if rest.len() == 16 => Packet::Auth { token: rest.try_into().ok()? },
            8 if rest.is_empty() => Packet::Register,
            9 => {
                let (serial, r) = take_str(rest, MAX_SERIAL)?;
                r.is_empty().then_some(Packet::Issued { serial })?
            }
            10 if rest.is_empty() => Packet::Leave,
            13 if rest.is_empty() => Packet::StatsRequest,
            15 if rest.len() == 9 => Packet::Crc { frame: u32::from_be_bytes(rest[..4].try_into().ok()?), crc: u32::from_be_bytes(rest[4..8].try_into().ok()?), host: rest[8] != 0 },
            16 if rest.len() >= 10 => {
                let n = rest[9] as usize;
                if n > MAX_SYNC_PLAYERS || rest.len() != 10 + n * 9 {
                    return None;
                }
                let host: Vip = rest[5..9].try_into().ok()?;
                let players = (0..n)
                    .map(|i| {
                        let e = &rest[10 + i * 9..10 + (i + 1) * 9];
                        SyncEntry { vip: e[..4].try_into().unwrap(), crc: u32::from_be_bytes(e[4..8].try_into().unwrap()), odd: e[8] != 0 }
                    })
                    .collect();
                Packet::Desync(SyncVerdict { frame: u32::from_be_bytes(rest[..4].try_into().ok()?), certain: rest[4] != 0, host: (host != [0; 4]).then_some(host), players })
            }
            18 if rest.len() == 1 && rest[0] <= MAX_AI_LEVEL => Packet::SetAi { level: rest[0] },
            19 if rest.len() == 2 && rest[1] <= MAX_AI_LEVEL => Packet::Room { master: rest[0] != 0, ai: rest[1] },
            26 if (5..=4 + MAX_TUNNEL_PAYLOAD).contains(&rest.len()) => Packet::Tunnel { vip: rest[..4].try_into().ok()?, payload: rest[4..].to_vec() },
            27 if (4..=4 + MAX_LOBBY_PAYLOAD).contains(&rest.len()) => Packet::Lobby { vip: rest[..4].try_into().ok()?, payload: rest[4..].to_vec() },
            28 if rest.len() > 4 => Packet::Map { vip: rest[..4].try_into().ok()?, msg: MapMsg::decode(&rest[4..])? },
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
    for w in plain.as_chunks_mut::<4>().0 {
        let v = u32::from_be_bytes([w[0], w[1], w[2], w[3]]) ^ mask;
        w.copy_from_slice(&v.to_le_bytes());
        mask = mask.wrapping_add(0x321);
    }
    let crc = u32::from_le_bytes([plain[0], plain[1], plain[2], plain[3]]);
    let magic = u16::from_le_bytes([plain[4], plain[5]]);
    let sum = plain[4..].iter().fold(0u32, |c, &b| (c << 1).wrapping_add(b as u32).wrapping_add(c >> 31));
    magic == 0xF00D && sum == crc
}

/// Largest Red Alert 2 game datagram: the engine's `build_datagram` refuses a packet over 636 bytes (`kMaxDatagramPayload`,
/// the original's 0x280 receive buffer less the checksum), plus its 4-byte checksum.
pub const MAX_RA2_DATAGRAM: usize = 640;

/// Does `p` (a `Packet::Tunnel` payload: what follows the CnCNet v2 header) look like a datagram the Red Alert 2 engine
/// made? Its layout (`ra2/engine/engine/net/wire.hpp`, `channel.hpp`, byte-exact with gamemd's UDPInterfaceClass and
/// ConnectionClass) is `[u32 LE checksum][channel packet]`; the checksum is `crc = rotl1(crc) + word` over the packet's
/// little-endian 32-bit words, a 1..3 byte tail read as a zero-padded word. The packet is a private channel packet (magic
/// 0xAA04, Yuri's Revenge's product id, 14-byte header) or a global one (magic 0x1236, 16-byte header), both with code 0
/// to 2 (data with ack, data without, ack) and a zero router byte. Like `is_game_packet` for Generals, the relay carries
/// nothing else.
pub fn is_ra2_game_packet(p: &[u8]) -> bool {
    if p.len() < 6 || p.len() > MAX_RA2_DATAGRAM {
        return false;
    }
    let (sum, pkt) = (u32::from_le_bytes([p[0], p[1], p[2], p[3]]), &p[4..]);
    let header = match u16::from_le_bytes([pkt[0], pkt[1]]) {
        0xAA04 => 14,
        0x1236 => 16,
        _ => return false,
    };
    pkt.len() >= header && pkt[2] <= 2 && pkt[3] == 0 && ra2_checksum(pkt) == sum
}

/// The Red Alert 2 engine's datagram checksum (`packet_crc` in `ra2/engine/engine/net/wire.cpp`).
fn ra2_checksum(pkt: &[u8]) -> u32 {
    pkt.chunks(4).fold(0u32, |c, w| {
        let mut b = [0u8; 4];
        b[..w.len()].copy_from_slice(w);
        c.rotate_left(1).wrapping_add(u32::from_le_bytes(b))
    })
}

/// A Red Alert 2 datagram the way its engine builds one: a global channel packet (magic 0x1236, code 1, 16-byte
/// header) around `body`, checksum in front. For tests and tools; the game does this itself.
pub fn ra2_game_packet(body: &[u8]) -> Vec<u8> {
    let pkt = [&[0x36, 0x12, 1, 0][..], &[0; 12], body].concat();
    [&ra2_checksum(&pkt).to_le_bytes()[..], &pkt].concat()
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
    for w in p.as_chunks_mut::<4>().0 {
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
            Packet::Auth { token: [5; 16] },
            Packet::Register,
            Packet::Issued { serial: "AAAA-BBBB-CCCC-DDDD".into() },
            Packet::Ping,
            Packet::Leave,
            Packet::StatsRequest,
            Packet::SetAi { level: 3 },
            Packet::Room { master: true, ai: 2 },
            Packet::Tunnel { vip: [10, 77, 0, 2], payload: vec![9; MAX_TUNNEL_PAYLOAD] },
            Packet::Lobby { vip: [0; 4], payload: vec![] },
            Packet::Lobby { vip: [10, 77, 0, 3], payload: vec![1; MAX_LOBBY_PAYLOAD] },
            Packet::Crc { frame: 100, crc: 0xDEADBEEF, host: true },
            Packet::Desync(SyncVerdict { frame: 100, certain: true, host: Some([10, 77, 0, 1]), players: vec![SyncEntry { vip: [10, 77, 0, 1], crc: 1, odd: false }, SyncEntry { vip: [10, 77, 0, 2], crc: 2, odd: true }] }),
            Packet::Stats(ServerStats { uptime_secs: 86_400, rooms: 3, players: 9, max_rooms: 256, max_players: 2048, room_players: 4, load_pct: 12, pps: 1500, kbps: 800, version: "0.1.0-beta.4".into() }),
        ] {
            assert_eq!(Packet::decode(&p.encode()), Some(p));
        }
    }

    /// Golden bytes; the engine's RelayWire.h test checks the same layout.
    #[test]
    fn sync_packets_layout_and_limits() {
        assert_eq!(Packet::Crc { frame: 100, crc: 0x12C26F6A, host: true }.encode(), [15, 0, 0, 0, 100, 0x12, 0xC2, 0x6F, 0x6A, 1], "the engine's RelayWire.h checks the same bytes");
        let v = Packet::Desync(SyncVerdict { frame: 7, certain: true, host: Some([10, 77, 0, 1]), players: vec![SyncEntry { vip: [10, 77, 0, 3], crc: 0xAABBCCDD, odd: true }] }).encode();
        assert_eq!(v, [16, 0, 0, 0, 7, 1, 10, 77, 0, 1, 1, 10, 77, 0, 3, 0xAA, 0xBB, 0xCC, 0xDD, 1]);
        assert_eq!(Packet::decode(&[15, 0, 0, 0, 1]), None);
        assert_eq!(Packet::decode(&[16, 0, 0, 0, 7, 0, 0, 0, 0, 0, 2, 10, 77, 0, 3, 0, 0, 0, 0, 0]), None, "count and length must agree");
        let mut nine = vec![16, 0, 0, 0, 1, 1, 0, 0, 0, 0, 9];
        nine.extend(vec![0u8; 81]);
        assert_eq!(Packet::decode(&nine), None, "more than eight players");
    }

    #[test]
    fn wire_layout() {
        let p = Packet::Data { vip: [10, 77, 0, 1], src_port: 8088, dst_port: 8086, payload: vec![0xAA] };
        assert_eq!(p.encode(), [5, 10, 77, 0, 1, 0x1F, 0x98, 0x1F, 0x96, 0xAA]);
        assert_eq!(Packet::Bind { port: 8086 }.encode(), [7, 0x1F, 0x96]);
        let mut auth = vec![17];
        auth.extend(0..16u8);
        assert_eq!(Packet::Auth { token: core::array::from_fn(|i| i as u8) }.encode(), auth, "the engine's RelayWire.h checks the same bytes");
        assert_eq!(Packet::decode(&[17, 1, 2, 3]), None);
    }

    #[test]
    fn ra2_packets_layout_and_limits() {
        assert_eq!(Packet::Tunnel { vip: [10, 77, 0, 2], payload: vec![0xAB, 0xCD] }.encode(), [26, 10, 77, 0, 2, 0xAB, 0xCD]);
        assert_eq!(Packet::Lobby { vip: [10, 77, 0, 1], payload: b"hi".to_vec() }.encode(), [27, 10, 77, 0, 1, b'h', b'i']);
        assert_eq!(Packet::decode(&[26, 10, 77, 0, 2]), None, "a tunnel datagram carries at least one byte");
        assert_eq!(Packet::decode(&[&[26, 10, 77, 0, 2][..], &[0; MAX_TUNNEL_PAYLOAD + 1]].concat()), None);
        assert_eq!(Packet::decode(&[27, 10, 77, 0]), None, "short address");
        assert_eq!(Packet::decode(&[&[27, 0, 0, 0, 0][..], &[0; MAX_LOBBY_PAYLOAD + 1]].concat()), None);
        // the encrypted transport has room for the biggest of them
        assert!(Packet::Lobby { vip: [0; 4], payload: vec![0; MAX_LOBBY_PAYLOAD] }.encode().len() + 64 < MAX_WIRE);
    }

    #[test]
    fn map_messages_roundtrip_layout_and_limits() {
        let offer = MapMsg::Offer { id: 7, game: MAP_RA2, master: true, size: 1234, players: 4, hash: [9; 32], name: "Dune Wars".into() };
        for m in [
            MapMsg::Hi { game: MAP_GENERALS, master: false, name: "Sam".into() },
            MapMsg::Caps { proto: MAP_PROTO, max_bytes: MAX_MAP_BYTES },
            offer.clone(),
            MapMsg::Accept { id: 7 },
            MapMsg::End { id: 7, code: map_end::HAVE_IT },
            MapMsg::Data { id: 7, seq: 3, bytes: vec![1; MAP_CHUNK] },
            MapMsg::Data { id: 7, seq: 3, bytes: vec![1] },
            MapMsg::Ack { id: 7, next: 4 },
            MapMsg::Cancel { id: 7 },
        ] {
            assert_eq!(Packet::decode(&Packet::Map { vip: [10, 77, 0, 2], msg: m.clone() }.encode()), Some(Packet::Map { vip: [10, 77, 0, 2], msg: m }));
        }
        // golden bytes: tag 28, the address, the kind, then big-endian fields
        assert_eq!(Packet::Map { vip: [10, 77, 0, 2], msg: MapMsg::Accept { id: 0x01020304 } }.encode(), [28, 10, 77, 0, 2, 4, 1, 2, 3, 4]);
        assert_eq!(Packet::Map { vip: [0; 4], msg: MapMsg::Hi { game: 1, master: false, name: "Sam".into() } }.encode(), [28, 0, 0, 0, 0, 1, 1, 0, 3, b'S', b'a', b'm']);
        assert_eq!(Packet::Map { vip: [10, 77, 0, 1], msg: MapMsg::Ack { id: 1, next: 2 } }.encode(), [28, 10, 77, 0, 1, 7, 0, 0, 0, 1, 0, 0, 0, 2]);
        let mut o = vec![28, 10, 77, 0, 3, 3, 0, 0, 0, 7, 1, 1, 0, 0, 4, 0xD2, 4];
        o.extend([9; 32]);
        o.extend([9, b'D', b'u', b'n', b'e', b' ', b'W', b'a', b'r', b's']);
        assert_eq!(Packet::Map { vip: [10, 77, 0, 3], msg: offer }.encode(), o);
        // too long, too short, trailing bytes, unknown kinds and values are all refused
        let dec = |m: &[u8]| Packet::decode(&[&[28, 10, 77, 0, 2][..], m].concat());
        assert_eq!(dec(&[]), None);
        assert_eq!(dec(&[4, 0, 0, 0]), None);
        assert_eq!(dec(&[4, 0, 0, 0, 1, 0]), None, "Accept is exactly an id");
        assert_eq!(dec(&[5, 0, 0, 0, 1, map_end::LAST + 1]), None, "unknown end code");
        assert_eq!(dec(&[9, 0, 0, 0, 1]), None, "unknown kind");
        assert_eq!(dec(&[1, 2, 0, 0]), None, "unknown game");
        assert_eq!(dec(&[6, 0, 0, 0, 1, 0, 0, 0, 0]), None, "a chunk carries at least a byte");
        assert_eq!(dec(&[&[6, 0, 0, 0, 1, 0, 0, 0, 0][..], &[0; MAP_CHUNK + 1]].concat()), None, "chunk too long");
        assert_eq!(Packet::decode(&[28, 10, 77, 0, 2]), None);
        let big = |size: u32| MapMsg::Offer { id: 1, game: 0, master: false, size, players: 2, hash: [0; 32], name: "m".into() }.encode();
        assert!(MapMsg::decode(&big(MAX_MAP_BYTES)).is_some());
        assert_eq!(MapMsg::decode(&big(MAX_MAP_BYTES + 1)), None);
        assert_eq!(MapMsg::decode(&big(0)), None);
        let mut long = big(5);
        long[43 + 1] = MAX_MAP_NAME as u8 + 1; // length byte (kind + 43)
        long.extend(vec![b'x'; MAX_MAP_NAME + 1]);
        assert_eq!(MapMsg::decode(&long), None, "name over the limit");
        let hi_long = [&[1, 0, 0, MAX_PEER_NAME as u8 + 1][..], &[b'x'; MAX_PEER_NAME + 1]].concat();
        assert_eq!(MapMsg::decode(&hi_long), None);
        assert_eq!(MapMsg::decode(&[1, 0, 0, 2, 0xFF, 0xFE]), None, "names are UTF-8");
        // the biggest map message fits the encrypted transport
        assert!(Packet::Map { vip: [0; 4], msg: MapMsg::Data { id: 1, seq: 1, bytes: vec![0; MAP_CHUNK] } }.encode().len() + 64 < MAX_WIRE);
        assert!(Packet::Map { vip: [0; 4], msg: MapMsg::Offer { id: 1, game: 0, master: false, size: 1, players: 0, hash: [0; 32], name: "x".repeat(MAX_MAP_NAME) } }.encode().len() + 64 < MAX_WIRE);
        // chunk arithmetic
        assert_eq!((map_chunks(1), map_chunks(900), map_chunks(901), map_chunks(MAX_MAP_BYTES)), (1, 1, 2, 9321));
        assert_eq!((map_chunk_len(901, 0), map_chunk_len(901, 1), map_chunk_len(900, 0)), (900, 1, 900));
        let total: usize = (0..map_chunks(MAX_MAP_BYTES)).map(|s| map_chunk_len(MAX_MAP_BYTES, s)).sum();
        assert_eq!(total, MAX_MAP_BYTES as usize);
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

    /// Datagrams the Red Alert 2 engine's own NetManager sent through a V2 tunnel socket (captured with a small C++
    /// program linking `engine/net` and `engine/mp/mp_net.cpp`), tunnel header removed.
    #[test]
    fn recognises_real_ra2_game_packets_and_nothing_else() {
        let hex = |s: &str| (0..s.len()).step_by(2).map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap()).collect::<Vec<u8>>();
        let real = [
            hex("ac40990004aa0100000000000000000000001900010000002a"),   // private, no ack wanted (a frame)
            hex("41a03c0004aa000000000000000000000000190001"),           // private, ack wanted
            hex("2050150004aa020000000000000000000000"),                 // private ack
            hex("0212eb80361200000000000000000000000004aa68656c6c6f"),   // global (lobby), ack wanted
            hex("b09114aa361202000000000000000000000004aa"),             // global ack
        ];
        for (n, a) in real.iter().enumerate() {
            assert!(is_ra2_game_packet(a), "packet {n}");
            for i in 0..a.len() {
                for bit in [1u8, 0x80] {
                    let mut bad = a.clone();
                    bad[i] ^= bit;
                    assert!(!is_ra2_game_packet(&bad), "packet {n}, flipped byte {i}");
                }
            }
        }
        // shorter than its header (checksum fixed up), the tunnel's keep-alive and ping, the token datagram, other things
        let mut short = real[2][..17].to_vec();
        let sum = ra2_checksum(&short[4..]);
        short[..4].copy_from_slice(&sum.to_le_bytes());
        assert!(!is_ra2_game_packet(&short));
        assert!(!is_ra2_game_packet(&[]) && !is_ra2_game_packet(&[0]) && !is_ra2_game_packet(&[0; 46]));
        assert!(!is_ra2_game_packet(&hex("ffffffff5250415500112233445566778899aabbccddeeff")));
        assert!(!is_ra2_game_packet(b"GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"));
        assert!(is_ra2_game_packet(&ra2_game_packet(b"x")) && ra2_game_packet(&[]).len() == 20);
        // 640 bytes is the most the engine sends
        let mut big = [&[0u8; 4][..], &[0x36, 0x12, 1, 0]].concat();
        big.resize(MAX_RA2_DATAGRAM, 7);
        let sum = ra2_checksum(&big[4..]);
        big[..4].copy_from_slice(&sum.to_le_bytes());
        assert!(is_ra2_game_packet(&big));
        big.push(7);
        let sum = ra2_checksum(&big[4..]);
        big[..4].copy_from_slice(&sum.to_le_bytes());
        assert!(!is_ra2_game_packet(&big));
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
