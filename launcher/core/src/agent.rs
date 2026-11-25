//! Tunnel agent. The engine (patched, `GEN_RELAY`) sends its UDP traffic to a loopback socket here; the agent
//! signs up with the relay if needed, joins the room, and forwards datagrams both ways. Sign-up is invisible to
//! the user: with no stored serial the agent registers and hands the issued serial back through `take_issued`.
//! Engine sockets register with `Bind{port}` (or by sending), keyed by logical port (8086 lobby, 8088 game).
//! Red Alert 2 uses a second loopback socket that looks like a CnCNet tunnel to the game (see `from_spawner`).
use generals_proto::secure::{self, ClientPending, Key, Session as Crypto};
use generals_proto::{ct_eq, is_game_packet, is_ra2_game_packet, room_verifier, Cookie, MapMsg, Packet, Reason, Vip, Wire, GAME_PORTS, LOBBY_PORT, MAX_LOBBY_PAYLOAD};
use std::collections::{HashMap, VecDeque};
use std::io;
use std::net::{SocketAddr, ToSocketAddrs, UdpSocket};
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize)]
pub enum Status {
    Connecting,
    Connected(Vip),
    Rejected(u8),
}

const TICK: Duration = Duration::from_millis(500);
const PING_EVERY: Duration = Duration::from_secs(2);
/// Pings kept for latency and loss: about a minute.
const WINDOW: usize = 30;
/// How long the relay may stay silent before the agent joins again. The server forgets a quiet client after 30 s,
/// and a returning serial takes over its old session, so rejoining early is safe and shortens a dead lobby.
const SILENCE_LIMIT: Duration = Duration::from_secs(8);
/// Red Alert 2 lobby blobs kept until the launcher takes them; the oldest go first.
const LOBBY_QUEUE: usize = 256;
/// Received lobby blobs: (last octet of the sender's virtual address, blob).
type LobbyQueue = Arc<Mutex<VecDeque<(u8, Vec<u8>)>>>;
/// Map-sharing messages kept until the launcher takes them (it does every few tens of milliseconds); the oldest go first,
/// which a transfer survives (it repeats what was not acknowledged).
const MAP_QUEUE: usize = 2048;
/// Received map messages: (sender's virtual address, or all zeros for the server itself, message).
type MapQueue = Arc<Mutex<VecDeque<(Vip, MapMsg)>>>;
/// Every room's virtual addresses are `10.77.0.x`; a Red Alert 2 player's tunnel id is `x`.
const VIP_PREFIX: [u8; 3] = [10, 77, 0];

/// What a datagram from the local Red Alert 2 game (yrpp-spawner in tunnel mode) asks for.
#[derive(Debug, PartialEq, Eq)]
enum FromSpawner<'a> {
    /// The game presented the token: answer with `AUTH_PREFIX`.
    Authed,
    /// A tunnel ping: answer with these bytes.
    Pong(&'a [u8]),
    /// Game traffic for the player at this virtual address.
    Forward { to: Vip, payload: &'a [u8] },
    Ignore,
}

/// The first 8 bytes of the engine's token datagram (`[Tunnel] Token=`, ra2 patch 0006), followed by the token; the agent
/// answers with these 8 bytes alone. Sender = receiver = 0xFFFF, which no tunnel v2 header has (they must differ).
const AUTH_PREFIX: [u8; 8] = [0xFF, 0xFF, 0xFF, 0xFF, b'R', b'P', b'A', b'U'];

/// Who may use the Red Alert 2 tunnel: the endpoint that presented this launch's token (`Agent::arm_tunnel`).
struct TunnelTrust {
    token: [u8; 16],
    peer: Option<SocketAddr>,
}

/// CnCNet tunnel v2 as our engine (and yrpp-spawner) speaks it. Checked against CnCNet/yrpp-spawner `src/Spawner/NetHack.cpp`
/// (`Tunnel::SendTo`/`RecvFrom`: header `u16 sender, u16 receiver`, both `htons`, so big-endian, then at most 1024 bytes
/// of payload; a received datagram needs at least 5 bytes and `receiver == own id`, and `sender` becomes the peer's
/// "port", matched against `[OtherN] Port`) and CnCNet/cncnet-server `TunnelV2.cs` (`OnReceive`: sender 0 and receiver 0
/// with exactly 50 bytes is a ping, answered with its first 12 bytes; sender == receiver otherwise is refused; its
/// 1024-byte buffer leaves 1020 bytes of payload). Here a player's tunnel id is the last octet of its room address.
///
/// Only the loopback endpoint that sent the token datagram (`AUTH_PREFIX` + the current token, compared in constant time)
/// is heard, the way Generals engines present `Packet::Auth`; everything from any other endpoint is dropped. Its
/// payloads must be the engine's own datagrams (`is_ra2_game_packet`).
fn from_spawner<'a>(own: Option<u8>, trust: &mut TunnelTrust, from: SocketAddr, d: &'a [u8]) -> FromSpawner<'a> {
    if from.ip() != std::net::Ipv4Addr::LOCALHOST || d.len() < 4 {
        return FromSpawner::Ignore;
    }
    if d.starts_with(&AUTH_PREFIX) {
        if ct_eq(&d[AUTH_PREFIX.len()..], &trust.token) && trust.peer.is_none_or(|p| p == from) {
            trust.peer = Some(from);
            return FromSpawner::Authed;
        }
        return FromSpawner::Ignore;
    }
    if trust.peer != Some(from) {
        return FromSpawner::Ignore;
    }
    let (sender, receiver) = (u16::from_be_bytes([d[0], d[1]]), u16::from_be_bytes([d[2], d[3]]));
    if sender == 0 && receiver == 0 {
        return if d.len() == 50 { FromSpawner::Pong(&d[..12]) } else { FromSpawner::Ignore };
    }
    let payload = &d[4..];
    let Some(own) = own else { return FromSpawner::Ignore };
    if sender != own as u16 || receiver == sender || !(1..=255).contains(&receiver) || !is_ra2_game_packet(payload) {
        return FromSpawner::Ignore;
    }
    FromSpawner::Forward { to: [VIP_PREFIX[0], VIP_PREFIX[1], VIP_PREFIX[2], receiver as u8], payload }
}

/// A relayed Red Alert 2 datagram as the spawner expects it from its tunnel: `sender` is the source's id.
fn to_spawner(src: Vip, own: u8, payload: &[u8]) -> Option<Vec<u8>> {
    (src[..3] == VIP_PREFIX && src[3] != own && is_ra2_game_packet(payload))
        .then(|| [&(src[3] as u16).to_be_bytes()[..], &(own as u16).to_be_bytes(), payload].concat())
}

/// Where the connection is, for the Online page when it cannot connect (or lost the server).
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct Connection {
    /// The address the server name resolved to.
    pub address: String,
    pub stage: String,
    pub waited_secs: u64,
    /// Datagrams received from the server so far.
    pub replies: u32,
    /// Times the agent had to join again after the server went quiet.
    pub reconnects: u32,
    /// The server gave this player a different virtual address after rejoining: a running game still uses the old one.
    pub address_changed: bool,
    /// What to check, in plain words. Empty while all is well or too early to say.
    pub hint: String,
}

/// What to tell a player who is not connected yet, from what the agent has seen.
pub fn connect_hint(stage: &str, replies: u32, waited_secs: u64, address: &str) -> String {
    if stage == "connected" || waited_secs < 6 {
        return String::new();
    }
    if replies == 0 {
        return format!("Nothing has answered from {address} in {waited_secs} seconds. Check the server address and port, that the server is running, and that no firewall or router is blocking UDP to that port.");
    }
    match stage {
        "asking for the server's identity" => format!("{address} answers, but not with its identity, so it may not be a Rallypoint server."),
        _ if waited_secs >= 15 => format!("{address} answers, but signing in to the room has not finished after {waited_secs} seconds. The server may be busy, or a network device may be dropping some of the replies."),
        _ => String::new(),
    }
}

#[derive(Default)]
struct Progress {
    started: Option<Instant>,
    replies: u32,
    stage: &'static str,
    reconnects: u32,
    first_vip: Option<Vip>,
    address_changed: bool,
}

/// What the relay says about itself, for display.
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct ServerView {
    pub version: String,
    pub uptime_secs: u32,
    pub rooms: u16,
    pub players: u16,
    pub max_rooms: u16,
    pub max_players: u16,
    pub room_players: u8,
    pub load_pct: u8,
    pub pps: u32,
    pub kbps: u32,
}

/// Connection health as the player sees it.
#[derive(Debug, Clone, Default, PartialEq, Eq, serde::Serialize)]
pub struct Diagnostics {
    /// Round trip to the relay: the latest, the average and the jitter (average change between pings).
    pub rtt_ms: Option<u32>,
    pub avg_ms: Option<u32>,
    pub jitter_ms: Option<u32>,
    /// Share of recent pings that got no answer.
    pub loss_pct: Option<u32>,
    pub samples: u32,
    /// Game traffic through the tunnel, kilobits per second.
    pub up_kbps: u32,
    pub down_kbps: u32,
    pub server: Option<ServerView>,
    /// The relay's identity (short fingerprint) and whether it was already trusted from an earlier visit.
    pub server_identity: Option<String>,
    pub identity_saved: bool,
    /// The relay's last verdict that the players' simulations differed (see `Packet::Desync`).
    pub desync: Option<DesyncView>,
    /// Whether this player is the room's master (the first one in it), and the "Stronger AI" level the room plays with.
    /// `None` until the relay has said.
    pub room_master: Option<bool>,
    pub ai_level: Option<u8>,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct DesyncView {
    pub frame: u32,
    pub certain: bool,
    /// The host the others were measured against (virtual address), if the verdict went by the host.
    pub host: Option<String>,
    pub players: Vec<DesyncPlayer>,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct DesyncPlayer {
    /// Virtual address, e.g. `10.77.0.2`.
    pub vip: String,
    pub crc: String,
    pub odd: bool,
}

/// Rolling ping record. The ping has no id, so an answer is credited to the latest ping sent; a reply that
/// arrives after the next ping has gone out is counted late, which only matters on a very bad link.
#[derive(Default)]
struct Health {
    rtts: VecDeque<u32>,
    results: VecDeque<bool>,
    outstanding: Option<Instant>,
    up_kbps: u32,
    down_kbps: u32,
    server: Option<ServerView>,
    desync: Option<DesyncView>,
    room: Option<(bool, u8)>,
}

impl Health {
    fn push<T>(q: &mut VecDeque<T>, v: T) {
        if q.len() == WINDOW {
            q.pop_front();
        }
        q.push_back(v);
    }

    fn ping_sent(&mut self, at: Instant) {
        if self.outstanding.is_some() {
            Self::push(&mut self.results, false); // the previous one never came back
        }
        self.outstanding = Some(at);
    }

    fn pong(&mut self, at: Instant) {
        if let Some(sent) = self.outstanding.take() {
            Self::push(&mut self.rtts, at.saturating_duration_since(sent).as_millis().min(u32::MAX as u128) as u32);
            Self::push(&mut self.results, true);
        }
    }

    fn view(&self) -> Diagnostics {
        let n = self.rtts.len() as u32;
        let avg = (n > 0).then(|| self.rtts.iter().sum::<u32>() / n);
        let jitter = (n > 1).then(|| self.rtts.iter().zip(self.rtts.iter().skip(1)).map(|(a, b)| a.abs_diff(*b)).sum::<u32>() / (n - 1));
        let lost = self.results.iter().filter(|r| !**r).count() as u32;
        let total = self.results.len() as u32;
        Diagnostics {
            rtt_ms: self.rtts.back().copied(),
            avg_ms: avg,
            jitter_ms: jitter,
            loss_pct: (total > 0).then(|| (lost * 100 + total / 2) / total),
            samples: total,
            up_kbps: self.up_kbps,
            down_kbps: self.down_kbps,
            server: self.server.clone(),
            server_identity: None,
            identity_saved: false,
            desync: self.desync.clone(),
            room_master: self.room.map(|r| r.0),
            ai_level: self.room.map(|r| r.1),
        }
    }
}

pub struct Agent {
    /// Where the engine should send its traffic (loopback).
    pub local: SocketAddr,
    health: Arc<Mutex<Health>>,
    status: Arc<Mutex<Status>>,
    progress: Arc<Mutex<Progress>>,
    address: String,
    issued: Arc<Mutex<Option<String>>>,
    /// The relay's identity key: the one we pinned, or the one learned on first contact.
    server_key: Arc<Mutex<Option<Key>>>,
    learned: Arc<Mutex<Option<Key>>>,
    pinned: bool,
    /// What the engine must present (`Packet::Auth`) before the agent listens to it.
    token: [u8; 16],
    /// Raised each time the launcher starts an engine: the engines of earlier games are forgotten (see `EngineSet`).
    engine_epoch: Arc<AtomicU32>,
    crypto: Arc<Mutex<Option<Arc<Crypto>>>>,
    relay: UdpSocket,
    tunnel_port: u16,
    /// This launch's Red Alert 2 token and the game endpoint that presented it (see `from_spawner`).
    tunnel_trust: Arc<Mutex<TunnelTrust>>,
    lobby: LobbyQueue,
    maps: MapQueue,
    stop: Arc<AtomicBool>,
    threads: Vec<JoinHandle<()>>,
}

/// The local addresses that proved (with the token) they belong to the engine we started. A game opens several sockets
/// (lobby, game, checksums); only a bounded number are admitted, and a new game starts from an empty set.
#[derive(Default)]
struct EngineSet {
    addrs: std::collections::HashSet<SocketAddr>,
    epoch: u32,
}

impl EngineSet {
    /// Returns true when a new game began since the last call (and the set was emptied).
    fn new_epoch(&mut self, epoch: u32) -> bool {
        if epoch == self.epoch {
            return false;
        }
        self.epoch = epoch;
        self.addrs.clear();
        true
    }
    fn admit(&mut self, from: SocketAddr) {
        if self.addrs.len() < 16 {
            self.addrs.insert(from);
        }
    }
    fn contains(&self, a: &SocketAddr) -> bool {
        self.addrs.contains(a)
    }
}

/// Send `p` encrypted, if a session exists.
fn send_sealed(relay: &UdpSocket, crypto: &Mutex<Option<Arc<Crypto>>>, p: &Packet) -> bool {
    let Some(c) = crypto.lock().unwrap().clone() else { return false };
    let _ = relay.send(&Wire::Transport { data: c.seal(p) }.encode());
    true
}

impl Agent {
    /// Joins `room` (creating it with `password` if it does not exist). An empty `serial` makes the agent sign
    /// up first; the serial it is issued is available once from `take_issued` and should be stored.
    ///
    /// Everything after the first contact is encrypted. `pinned` is the relay's identity key from an earlier visit:
    /// with it, only that relay can complete the join, and a different one is reported as `ServerKeyChanged`.
    /// Without it (first visit) the key is fetched and offered through `take_learned_key` to be saved.
    pub fn start(server: &str, serial: &str, room: &str, password: &str, pinned: Option<Key>) -> io::Result<Agent> {
        let raw = server;
        let server = server.to_socket_addrs().map_err(|e| io::Error::other(format!("can't find a server called {raw:?}: check the address ({e})")))?.next().ok_or_else(|| io::Error::other(format!("can't find a server called {raw:?}: check the address")))?;
        let relay = UdpSocket::bind("0.0.0.0:0")?;
        relay.connect(server)?; // also makes the kernel drop datagrams from anyone else
        relay.set_read_timeout(Some(TICK))?;
        let local = UdpSocket::bind("127.0.0.1:0")?;
        local.set_read_timeout(Some(TICK))?;
        let local_addr = local.local_addr()?;
        let tunnel = UdpSocket::bind("127.0.0.1:0")?;
        tunnel.set_read_timeout(Some(TICK))?;
        let tunnel_port = tunnel.local_addr()?.port();
        // nobody knows this one: nothing binds before `arm_tunnel`
        let tunnel_trust = Arc::new(Mutex::new(TunnelTrust { token: secure::random_token(), peer: None }));
        let lobby: LobbyQueue = Arc::default();
        let maps: MapQueue = Arc::default();

        let status = Arc::new(Mutex::new(Status::Connecting));
        let progress: Arc<Mutex<Progress>> = Arc::new(Mutex::new(Progress { started: Some(Instant::now()), stage: "contacting the server", ..Default::default() }));
        let address = server.to_string();
        let stop = Arc::new(AtomicBool::new(false));
        let issued: Arc<Mutex<Option<String>>> = Arc::default();
        let server_key: Arc<Mutex<Option<Key>>> = Arc::new(Mutex::new(pinned));
        let learned: Arc<Mutex<Option<Key>>> = Arc::default();
        let crypto: Arc<Mutex<Option<Arc<Crypto>>>> = Arc::default();
        let ports: Arc<Mutex<HashMap<u16, SocketAddr>>> = Arc::default();
        let health: Arc<Mutex<Health>> = Arc::default();
        // bytes to / from the relay, for the traffic rates
        let bytes: Arc<[AtomicU64; 2]> = Arc::new([AtomicU64::new(0), AtomicU64::new(0)]);
        let (serial, room, verifier) = (serial.to_string(), room.to_string(), room_verifier(room, password));
        let token = secure::random_token();
        let engine_epoch = Arc::new(AtomicU32::new(0));

        // local -> relay
        let l2r = {
            let (relay, local, status, stop, ports, crypto, bytes) =
                (relay.try_clone()?, local.try_clone()?, status.clone(), stop.clone(), ports.clone(), crypto.clone(), bytes.clone());
            let epoch = engine_epoch.clone();
            std::thread::spawn(move || {
                let mut buf = [0u8; 2048];
                // Local addresses that proved (with the token) they are the engine we started. Any other process on this
                // machine, even another user's, can send to the loopback port; it must not bind ports or inject traffic.
                let mut engine = EngineSet::default();
                while !stop.load(Ordering::Relaxed) {
                    let Ok((n, from)) = local.recv_from(&mut buf) else { continue };
                    // A new game was started: the sockets of the one before are gone, and so are the ports bound to them.
                    if engine.new_epoch(epoch.load(Ordering::Relaxed)) {
                        ports.lock().unwrap().clear();
                    }
                    match Packet::decode(&buf[..n]) {
                        Some(Packet::Auth { token: t }) => {
                            if t == token {
                                engine.admit(from);
                            }
                        }
                        Some(_) if !engine.contains(&from) => {}
                        // The tunnel is for the game only: its two ports and datagrams the engine made.
                        Some(Packet::Bind { port }) if GAME_PORTS.contains(&port) => {
                            ports.lock().unwrap().insert(port, from);
                        }
                        // The engine's own simulation checksum, for the relay to compare with the other players'.
                        Some(p @ Packet::Crc { .. }) => {
                            if matches!(*status.lock().unwrap(), Status::Connected(_)) {
                                send_sealed(&relay, &crypto, &p);
                            }
                        }
                        Some(p @ (Packet::Bcast { .. } | Packet::Data { .. })) => {
                            let (src_port, dst_port, payload, bcast) = match &p {
                                Packet::Bcast { src_port, dst_port, payload, .. } => (*src_port, *dst_port, payload, true),
                                Packet::Data { src_port, dst_port, payload, .. } => (*src_port, *dst_port, payload, false),
                                _ => unreachable!(),
                            };
                            let dst_ok = if bcast { dst_port == LOBBY_PORT } else { GAME_PORTS.contains(&dst_port) };
                            if !GAME_PORTS.contains(&src_port) || !dst_ok || !is_game_packet(payload) {
                                continue;
                            }
                            ports.lock().unwrap().insert(src_port, from);
                            if matches!(*status.lock().unwrap(), Status::Connected(_)) && send_sealed(&relay, &crypto, &p) {
                                bytes[0].fetch_add(n as u64, Ordering::Relaxed);
                            }
                        }
                        _ => {}
                    }
                }
            })
        };

        // Red Alert 2 game -> relay
        let t2r = {
            let (relay, tunnel, status, stop, crypto, bytes, peer) = (relay.try_clone()?, tunnel.try_clone()?, status.clone(), stop.clone(), crypto.clone(), bytes.clone(), tunnel_trust.clone());
            std::thread::spawn(move || {
                let mut buf = [0u8; 2048];
                while !stop.load(Ordering::Relaxed) {
                    let Ok((n, from)) = tunnel.recv_from(&mut buf) else { continue };
                    let own = match *status.lock().unwrap() {
                        Status::Connected(v) => Some(v[3]),
                        _ => None,
                    };
                    let what = from_spawner(own, &mut peer.lock().unwrap(), from, &buf[..n]);
                    match what {
                        FromSpawner::Authed => {
                            let _ = tunnel.send_to(&AUTH_PREFIX, from);
                        }
                        FromSpawner::Pong(echo) => {
                            let _ = tunnel.send_to(echo, from);
                        }
                        FromSpawner::Forward { to, payload } => {
                            if send_sealed(&relay, &crypto, &Packet::Tunnel { vip: to, payload: payload.to_vec() }) {
                                bytes[0].fetch_add(n as u64, Ordering::Relaxed);
                            }
                        }
                        FromSpawner::Ignore => {}
                    }
                }
            })
        };

        // relay -> local, plus handshake and keepalive
        let r2l = {
            let (relay, status, stop, issued, health, bytes, server_key, learned, crypto, progress) =
                (relay.try_clone()?, status.clone(), stop.clone(), issued.clone(), health.clone(), bytes.clone(), server_key.clone(), learned.clone(), crypto.clone(), progress.clone());
            let (tunnel, tunnel_trust, lobby, maps) = (tunnel, tunnel_trust.clone(), lobby.clone(), maps.clone());
            std::thread::spawn(move || {
                let mut serial = serial;
                let mut cookie: Cookie = [0; 16];
                // The first handshake message, kept so a lost reply is answered by resending the same bytes.
                let mut pending: Option<(Vec<u8>, ClientPending)> = None;
                let mut buf = [0u8; 2048];
                let (mut last_rx, mut last_ping, mut last_rate) = (Instant::now(), Instant::now(), (Instant::now(), 0u64, 0u64));
                let mut attempts = 0u32;
                let rejected = |r: u8| *status.lock().unwrap() = Status::Rejected(r);
                loop {
                    if stop.load(Ordering::Relaxed) {
                        return;
                    }
                    if let Ok(n) = relay.recv(&mut buf) {
                        last_rx = Instant::now();
                        progress.lock().unwrap().replies += 1;
                        match Wire::decode(&buf[..n]) {
                            // Unsigned, so only meaningful while we are still finding out who the server is. Once a session
                            // exists a forged one (spoofed source, guessed port) must not be able to drop it.
                            Some(Wire::KeyReply { .. }) if crypto.lock().unwrap().is_some() => {}
                            Some(Wire::KeyReply { key }) => {
                                let mut known = server_key.lock().unwrap();
                                match *known {
                                    // a different identity than the one we trust (saved earlier, or learned a moment ago)
                                    Some(k) if k != key => {
                                        rejected(Reason::ServerKeyChanged as u8);
                                        return;
                                    }
                                    None => {
                                        *known = Some(key);
                                        *learned.lock().unwrap() = Some(key);
                                    }
                                    _ => {}
                                }
                            }
                            // The first message is bound to this cookie (see `secure::client_start`), so a new cookie means building
                            // it again. Ignored once connected.
                            Some(Wire::Challenge { cookie: c }) if crypto.lock().unwrap().is_none() => {
                                if c != cookie {
                                    cookie = c;
                                    pending = None;
                                }
                            }
                            Some(Wire::Hs2 { msg }) => {
                                if let Some((_, p)) = pending.take() {
                                    match p.finish(&msg) {
                                        Ok((Packet::Welcome { vip }, c)) => {
                                            *crypto.lock().unwrap() = Some(Arc::new(c));
                                            *status.lock().unwrap() = Status::Connected(vip);
                                            {
                                                let mut p = progress.lock().unwrap();
                                                p.stage = "connected";
                                                match p.first_vip {
                                                    None => p.first_vip = Some(vip),
                                                    Some(first) if first != vip => p.address_changed = true,
                                                    _ => {}
                                                }
                                            }
                                            attempts = 0;
                                        }
                                        Ok((Packet::Issued { serial: s }, _)) if serial.is_empty() => {
                                            serial = s.clone();
                                            *issued.lock().unwrap() = Some(s); // next tick: a new handshake that joins
                                        }
                                        Ok((Packet::Reject(r), _)) => {
                                            rejected(r as u8);
                                            return;
                                        }
                                        _ => {} // unreadable or unexpected: start over
                                    }
                                }
                            }
                            Some(Wire::Transport { data }) => {
                                let session = crypto.lock().unwrap().clone();
                                match session.and_then(|c| c.open(&data)) {
                                    Some(Packet::Ping) => health.lock().unwrap().pong(Instant::now()),
                                    Some(Packet::Room { master, ai }) => health.lock().unwrap().room = Some((master, ai)),
                                    Some(Packet::Stats(s)) => {
                                        health.lock().unwrap().server = Some(ServerView {
                                            version: s.version,
                                            uptime_secs: s.uptime_secs,
                                            rooms: s.rooms,
                                            players: s.players,
                                            max_rooms: s.max_rooms,
                                            max_players: s.max_players,
                                            room_players: s.room_players,
                                            load_pct: s.load_pct,
                                            pps: s.pps,
                                            kbps: s.kbps,
                                        });
                                    }
                                    Some(p @ Packet::Desync(_)) => {
                                        if let Packet::Desync(v) = &p {
                                            health.lock().unwrap().desync = Some(DesyncView {
                                                frame: v.frame,
                                                certain: v.certain,
                                                host: v.host.map(|h| format!("{}.{}.{}.{}", h[0], h[1], h[2], h[3])),
                                                players: v.players.iter().map(|e| DesyncPlayer { vip: format!("{}.{}.{}.{}", e.vip[0], e.vip[1], e.vip[2], e.vip[3]), crc: format!("{:08X}", e.crc), odd: e.odd }).collect(),
                                            });
                                        }
                                        // The engine tells the player in the game: hand it to whichever of its sockets is bound.
                                        let to = { let ports = ports.lock().unwrap(); GAME_PORTS.iter().rev().find_map(|g| ports.get(g).copied()) };
                                        if let Some(to) = to {
                                            let _ = local.send_to(&p.encode(), to);
                                        }
                                    }
                                    // A relay (or anyone spoofing one) only gets to hand the engine game datagrams for its own two ports.
                                    Some(p @ (Packet::Bcast { .. } | Packet::Data { .. })) => {
                                        let (dst_port, payload) = match &p {
                                            Packet::Bcast { dst_port, payload, .. } | Packet::Data { dst_port, payload, .. } => (*dst_port, payload),
                                            _ => unreachable!(),
                                        };
                                        if GAME_PORTS.contains(&dst_port) && is_game_packet(payload) {
                                            if let Some(to) = ports.lock().unwrap().get(&dst_port) {
                                                bytes[1].fetch_add(n as u64, Ordering::Relaxed);
                                                let _ = local.send_to(&p.encode(), to);
                                            }
                                        }
                                    }
                                    Some(Packet::Tunnel { vip, payload }) => {
                                        let own = match *status.lock().unwrap() {
                                            Status::Connected(v) => Some(v[3]),
                                            _ => None,
                                        };
                                        let to = tunnel_trust.lock().unwrap().peer;
                                        if let (Some(own), Some(to)) = (own, to) {
                                            if let Some(d) = to_spawner(vip, own, &payload) {
                                                bytes[1].fetch_add(n as u64, Ordering::Relaxed);
                                                let _ = tunnel.send_to(&d, to);
                                            }
                                        }
                                    }
                                    Some(Packet::Lobby { vip, payload }) if vip[..3] == VIP_PREFIX => {
                                        let mut q = lobby.lock().unwrap();
                                        if q.len() == LOBBY_QUEUE {
                                            q.pop_front();
                                        }
                                        q.push_back((vip[3], payload));
                                    }
                                    // Map sharing: from another player in the room, or the relay's own `Caps` (address all zeros).
                                    Some(Packet::Map { vip, msg }) if vip[..3] == VIP_PREFIX || vip == [0; 4] => {
                                        let mut q = maps.lock().unwrap();
                                        if q.len() == MAP_QUEUE {
                                            q.pop_front();
                                        }
                                        q.push_back((vip, msg));
                                    }
                                    _ => {}
                                }
                            }
                            _ => {}
                        }
                    }
                    // periodic work (also runs after a read timeout)
                    let connected = matches!(*status.lock().unwrap(), Status::Connected(_));
                    if !connected {
                        attempts += 1;
                        let key = *server_key.lock().unwrap();
                        {
                            let mut p = progress.lock().unwrap();
                            if p.stage != "joining the room again" {
                                p.stage = if key.is_none() { "asking for the server's identity" } else { "signing in to the room" };
                            }
                        }
                        match key {
                            None => {
                                let _ = relay.send(&Wire::KeyRequest.encode()); // first visit: learn who we are talking to
                            }
                            Some(k) => {
                                if cookie == [0; 16] {
                                    // No cookie yet: a first message of the right size only asks for one, its content is ignored.
                                    let _ = relay.send(&Wire::Hs1 { cookie, msg: vec![0; 80] }.encode());
                                } else {
                                    if pending.is_none() {
                                        let first = if serial.is_empty() { Packet::Register } else { Packet::Hello { serial: serial.clone(), room: room.clone(), verifier } };
                                        if let Ok((msg, p)) = secure::client_start(&k, &first, &cookie) {
                                            pending = Some((msg, p));
                                        }
                                    }
                                    if let Some((msg, _)) = &pending {
                                        let _ = relay.send(&Wire::Hs1 { cookie, msg: msg.clone() }.encode());
                                    }
                                }
                                // A pinned server that does not answer may be a different one: ask who it is.
                                if pinned.is_some() && attempts % 4 == 0 {
                                    let _ = relay.send(&Wire::KeyRequest.encode());
                                }
                            }
                        }
                    } else if last_rx.elapsed() > SILENCE_LIMIT {
                        // the relay forgot us (restart or timeout): join again from the start
                        *status.lock().unwrap() = Status::Connecting;
                        *crypto.lock().unwrap() = None;
                        pending = None;
                        {
                            let mut p = progress.lock().unwrap();
                            p.reconnects += 1;
                            p.stage = "joining the room again";
                            p.started = Some(Instant::now());
                            p.replies = 0;
                        }
                    } else if last_ping.elapsed() > PING_EVERY {
                        health.lock().unwrap().ping_sent(Instant::now());
                        send_sealed(&relay, &crypto, &Packet::Ping);
                        send_sealed(&relay, &crypto, &Packet::StatsRequest);
                        last_ping = Instant::now();
                    }
                    // traffic rates, refreshed about once a second
                    let since = last_rate.0.elapsed();
                    if since >= Duration::from_secs(1) {
                        let (up, down) = (bytes[0].load(Ordering::Relaxed), bytes[1].load(Ordering::Relaxed));
                        let kbps = |now: u64, before: u64| ((now - before) as f64 * 8.0 / 1000.0 / since.as_secs_f64()).round() as u32;
                        let mut h = health.lock().unwrap();
                        (h.up_kbps, h.down_kbps) = (kbps(up, last_rate.1), kbps(down, last_rate.2));
                        last_rate = (Instant::now(), up, down);
                    }
                }
            })
        };

        Ok(Agent { local: local_addr, health, status, progress, address, issued, server_key, learned, pinned: pinned.is_some(), token, engine_epoch, crypto, relay, tunnel_port, tunnel_trust, lobby, maps, stop, threads: vec![l2r, t2r, r2l] })
    }

    /// Where the connection is and what to check, for the Online page.
    pub fn connection(&self) -> Connection {
        let p = self.progress.lock().unwrap();
        let waited = p.started.map(|t| t.elapsed().as_secs()).unwrap_or(0);
        let stage = if matches!(*self.status.lock().unwrap(), Status::Connected(_)) { "connected" } else { p.stage };
        Connection {
            address: self.address.clone(),
            stage: stage.to_string(),
            waited_secs: waited,
            replies: p.replies,
            reconnects: p.reconnects,
            address_changed: p.address_changed,
            hint: connect_hint(stage, p.replies, waited, &self.address),
        }
    }

    /// The launcher is starting an engine: forget the ones of earlier games. Each engine opens several sockets and the
    /// agent only listens to a few, so after a couple of games without reconnecting the next game's sockets were refused:
    /// the lobby worked (its sockets came first) and the game never loaded.
    pub fn new_engine(&self) {
        self.engine_epoch.fetch_add(1, Ordering::Relaxed);
    }

    /// The loopback port of the Red Alert 2 tunnel: the spawn.ini `[Tunnel] Ip=127.0.0.1 Port=` value. The player's own
    /// tunnel id (`[Settings] Port`) and each other player's (`[OtherN] Port`) are the last octet of their virtual address.
    pub fn tunnel_port(&self) -> u16 {
        self.tunnel_port
    }

    /// A Red Alert 2 game is about to start: forget the last one and make a new token, returned for the launch file
    /// (`[Tunnel] Token=`). The tunnel then belongs to the first local endpoint that presents it.
    pub fn arm_tunnel(&self) -> [u8; 16] {
        let token = secure::random_token();
        *self.tunnel_trust.lock().unwrap() = TunnelTrust { token, peer: None };
        token
    }

    /// Hand a blob (at most 1024 bytes) to every other player in the room, for the launcher's Red Alert 2 lobby.
    /// False if it is too big or the agent is not connected.
    pub fn send_lobby(&self, blob: &[u8]) -> bool {
        blob.len() <= MAX_LOBBY_PAYLOAD && matches!(self.status(), Status::Connected(_)) && send_sealed(&self.relay, &self.crypto, &Packet::Lobby { vip: [0; 4], payload: blob.to_vec() })
    }

    /// Lobby blobs received since the last call, oldest first, each with the last octet of its sender's virtual address.
    pub fn take_lobby(&self) -> Vec<(u8, Vec<u8>)> {
        self.lobby.lock().unwrap().drain(..).collect()
    }

    /// Say something about maps to one player (`to`), or to the whole room (all zeros, for `Hi`). False if not connected.
    pub fn send_map(&self, to: Vip, msg: MapMsg) -> bool {
        matches!(self.status(), Status::Connected(_)) && send_sealed(&self.relay, &self.crypto, &Packet::Map { vip: to, msg })
    }

    /// Map messages received since the last call, oldest first, each with the virtual address the relay says it came from
    /// (all zeros: the relay itself).
    pub fn take_map(&self) -> Vec<(Vip, MapMsg)> {
        self.maps.lock().unwrap().drain(..).collect()
    }

    /// The token to give the engine (`GEN_RELAY_TOKEN`), in hex.
    pub fn token_hex(&self) -> String {
        self.token.iter().map(|b| format!("{b:02x}")).collect()
    }

    /// The serial the server issued to us, once; the caller must store it.
    pub fn take_issued(&self) -> Option<String> {
        self.issued.lock().unwrap().take()
    }

    /// The relay's identity key if this was the first visit, once; the caller should save it so later visits are checked.
    pub fn take_learned_key(&self) -> Option<Key> {
        self.learned.lock().unwrap().take()
    }

    pub fn status(&self) -> Status {
        *self.status.lock().unwrap()
    }

    /// The room master asks for a "Stronger AI" level (0 to 3) for everyone; the relay ignores anyone else.
    pub fn set_ai(&self, level: u8) {
        send_sealed(&self.relay, &self.crypto, &Packet::SetAi { level: level.min(generals_proto::MAX_AI_LEVEL) });
    }

    /// The level the room plays with, once the relay has said (a second or two after joining).
    pub fn room_ai(&self) -> Option<u8> {
        self.health.lock().unwrap().room.map(|r| r.1)
    }

    /// Latency, loss, traffic and the relay's own numbers; `None` unless connected.
    pub fn diagnostics(&self) -> Option<Diagnostics> {
        if !matches!(self.status(), Status::Connected(_)) {
            return None;
        }
        let mut d = self.health.lock().unwrap().view();
        d.server_identity = self.server_key.lock().unwrap().as_ref().map(secure::fingerprint);
        d.identity_saved = self.pinned;
        Some(d)
    }
}

impl Drop for Agent {
    fn drop(&mut self) {
        // Tell the server now so an empty room disappears immediately instead of after the session timeout.
        if matches!(self.status(), Status::Connected(_)) {
            send_sealed(&self.relay, &self.crypto, &Packet::Leave);
        }
        self.stop.store(true, Ordering::Relaxed);
        for t in self.threads.drain(..) {
            let _ = t.join(); // threads wake within one TICK
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_new_game_gets_its_sockets_admitted_however_many_games_came_before() {
        let sock = |p: u16| -> SocketAddr { format!("127.0.0.1:{p}").parse().unwrap() };
        let mut e = EngineSet::default();
        // four games of four sockets each, without reconnecting: the old limit of eight refused the third game
        for game in 1..=4u32 {
            assert_eq!(e.new_epoch(game), true);
            for k in 0..4u16 {
                let a = sock(40000 + game as u16 * 10 + k);
                e.admit(a);
                assert!(e.contains(&a), "game {game} socket {k}");
            }
            assert!(!e.contains(&sock(40000 + (game as u16 - 1) * 10)) || game == 1, "the earlier game's socket is forgotten");
            assert!(!e.new_epoch(game));
        }
        // a flood from one process is still bounded
        for p in 0..100u16 {
            e.admit(sock(50000 + p));
        }
        assert!(e.addrs.len() <= 16);
    }

    fn authed(game: SocketAddr) -> TunnelTrust {
        let mut t = TunnelTrust { token: [9; 16], peer: None };
        assert_eq!(from_spawner(None, &mut t, game, &[&AUTH_PREFIX[..], &[9; 16]].concat()), FromSpawner::Authed);
        t
    }

    #[test]
    fn spawner_header_becomes_a_tunnel_packet_and_back() {
        let game: SocketAddr = "127.0.0.1:5000".parse().unwrap();
        let mut trust = authed(game);
        let g = generals_proto::ra2_game_packet(b"frame");
        // our id is 3: to id 2 goes to 10.77.0.2 (header big-endian, as htons writes it)
        let d = [&[0, 3, 0, 2][..], &g].concat();
        assert_eq!(from_spawner(Some(3), &mut trust, game, &d), FromSpawner::Forward { to: [10, 77, 0, 2], payload: &g });
        // what arrives from 10.77.0.2 for us
        assert_eq!(to_spawner([10, 77, 0, 2], 3, &g), Some([&[0, 2, 0, 3][..], &g].concat()));
        assert_eq!(to_spawner([192, 168, 0, 2], 3, &g), None, "not a room address");
        assert_eq!(to_spawner([10, 77, 0, 2], 3, &[]), None);
        // a ping: 50 bytes of sender 0, receiver 0, answered with its first 12, even before connecting
        let mut ping = vec![0u8; 50];
        ping[4..12].copy_from_slice(b"pingpong");
        assert_eq!(from_spawner(None, &mut trust, game, &ping), FromSpawner::Pong(&ping[..12]));
        assert_eq!(from_spawner(None, &mut trust, game, &ping[..49]), FromSpawner::Ignore);
    }

    #[test]
    fn only_game_datagrams_pass_the_tunnel_either_way() {
        let game: SocketAddr = "127.0.0.1:5000".parse().unwrap();
        let mut trust = authed(game);
        let g = generals_proto::ra2_game_packet(b"frame");
        let mut bad = g.clone();
        bad[0] ^= 1;
        let mut big = vec![0u8; generals_proto::MAX_TUNNEL_PAYLOAD];
        big[..g.len()].copy_from_slice(&g);
        for p in [bad, big, vec![1], b"GET / HTTP/1.1\r\n\r\n".to_vec(), generals_proto::game_packet(b"Generals")] {
            assert_eq!(from_spawner(Some(3), &mut trust, game, &[&[0, 3, 0, 2][..], &p].concat()), FromSpawner::Ignore, "{p:?}");
            assert_eq!(to_spawner([10, 77, 0, 2], 3, &p), None, "{p:?}");
        }
        // the engine's keep-alive (receiver 0xFFFE, one zero byte) stays here too
        assert_eq!(from_spawner(Some(3), &mut trust, game, &[0, 3, 0xFF, 0xFE, 0]), FromSpawner::Ignore);
    }

    #[test]
    fn the_tunnel_belongs_to_the_endpoint_with_the_token() {
        let (game, other): (SocketAddr, SocketAddr) = ("127.0.0.1:5000".parse().unwrap(), "127.0.0.1:5001".parse().unwrap());
        let ok = [&[0, 3, 0, 2][..], &generals_proto::ra2_game_packet(b"x")].concat();
        let auth = |t: &[u8]| [&AUTH_PREFIX[..], t].concat();
        let mut trust = TunnelTrust { token: [9; 16], peer: None };
        // before the token nothing is heard, not even a ping; a wrong, short or long token, or one from elsewhere, binds nothing
        assert_eq!(from_spawner(Some(3), &mut trust, game, &ok), FromSpawner::Ignore);
        assert_eq!(from_spawner(Some(3), &mut trust, game, &[0; 50]), FromSpawner::Ignore);
        for bad in [auth(&[8; 16]), auth(&[9; 15]), auth(&[9; 17]), AUTH_PREFIX.to_vec()] {
            assert_eq!(from_spawner(Some(3), &mut trust, game, &bad), FromSpawner::Ignore, "{bad:?}");
        }
        assert_eq!(from_spawner(Some(3), &mut trust, "192.168.1.9:5000".parse().unwrap(), &auth(&[9; 16])), FromSpawner::Ignore);
        assert_eq!(trust.peer, None);
        // the game presents it (again: its resends are answered too); a spoofing local endpoint is then ignored, token or not
        assert_eq!(from_spawner(Some(3), &mut trust, game, &auth(&[9; 16])), FromSpawner::Authed);
        assert_eq!(from_spawner(Some(3), &mut trust, game, &auth(&[9; 16])), FromSpawner::Authed);
        assert!(matches!(from_spawner(Some(3), &mut trust, game, &ok), FromSpawner::Forward { .. }));
        assert_eq!(from_spawner(Some(3), &mut trust, other, &auth(&[9; 16])), FromSpawner::Ignore);
        assert_eq!(from_spawner(Some(3), &mut trust, other, &ok), FromSpawner::Ignore);
        assert_eq!(from_spawner(Some(3), &mut trust, other, &[0; 50]), FromSpawner::Ignore);
        // checks after the token: not connected (no id), someone else's id, our own id as receiver, id 0, id above 255
        assert_eq!(from_spawner(None, &mut trust, game, &ok), FromSpawner::Ignore);
        for hdr in [[0, 4, 0, 2], [0, 3, 0, 3], [0, 3, 0, 0], [0, 3, 1, 0]] {
            let d = [&hdr[..], &ok[4..]].concat();
            assert_eq!(from_spawner(Some(3), &mut trust, game, &d), FromSpawner::Ignore, "{hdr:?}");
        }
        // re-armed (`arm_tunnel`: a new token, no endpoint): the old game and the old token are both refused now
        trust = TunnelTrust { token: [7; 16], peer: None };
        assert_eq!(from_spawner(Some(3), &mut trust, game, &ok), FromSpawner::Ignore);
        assert_eq!(from_spawner(Some(3), &mut trust, game, &auth(&[9; 16])), FromSpawner::Ignore, "replayed old token");
        assert_eq!(from_spawner(Some(3), &mut trust, other, &auth(&[7; 16])), FromSpawner::Authed);
        assert!(matches!(from_spawner(Some(3), &mut trust, other, &ok), FromSpawner::Forward { .. }));
    }

    #[test]
    fn explains_why_it_cannot_connect() {
        assert_eq!(connect_hint("contacting the server", 0, 3, "1.2.3.4:7777"), "", "too early to say");
        assert!(connect_hint("contacting the server", 0, 8, "1.2.3.4:7777").contains("Nothing has answered from 1.2.3.4:7777"));
        assert!(connect_hint("asking for the server's identity", 2, 8, "x").contains("may not be a Rallypoint server"));
        assert!(connect_hint("signing in to the room", 5, 20, "x").contains("signing in to the room has not finished"));
        assert_eq!(connect_hint("connected", 9, 100, "x"), "");
    }


    #[test]
    fn latency_jitter_and_loss_from_pings() {
        let mut h = Health::default();
        let t0 = Instant::now();
        assert_eq!(h.view(), Diagnostics::default());
        for (i, ms) in [40u64, 60, 40, 60].into_iter().enumerate() {
            let sent = t0 + Duration::from_secs(2 * i as u64);
            h.ping_sent(sent);
            h.pong(sent + Duration::from_millis(ms));
        }
        let v = h.view();
        assert_eq!((v.rtt_ms, v.avg_ms, v.jitter_ms, v.loss_pct, v.samples), (Some(60), Some(50), Some(20), Some(0), 4));
        // one ping gets no answer: it is counted when the next one goes out
        h.ping_sent(t0 + Duration::from_secs(10));
        h.ping_sent(t0 + Duration::from_secs(12));
        assert_eq!(h.view().loss_pct, Some(20), "1 of 5");
        // a stray answer with nothing outstanding changes nothing
        h.pong(t0 + Duration::from_secs(13));
        h.pong(t0 + Duration::from_secs(14));
        assert_eq!(h.view().samples, 6);
    }

    #[test]
    fn history_is_a_rolling_window() {
        let mut h = Health::default();
        let t0 = Instant::now();
        for i in 0..100u64 {
            h.ping_sent(t0 + Duration::from_secs(i));
            if i % 2 == 0 {
                h.pong(t0 + Duration::from_secs(i) + Duration::from_millis(30));
            }
        }
        let v = h.view();
        assert_eq!(v.samples as usize, WINDOW);
        assert_eq!(v.loss_pct, Some(50));
    }
}
