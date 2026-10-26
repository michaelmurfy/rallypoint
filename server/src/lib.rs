//! Relay state machine. No sockets or clocks in here: `handle` takes the sender and the time and returns
//! the datagrams to send, so it is testable without a network.
use generals_proto::secure::{self, Key, Session as Crypto};
use generals_proto::{ct_eq, is_game_packet, sha256, Cookie, Packet, Reason, ServerStats, SyncEntry, SyncVerdict, Verifier, Vip, Wire, GAME_PORTS, LOBBY_PORT, MAX_SYNC_PLAYERS};
pub mod geo;

use std::collections::{BTreeMap, HashMap, HashSet};
use std::net::{IpAddr, SocketAddr};

pub const SESSION_TIMEOUT_SECS: u64 = 30;
pub const MAX_ROOMS: usize = 256;
pub const ROOM_SIZE: usize = 8; // Generals' player limit
/// Wrong room passwords allowed per source address per window before it is cut off.
const PASSWORD_TRIES: u32 = 10;
const PASSWORD_WINDOW_SECS: u64 = 600;
pub const REGISTRATIONS_PER_HOUR: usize = 5;
/// Sign-ups the whole server will issue per hour, however many addresses ask (an attacker with many addresses,
/// e.g. a whole IPv6 range, must not be able to fill the serial file).
pub const REGISTRATIONS_PER_HOUR_TOTAL: usize = 300;
/// Sessions from one address (a household or LAN party sits behind one).
pub const SESSIONS_PER_IP: usize = 16;
/// Unknown serials one address may try per window before it is cut off (serials are 64 random bits).
const SERIAL_TRIES: u32 = 20;
/// Per session and second: more than this is a flood, and is dropped. A game uses a small fraction.
pub const SESSION_PACKETS_PER_SEC: u32 = 600;
pub const SESSION_BYTES_PER_SEC: u32 = 400_000;
/// A Register retry from the same address within this window gets the same serial back (UDP loss).
const REGISTER_DEDUP_SECS: u64 = 60;
const VIP_PREFIX: [u8; 3] = [10, 77, 0];

/// Serial file: one `SERIAL [label]` per line; a leading `!` marks it revoked. `#` lines are comments.
#[derive(Default, Clone)]
pub struct Serials {
    pub active: HashSet<String>,
    pub revoked: HashSet<String>,
}

impl Serials {
    pub fn parse(text: &str) -> Serials {
        let mut s = Serials::default();
        for l in text.lines().map(str::trim).filter(|l| !l.is_empty() && !l.starts_with('#')) {
            let first = l.split_whitespace().next().unwrap_or("");
            match first.strip_prefix('!') {
                Some(r) => s.revoked.insert(r.to_string()),
                None => s.active.insert(first.to_string()),
            };
        }
        s
    }
}

/// `XXXX-XXXX-XXXX-XXXX` from 8 random bytes (64 bits is plenty against online guessing at UDP rates).
pub fn format_serial(b: [u8; 8]) -> String {
    let h: String = b.iter().map(|x| format!("{x:02X}")).collect();
    format!("{}-{}-{}-{}", &h[0..4], &h[4..8], &h[8..12], &h[12..16])
}

pub type Rng = Box<dyn FnMut() -> Option<[u8; 8]> + Send>;

/// 64 random bits from the OS (Linux/macOS).
pub fn os_random() -> Option<[u8; 8]> {
    use std::io::Read;
    let mut b = [0u8; 8];
    std::fs::File::open("/dev/urandom").ok()?.read_exact(&mut b).ok()?;
    Some(b)
}

struct Session {
    serial: String,
    room: String,
    vip: Vip,
    /// Keys for this client's encrypted traffic.
    crypto: Crypto,
    last_seen: u64,
    /// Packets and bytes seen from this session in second `window`.
    window: u64,
    pkts: u32,
    bytes: u32,
}

/// Datagrams to send, already encrypted.
type Out = Vec<(SocketAddr, Vec<u8>)>;

/// Frames of checksums kept per room while waiting for every player's report, and verdicts sent per room (a game that
/// has diverged stays diverged; one clear report is enough).
const SYNC_FRAMES_KEPT: usize = 16;
const SYNC_VERDICTS_PER_ROOM: u8 = 3;

/// What the players of one room have reported about their simulation.
#[derive(Default)]
struct RoomSync {
    /// frame -> (player -> (checksum, claims to be the host))
    frames: BTreeMap<u32, HashMap<Vip, (u32, bool)>>,
    verdicts: u8,
    /// Frames every player agreed on, and the last one.
    agreed: u32,
    last_agreed: u32,
}

/// Handshakes one address (or IPv6 /64) may start per minute; each costs the server a key exchange.
const HANDSHAKES_PER_MIN: u32 = 30;
/// A repeated first message within this long is answered from memory instead of starting another session.
const HANDSHAKE_CACHE_SECS: u64 = 20;
const HANDSHAKE_CACHE_MAX: usize = 4096;

/// Rooms are created by the first player to join and live only while someone is in them.
pub struct Server {
    serials: Serials,
    sessions: HashMap<SocketAddr, Session>,
    rooms: HashMap<String, Verifier>,
    open: bool,
    rng: Rng,
    secret: [u8; 32],
    /// The server's long-term identity (private, public); clients pin the public half.
    identity: (Key, Key),
    hs_cache: HashMap<SocketAddr, (u64, [u8; 32], Vec<u8>)>,
    hs_rate: HashMap<IpAddr, (u64, u32)>,
    allow: Box<dyn Fn(IpAddr) -> bool + Send>,
    recent: HashMap<SocketAddr, (u64, String)>,
    by_ip: HashMap<IpAddr, Vec<u64>>,
    bad_passwords: HashMap<IpAddr, (u64, u32)>,
    new_serials: Vec<String>,
    sync: HashMap<String, RoomSync>,
    all_signups: Vec<u64>,
    bad_serials: HashMap<IpAddr, (u64, u32)>,
    version: String,
    /// Relay load: totals for the second in progress (`sec`), and the last full second.
    sec: u64,
    sec_pkts: u32,
    sec_bytes: u32,
    last_pps: u32,
    last_kbps: u32,
    /// Lines for the operator (joins, leaves, rejects, desyncs). The library does no I/O; `main` prints them.
    events: Vec<String>,
}

/// Address used for per-source limits. An IPv6 user typically owns a whole /64, so that is the unit; an IPv4-mapped
/// IPv6 address counts as the IPv4 one.
fn rate_key(ip: IpAddr) -> IpAddr {
    match ip {
        IpAddr::V6(a) => match a.to_ipv4_mapped() {
            Some(v4) => IpAddr::V4(v4),
            None => {
                let mut o = a.octets();
                o[8..].fill(0);
                IpAddr::V6(o.into())
            }
        },
        v4 => v4,
    }
}

fn fmt_vip(v: Vip) -> String {
    format!("{}.{}.{}.{}", v[0], v[1], v[2], v[3])
}

fn valid_room(name: &str) -> bool {
    !name.is_empty()
        && name.len() <= generals_proto::MAX_ROOM
        && name == name.trim()
        && name.bytes().all(|b| b.is_ascii_alphanumeric() || b" _.-".contains(&b))
}

fn ip_bytes(ip: IpAddr) -> Vec<u8> {
    match ip {
        IpAddr::V4(a) => a.octets().to_vec(),
        IpAddr::V6(a) => a.octets().to_vec(),
    }
}

impl Server {
    /// Open registration: clients without a serial are issued one automatically.
    pub fn new(serials: Serials) -> Server {
        let mut s = Server {
            serials,
            sessions: HashMap::new(),
            rooms: HashMap::new(),
            open: true,
            rng: Box::new(os_random),
            secret: [0; 32],
            identity: secure::generate_keypair(),
            hs_cache: HashMap::new(),
            hs_rate: HashMap::new(),
            allow: Box::new(|_| true),
            recent: HashMap::new(),
            by_ip: HashMap::new(),
            bad_passwords: HashMap::new(),
            new_serials: vec![],
            sync: HashMap::new(),
            all_signups: vec![],
            bad_serials: HashMap::new(),
            version: String::new(),
            sec: 0,
            sec_pkts: 0,
            sec_bytes: 0,
            last_pps: 0,
            last_kbps: 0,
            events: vec![],
        };
        s.reseed();
        s
    }

    /// New cookie secret from the current rng. If there is no entropy the secret stays zero and sign-ups are refused.
    fn reseed(&mut self) {
        let mut secret = [0u8; 32];
        for chunk in secret.chunks_mut(8) {
            match (self.rng)() {
                Some(b) => chunk.copy_from_slice(&b),
                None => {
                    self.secret = [0; 32];
                    return;
                }
            }
        }
        self.secret = secret;
    }

    /// Something worth a line in the log. Capped so a flood cannot grow it without bound between drains.
    fn note(&mut self, msg: String) {
        if self.events.len() < 500 {
            self.events.push(msg);
        }
    }

    pub fn take_events(&mut self) -> Vec<String> {
        std::mem::take(&mut self.events)
    }

    /// `false` = only admin-issued serials (Register is refused).
    pub fn set_open(&mut self, open: bool) {
        self.open = open;
    }

    pub fn set_rng(&mut self, rng: Rng) {
        self.rng = rng;
        self.reseed();
    }

    /// Who may start a session or sign up (e.g. a country filter). Existing sessions are not re-checked.
    pub fn set_allow(&mut self, allow: Box<dyn Fn(IpAddr) -> bool + Send>) {
        self.allow = allow;
    }

    /// Serials auto-issued since the last call; the caller must persist them.
    pub fn take_new_serials(&mut self) -> Vec<String> {
        std::mem::take(&mut self.new_serials)
    }

    /// Use a stored identity instead of the one made at start-up.
    pub fn set_identity(&mut self, private: Key, public: Key) {
        self.identity = (private, public);
    }

    pub fn public_key(&self) -> Key {
        self.identity.1
    }

    /// Shown to clients in their diagnostics.
    pub fn set_version(&mut self, v: &str) {
        self.version = v.chars().filter(|c| !c.is_control()).take(generals_proto::MAX_VERSION).collect();
    }

    pub fn room_count(&self) -> usize {
        self.rooms.len()
    }

    /// Replace the serial list (e.g. after the file changed). Sessions whose serial is no longer valid are dropped.
    pub fn set_serials(&mut self, serials: Serials) {
        self.sessions.retain(|_, s| serials.active.contains(&s.serial) && !serials.revoked.contains(&s.serial));
        self.serials = serials;
        self.gc_rooms();
    }

    pub fn expire(&mut self, now: u64) {
        let timed_out: Vec<SocketAddr> = self.sessions.iter().filter(|(_, s)| now.saturating_sub(s.last_seen) >= SESSION_TIMEOUT_SECS).map(|(a, _)| *a).collect();
        for a in timed_out {
            if let Some(s) = self.sessions.remove(&a) {
                self.note(format!("timeout: {} left room '{}' ({}, silent for {SESSION_TIMEOUT_SECS}s)", fmt_vip(s.vip), s.room, a));
            }
        }
        self.gc_rooms();
        self.recent.retain(|_, (t, _)| now.saturating_sub(*t) < REGISTER_DEDUP_SECS);
        self.by_ip.retain(|_, h| {
            h.retain(|t| now.saturating_sub(*t) < 3600);
            !h.is_empty()
        });
        self.bad_passwords.retain(|_, (t, _)| now.saturating_sub(*t) < PASSWORD_WINDOW_SECS);
        self.bad_serials.retain(|_, (t, _)| now.saturating_sub(*t) < PASSWORD_WINDOW_SECS);
        self.all_signups.retain(|t| now.saturating_sub(*t) < 3600);
        self.hs_cache.retain(|_, (t, _, _)| now.saturating_sub(*t) < HANDSHAKE_CACHE_SECS);
        self.hs_rate.retain(|_, (t, _)| now.saturating_sub(*t) < 60);
        self.roll(now);
    }

    /// Close the load window when the second changes; a gap of more than a second means it was idle.
    fn roll(&mut self, now: u64) {
        if now != self.sec {
            let idle = now > self.sec + 1;
            self.last_pps = if idle { 0 } else { self.sec_pkts };
            self.last_kbps = if idle { 0 } else { (self.sec_bytes as u64 * 8).div_ceil(1000).min(u32::MAX as u64) as u32 };
            self.sec = now;
            self.sec_pkts = 0;
            self.sec_bytes = 0;
        }
    }

    fn stats(&self, now: u64, room: &str) -> ServerStats {
        let players = self.sessions.len();
        let max_players = MAX_ROOMS * ROOM_SIZE;
        let pct = |n: usize, max: usize| (n * 100).div_ceil(max.max(1)).min(100) as u8; // anything in use shows at least 1%
        ServerStats {
            uptime_secs: now.min(u32::MAX as u64) as u32,
            rooms: self.rooms.len() as u16,
            players: players as u16,
            max_rooms: MAX_ROOMS as u16,
            max_players: max_players as u16,
            room_players: self.sessions.values().filter(|s| s.room == room).count() as u8,
            load_pct: pct(self.rooms.len(), MAX_ROOMS).max(pct(players, max_players)),
            pps: self.last_pps,
            kbps: self.last_kbps,
            version: self.version.clone(),
        }
    }

    /// A room exists only while it has members.
    fn gc_rooms(&mut self) {
        let used: HashSet<&String> = self.sessions.values().map(|s| &s.room).collect();
        let gone: Vec<String> = self.rooms.keys().filter(|n| !used.contains(*n)).cloned().collect();
        let mut lines = vec![];
        for name in &gone {
            let (agreed, last, verdicts) = self.sync.get(name).map(|r| (r.agreed, r.last_agreed, r.verdicts)).unwrap_or_default();
            lines.push(format!("room '{name}' closed: {agreed} checked frames agreed (last {last}), {verdicts} desync verdicts"));
        }
        self.rooms.retain(|name, _| used.contains(name));
        self.sync.retain(|name, _| used.contains(name));
        for l in lines {
            self.note(l);
        }
    }

    fn cookie(&self, ip: IpAddr, bucket: u64) -> Cookie {
        let h = sha256(&[&self.secret, &ip_bytes(ip), &bucket.to_be_bytes()]);
        h[..16].try_into().unwrap()
    }

    /// One datagram in, the datagrams to send out. Anything that is not a valid handshake or authentic
    /// encrypted traffic gets no answer at all.
    pub fn handle(&mut self, from: SocketAddr, data: &[u8], now: u64) -> Out {
        self.roll(now);
        match Wire::decode(data) {
            Some(Wire::KeyRequest) if (self.allow)(from.ip()) => vec![(from, Wire::KeyReply { key: self.identity.1 }.encode())],
            Some(Wire::Hs1 { cookie, msg }) if (self.allow)(from.ip()) => self.handshake(from, cookie, msg, now),
            Some(Wire::Transport { data }) => self.transport(from, &data, now),
            _ => vec![], // everything else, server-to-client packets included, is never valid here
        }
    }

    /// First handshake message. Before any key exchange is spent on a sender, it must echo a cookie bound to its
    /// address (so spoofed floods cost one hash and a small reply), and stay under a per-address rate.
    fn handshake(&mut self, from: SocketAddr, cookie: Cookie, msg: Vec<u8>, now: u64) -> Out {
        if self.secret == [0; 32] {
            return vec![]; // no entropy: refuse rather than hand out guessable cookies or serials
        }
        let bucket = now / 60;
        let fresh = self.cookie(from.ip(), bucket);
        if !(cookie == fresh || (bucket > 0 && cookie == self.cookie(from.ip(), bucket - 1))) {
            return vec![(from, Wire::Challenge { cookie: fresh }.encode())];
        }
        // A retransmission (our reply was lost) gets the same reply, not a second session.
        let digest = sha256(&[&msg]);
        if let Some((t, d, reply)) = self.hs_cache.get(&from) {
            if *d == digest && now.saturating_sub(*t) < HANDSHAKE_CACHE_SECS {
                return vec![(from, reply.clone())];
            }
        }
        let e = self.hs_rate.entry(rate_key(from.ip())).or_insert((now, 0));
        if now.saturating_sub(e.0) >= 60 {
            *e = (now, 0);
        }
        if e.1 >= HANDSHAKES_PER_MIN {
            return vec![];
        }
        e.1 += 1;
        let Ok((first, pending)) = secure::server_accept(&self.identity.0, &msg) else {
            return vec![]; // not encrypted to our key
        };
        let (answer, join) = match first {
            Packet::Register => {
                let a = self.register(from, now);
                match &a {
                    Packet::Issued { .. } => self.note(format!("sign-up: serial issued to {}", from.ip())),
                    Packet::Reject(r) => self.note(format!("sign-up from {} refused: {r:?}", from.ip())),
                    _ => {}
                }
                (a, None)
            }
            Packet::Hello { serial, room, verifier } => match self.hello(from, serial, room, verifier, now) {
                Ok(join) => (Packet::Welcome { vip: join.vip }, Some(join)),
                Err(r) => {
                    self.note(format!("join from {} refused: {r:?}", from.ip()));
                    (Packet::Reject(r), None)
                }
            },
            _ => return vec![],
        };
        let Ok((reply, crypto)) = pending.reply(&answer) else { return vec![] };
        if let Some(j) = join {
            let new_room = self.rooms.insert(j.room.clone(), j.verifier).is_none();
            let n = self.sessions.values().filter(|s| s.room == j.room).count() + 1;
            let tail = &j.serial[j.serial.len().saturating_sub(4)..];
            self.note(format!("join: {} (serial ...{tail}, {}) {} room '{}', {n} in it", fmt_vip(j.vip), from, if new_room { "opened" } else { "entered" }, j.room));
            self.sessions.insert(from, Session { serial: j.serial, room: j.room, vip: j.vip, crypto, last_seen: now, window: now, pkts: 0, bytes: 0 });
        }
        let reply = Wire::Hs2 { msg: reply }.encode();
        if self.hs_cache.len() < HANDSHAKE_CACHE_MAX {
            self.hs_cache.insert(from, (now, digest, reply.clone()));
        }
        vec![(from, reply)]
    }

    /// Encrypted traffic from a client that has a session.
    fn transport(&mut self, from: SocketAddr, data: &[u8], now: u64) -> Out {
        let Some(me) = self.sessions.get_mut(&from) else {
            return vec![]; // unknown sender: no reply (the agent re-joins when its pings go unanswered)
        };
        // Flood limit per session, before any decryption is spent on it; dropped packets do not keep it alive.
        if me.window != now {
            (me.window, me.pkts, me.bytes) = (now, 0, 0);
        }
        let size = data.len() as u32;
        if me.pkts >= SESSION_PACKETS_PER_SEC || me.bytes.saturating_add(size) > SESSION_BYTES_PER_SEC {
            return vec![];
        }
        me.pkts += 1;
        me.bytes += size;
        let Some(pkt) = me.crypto.open(data) else {
            return vec![]; // forged, damaged, replayed: could be a spoofer, so do not answer
        };
        me.last_seen = now;
        let (my_vip, room) = (me.vip, me.room.clone());
        let same_room = |s: &Session| s.room == room;
        // The tunnel is for the game only: its two UDP ports, and datagrams the engine itself made.
        let game_ok = |src: u16, dst: u16, payload: &[u8], ports: &[u16]| GAME_PORTS.contains(&src) && ports.contains(&dst) && is_game_packet(payload);
        let replies: Vec<(SocketAddr, Packet)> = match pkt {
            Packet::Ping => vec![(from, Packet::Ping)],
            Packet::StatsRequest => vec![(from, Packet::Stats(self.stats(now, &room)))],
            Packet::Crc { frame, crc, host } => self.record_crc(&room, my_vip, frame, crc, host),
            Packet::Leave => {
                self.note(format!("leave: {} left room '{}'", fmt_vip(my_vip), room));
                self.sessions.remove(&from);
                self.hs_cache.remove(&from);
                self.gc_rooms();
                vec![]
            }
            Packet::Bcast { src_port, dst_port, payload, .. } if game_ok(src_port, dst_port, &payload, &[LOBBY_PORT]) => self
                .sessions
                .iter()
                .filter(|(a, s)| **a != from && same_room(s))
                .map(|(a, _)| (*a, Packet::Bcast { vip: my_vip, src_port, dst_port, payload: payload.clone() }))
                .collect(),
            Packet::Data { vip, src_port, dst_port, payload } if game_ok(src_port, dst_port, &payload, &GAME_PORTS) => self
                .sessions
                .iter()
                .filter(|(a, s)| **a != from && same_room(s) && s.vip == vip)
                .map(|(a, _)| (*a, Packet::Data { vip: my_vip, src_port, dst_port, payload: payload.clone() }))
                .collect(),
            _ => vec![],
        };
        // Count what the relay actually forwarded (not pings and stats).
        for (_, p) in &replies {
            if let Packet::Bcast { payload, .. } | Packet::Data { payload, .. } = p {
                self.sec_pkts = self.sec_pkts.saturating_add(1);
                self.sec_bytes = self.sec_bytes.saturating_add(payload.len() as u32 + 9);
            }
        }
        // Each recipient has its own keys.
        replies
            .into_iter()
            .filter_map(|(to, p)| self.sessions.get(&to).map(|s| (to, Wire::Transport { data: s.crypto.seal(&p) }.encode())))
            .collect()
    }

    /// One player's checksum for a frame. When every player in the room has reported that frame the checksums are
    /// compared: all equal is the normal case and says nothing. Otherwise everyone in the room is told which frame
    /// diverged and, if a strict majority agreed, which players are the odd ones out.
    fn record_crc(&mut self, room: &str, vip: Vip, frame: u32, crc: u32, host: bool) -> Vec<(SocketAddr, Packet)> {
        let members: Vec<(SocketAddr, Vip)> = self.sessions.iter().filter(|(_, s)| s.room == room).map(|(a, s)| (*a, s.vip)).collect();
        let sync = self.sync.entry(room.to_string()).or_default();
        if sync.verdicts >= SYNC_VERDICTS_PER_ROOM {
            return vec![]; // enough reports for this room; the log has them
        }
        sync.frames.entry(frame).or_default().insert(vip, (crc, host));
        while sync.frames.len() > SYNC_FRAMES_KEPT {
            sync.frames.pop_first();
        }
        let reports = &sync.frames[&frame];
        if reports.len() < members.len().max(2) || !members.iter().all(|(_, v)| reports.contains_key(v)) {
            return vec![]; // still waiting for someone
        }
        let mut votes: HashMap<u32, usize> = HashMap::new();
        for (_, v) in &members {
            *votes.entry(reports[v].0).or_default() += 1;
        }
        if votes.len() == 1 {
            sync.frames.remove(&frame);
            sync.agreed += 1;
            sync.last_agreed = frame;
            return vec![]; // everyone agrees
        }
        // The host is the master: when exactly one player says it is the host, everyone else is measured against it.
        // (Two players claiming to be the host cancel out, and the majority decides instead.)
        let hosts: Vec<Vip> = members.iter().map(|(_, v)| *v).filter(|v| reports[v].1).collect();
        let (master, host, certain) = match hosts.as_slice() {
            [h] => (reports[h].0, Some(*h), true),
            _ => {
                let top = votes.iter().max_by_key(|(c, n)| (**n, std::cmp::Reverse(**c))).map(|(c, n)| (*c, *n)).unwrap();
                (top.0, None, top.1 * 2 > members.len())
            }
        };
        let mut players: Vec<SyncEntry> = members.iter().take(MAX_SYNC_PLAYERS).map(|(_, v)| SyncEntry { vip: *v, crc: reports[v].0, odd: certain && reports[v].0 != master }).collect();
        players.sort_by_key(|e| e.vip);
        sync.frames.remove(&frame);
        sync.verdicts += 1;
        let summary: Vec<String> = players.iter().map(|e| format!("{}={:08X}{}", fmt_vip(e.vip), e.crc, if e.odd { " (odd)" } else { "" })).collect();
        let (agreed, last) = (sync.agreed, sync.last_agreed);
        let verdict = Packet::Desync(SyncVerdict { frame, certain, host, players });
        let line = format!(
            "DESYNC room '{room}' frame {frame}: {}; host {}; {}; {agreed} frames agreed before it (last {last}){}",
            summary.join(", "),
            host.map(fmt_vip).unwrap_or_else(|| "unknown".into()),
            if certain { "certain" } else { "no clear majority" },
            if self.sync[room].verdicts >= SYNC_VERDICTS_PER_ROOM { "; further verdicts for this room are not reported" } else { "" }
        );
        self.note(line);
        members.into_iter().map(|(a, _)| (a, verdict.clone())).collect()
    }

    /// Sign-up (the cookie has already shown the sender can receive at its address).
    fn register(&mut self, from: SocketAddr, now: u64) -> Packet {
        if !self.open {
            return Packet::Reject(Reason::RegistrationClosed);
        }
        if let Some((t, serial)) = self.recent.get(&from) {
            if now.saturating_sub(*t) < REGISTER_DEDUP_SECS {
                return Packet::Issued { serial: serial.clone() };
            }
        }
        self.all_signups.retain(|t| now.saturating_sub(*t) < 3600);
        if self.all_signups.len() >= REGISTRATIONS_PER_HOUR_TOTAL {
            return Packet::Reject(Reason::RateLimited);
        }
        let hist = self.by_ip.entry(rate_key(from.ip())).or_default();
        hist.retain(|t| now.saturating_sub(*t) < 3600);
        if hist.len() >= REGISTRATIONS_PER_HOUR {
            return Packet::Reject(Reason::RateLimited);
        }
        let Some(bytes) = (self.rng)() else {
            return Packet::Reject(Reason::Full);
        };
        let serial = format_serial(bytes);
        hist.push(now);
        self.all_signups.push(now);
        self.serials.active.insert(serial.clone());
        self.recent.insert(from, (now, serial.clone()));
        self.new_serials.push(serial.clone());
        Packet::Issued { serial }
    }

    /// Decide a join. Nothing is stored until the reply has been built (see `handshake`).
    fn hello(&mut self, from: SocketAddr, serial: String, room: String, verifier: Verifier, now: u64) -> Result<Join, Reason> {
        if self.serials.revoked.contains(&serial) {
            return Err(Reason::Revoked);
        }
        if !self.serials.active.contains(&serial) {
            // Guessing serials: cut an address (or IPv6 /64) off after a few misses.
            let key = rate_key(from.ip());
            let e = self.bad_serials.entry(key).or_insert((now, 0));
            if now.saturating_sub(e.0) >= PASSWORD_WINDOW_SECS {
                *e = (now, 0);
            }
            if e.1 >= SERIAL_TRIES {
                return Err(Reason::RateLimited);
            }
            e.1 += 1;
            return Err(Reason::UnknownSerial);
        }
        if !valid_room(&room) {
            return Err(Reason::BadRoom);
        }
        // A new handshake from an address that already has a session replaces it (the client restarted).
        if self.sessions.remove(&from).is_some() {
            self.gc_rooms();
        }
        if self.sessions.values().any(|s| s.serial == serial) {
            return Err(Reason::InUse);
        }
        let ip = rate_key(from.ip());
        if self.sessions.keys().filter(|a| rate_key(a.ip()) == ip).count() >= SESSIONS_PER_IP {
            return Err(Reason::RateLimited);
        }
        if self.bad_passwords.get(&ip).is_some_and(|&(t, n)| now.saturating_sub(t) < PASSWORD_WINDOW_SECS && n >= PASSWORD_TRIES) {
            return Err(Reason::RateLimited);
        }
        match self.rooms.get(&room) {
            Some(v) if !ct_eq(v, &verifier) => {
                let e = self.bad_passwords.entry(ip).or_insert((now, 0));
                if now.saturating_sub(e.0) >= PASSWORD_WINDOW_SECS {
                    *e = (now, 0);
                }
                e.1 += 1;
                return Err(Reason::WrongPassword);
            }
            Some(_) => {}
            None if self.rooms.len() >= MAX_ROOMS => return Err(Reason::Full),
            None => {}
        }
        let used: HashSet<u8> = self.sessions.values().filter(|s| s.room == room).map(|s| s.vip[3]).collect();
        if used.len() >= ROOM_SIZE {
            return Err(Reason::RoomFull);
        }
        let Some(n) = (1..=254u8).find(|n| !used.contains(n)) else {
            return Err(Reason::RoomFull);
        };
        Ok(Join { serial, room, verifier, vip: [VIP_PREFIX[0], VIP_PREFIX[1], VIP_PREFIX[2], n] })
    }
}

/// An accepted join, before its session exists.
struct Join {
    serial: String,
    room: String,
    verifier: Verifier,
    vip: Vip,
}

#[cfg(test)]
mod tests {
    use super::*;
    use generals_proto::{game_packet, room_verifier};

    fn addr(p: u16) -> SocketAddr {
        format!("127.0.0.1:{p}").parse().unwrap()
    }
    fn addr_from(ip: &str, p: u16) -> SocketAddr {
        format!("{ip}:{p}").parse().unwrap()
    }
    fn hello(s: &str, room: &str, pw: &str) -> Packet {
        Packet::Hello { serial: s.into(), room: room.into(), verifier: [pw.len() as u8; 32].map(|b| b ^ room.len() as u8) }
    }
    fn counting_rng() -> Rng {
        let mut n = 0u64;
        Box::new(move || {
            n += 1;
            Some(n.to_be_bytes())
        })
    }
    fn bcast(b: u8) -> Packet {
        Packet::Bcast { vip: [0; 4], src_port: 8086, dst_port: 8086, payload: game_packet(&[b]) }
    }
    fn data(vip: Vip, port: u16, payload: Vec<u8>) -> Packet {
        Packet::Data { vip, src_port: port, dst_port: port, payload }
    }

    /// A server plus the client side of every connection made to it, so tests can speak the real protocol:
    /// handshake with the cookie challenge, then encrypted traffic in both directions.
    struct World {
        s: Server,
        clients: HashMap<SocketAddr, Crypto>,
    }

    impl World {
        fn new(serials: &str) -> World {
            let mut s = Server::new(Serials::parse(serials));
            s.set_rng(counting_rng());
            World { s, clients: HashMap::new() }
        }
        fn standard() -> World {
            World::new("# c\nAAA first\nBBB\nDDD\n!CCC\n")
        }

        /// A first handshake message from `from` with a valid cookie. `None`: the server stayed silent.
        fn handshake(&mut self, from: SocketAddr, first: &Packet, now: u64) -> Option<(Packet, Crypto)> {
            let (msg1, pending) = secure::client_start(&self.s.public_key(), first).unwrap();
            let out = self.s.handle(from, &Wire::Hs1 { cookie: [0; 16], msg: msg1.clone() }.encode(), now);
            let Some(Wire::Challenge { cookie }) = out.first().and_then(|o| Wire::decode(&o.1)) else { panic!("expected a challenge, got {out:?}") };
            let out = self.s.handle(from, &Wire::Hs1 { cookie, msg: msg1 }.encode(), now);
            let Some(Wire::Hs2 { msg }) = out.first().and_then(|o| Wire::decode(&o.1)) else { return None };
            Some(pending.finish(&msg).unwrap())
        }

        /// Join as `from`; the reply is Welcome (and the keys are kept) or a Reject. Silence is a failure of the test.
        fn join(&mut self, from: SocketAddr, hello: Packet, now: u64) -> Packet {
            let (reply, crypto) = self.handshake(from, &hello, now).expect("the server answered");
            if matches!(reply, Packet::Welcome { .. }) {
                self.clients.insert(from, crypto);
            }
            reply
        }
        fn welcome(&mut self, from: SocketAddr, hello: Packet, now: u64) -> Vip {
            match self.join(from, hello, now) {
                Packet::Welcome { vip } => vip,
                p => panic!("expected Welcome, got {p:?}"),
            }
        }
        fn quick(&mut self, from: SocketAddr, serial: &str, room: &str, now: u64) -> Vip {
            self.welcome(from, hello(serial, room, ""), now)
        }

        /// Send `p` from `from` over its session; what each recipient decrypts.
        fn send(&mut self, from: SocketAddr, p: Packet, now: u64) -> Vec<(SocketAddr, Packet)> {
            let wire = Wire::Transport { data: self.clients[&from].seal(&p) }.encode();
            self.s
                .handle(from, &wire, now)
                .into_iter()
                .map(|(to, bytes)| {
                    let Some(Wire::Transport { data }) = Wire::decode(&bytes) else { panic!("not transport") };
                    (to, self.clients[&to].open(&data).expect("recipient can read it"))
                })
                .collect()
        }
        fn recipients(&mut self, from: SocketAddr, p: Packet, now: u64) -> Vec<SocketAddr> {
            self.send(from, p, now).into_iter().map(|o| o.0).collect()
        }
    }

    #[test]
    fn serial_checks() {
        let mut w = World::standard();
        assert_eq!(w.join(addr(1), hello("ZZZ", "r", ""), 0), Packet::Reject(Reason::UnknownSerial));
        assert_eq!(w.join(addr(1), hello("CCC", "r", ""), 0), Packet::Reject(Reason::Revoked));
        assert_eq!(w.quick(addr(1), "AAA", "r", 0), [10, 77, 0, 1]);
        // a second address cannot reuse the serial
        assert_eq!(w.join(addr(2), hello("AAA", "r", ""), 1), Packet::Reject(Reason::InUse));
        // the same address restarting (new handshake) takes its own session over
        assert_eq!(w.quick(addr(1), "AAA", "r", 2), [10, 77, 0, 1]);
    }

    #[test]
    fn serial_frees_after_timeout() {
        let mut w = World::standard();
        w.quick(addr(1), "AAA", "r", 0);
        w.s.expire(SESSION_TIMEOUT_SECS);
        assert!(matches!(w.join(addr(2), hello("AAA", "r", ""), SESSION_TIMEOUT_SECS), Packet::Welcome { .. }));
    }

    #[test]
    fn rooms_are_password_protected_and_isolated() {
        let mut w = World::standard();
        let j = |serial: &str, room: &str, pw: &str| Packet::Hello { serial: serial.into(), room: room.into(), verifier: room_verifier(room, pw) };
        w.welcome(addr(1), j("AAA", "lan", "pw"), 0);
        assert_eq!(w.join(addr(2), j("BBB", "lan", "nope"), 0), Packet::Reject(Reason::WrongPassword));
        assert_eq!(w.welcome(addr(2), j("BBB", "lan", "pw"), 0), [10, 77, 0, 2]);
        // a different room gets its own address space and sees nothing from "lan"
        assert_eq!(w.welcome(addr(3), j("DDD", "other", ""), 0), [10, 77, 0, 1]);
        assert_eq!(w.recipients(addr(1), bcast(1), 1), [addr(2)]);
        assert!(w.recipients(addr(3), bcast(2), 1).is_empty());
        // unicast cannot cross rooms: .2 exists only in "lan", so a player in "other" reaches nobody
        assert!(w.recipients(addr(3), data([10, 77, 0, 2], 8088, game_packet(&[1])), 1).is_empty());
        // ...while inside "lan" the same address is delivered
        assert_eq!(w.recipients(addr(2), data([10, 77, 0, 1], 8088, game_packet(&[1])), 1), [addr(1)]);
    }

    #[test]
    fn empty_rooms_remove_themselves() {
        let mut w = World::standard();
        w.quick(addr(1), "AAA", "r1", 0);
        w.quick(addr(2), "BBB", "r2", 0);
        assert_eq!(w.s.room_count(), 2);
        w.send(addr(1), Packet::Leave, 1); // explicit leave
        assert_eq!(w.s.room_count(), 1);
        w.s.expire(SESSION_TIMEOUT_SECS + 1); // timeout
        assert_eq!(w.s.room_count(), 0);
        // the name is free again, with a new password
        let h = Packet::Hello { serial: "AAA".into(), room: "r2".into(), verifier: room_verifier("r2", "new") };
        assert!(matches!(w.join(addr(5), h, SESSION_TIMEOUT_SECS + 2), Packet::Welcome { .. }));
    }

    #[test]
    fn room_limits_and_names() {
        let mut w = World::standard();
        for bad in ["", " x", "x ", "a/b", "<script>"] {
            assert_eq!(w.join(addr(1), hello("AAA", bad, ""), 0), Packet::Reject(Reason::BadRoom), "{bad:?}");
        }
        // too long to even be a valid message: no answer at all
        assert!(w.handshake(addr(1), &hello("AAA", &"x".repeat(33), ""), 0).is_none());
        let mut full = Serials::default();
        for i in 0..=ROOM_SIZE {
            full.active.insert(format!("S{i}"));
        }
        let mut w = World::standard();
        w.s.set_serials(full);
        for i in 0..ROOM_SIZE {
            w.quick(addr(100 + i as u16), &format!("S{i}"), "big", 0);
        }
        assert_eq!(w.join(addr(200), hello(&format!("S{ROOM_SIZE}"), "big", ""), 0), Packet::Reject(Reason::RoomFull));
    }

    #[test]
    fn password_guessing_is_rate_limited_per_address() {
        let mut w = World::standard();
        let j = |serial: &str, v: Verifier| Packet::Hello { serial: serial.into(), room: "r".into(), verifier: v };
        w.welcome(addr(1), j("AAA", room_verifier("r", "pw")), 0);
        for i in 0..PASSWORD_TRIES as u8 {
            assert_eq!(w.join(addr(9), j("BBB", [i; 32]), 1), Packet::Reject(Reason::WrongPassword));
        }
        assert_eq!(w.join(addr(9), j("BBB", [99; 32]), 2), Packet::Reject(Reason::RateLimited));
        // even the right password is refused from that address until the window passes
        assert_eq!(w.join(addr(9), j("BBB", room_verifier("r", "pw")), 3), Packet::Reject(Reason::RateLimited));
        w.s.expire(PASSWORD_WINDOW_SECS + 2);
        assert!(matches!(w.join(addr(9), j("BBB", room_verifier("r", "pw")), PASSWORD_WINDOW_SECS + 2), Packet::Welcome { .. }));
    }

    #[test]
    fn forged_replayed_and_unknown_traffic_gets_no_answer() {
        let mut w = World::standard();
        w.quick(addr(1), "AAA", "r", 0);
        w.quick(addr(2), "BBB", "r", 0);
        let wire = Wire::Transport { data: w.clients[&addr(1)].seal(&bcast(1)) }.encode();
        assert_eq!(w.s.handle(addr(1), &wire, 1).len(), 1);
        assert!(w.s.handle(addr(1), &wire, 1).is_empty(), "replay");
        let mut bad = wire.clone();
        *bad.last_mut().unwrap() ^= 1;
        assert!(w.s.handle(addr(1), &bad, 1).is_empty(), "damaged");
        assert!(w.s.handle(addr(7), &wire, 1).is_empty(), "unknown source, and no reply to say so");
        // someone else's keys, and a spoofer who knows the address but not the keys
        let other = World::standard().clients.len();
        assert_eq!(other, 0);
        let mut w2 = World::standard();
        w2.quick(addr(1), "AAA", "r", 0);
        let foreign = Wire::Transport { data: w2.clients[&addr(1)].seal(&bcast(1)) }.encode();
        assert!(w.s.handle(addr(1), &foreign, 2).is_empty());
        // plaintext packets (the old format, or an engine packet) are not a protocol here
        assert!(w.s.handle(addr(1), &bcast(1).encode(), 2).is_empty());
        assert!(w.s.handle(addr(1), &Wire::Hs2 { msg: vec![1; 50] }.encode(), 2).is_empty());
        // the genuine client carries on
        assert_eq!(w.recipients(addr(1), bcast(2), 3), [addr(2)]);
    }

    #[test]
    fn revoking_drops_live_session_and_room() {
        let mut w = World::standard();
        w.quick(addr(1), "AAA", "r", 0);
        w.s.set_serials(Serials::parse("!AAA\nBBB"));
        assert_eq!(w.s.room_count(), 0);
        let wire = Wire::Transport { data: w.clients[&addr(1)].seal(&Packet::Ping) }.encode();
        assert!(w.s.handle(addr(1), &wire, 1).is_empty());
    }

    #[test]
    fn register_needs_a_cookie_and_issues_a_working_serial() {
        let mut w = World::new("");
        let (msg1, _) = secure::client_start(&w.s.public_key(), &Packet::Register).unwrap();
        let out = w.s.handle(addr(1), &Wire::Hs1 { cookie: [0; 16], msg: msg1.clone() }.encode(), 0);
        let Some(Wire::Challenge { cookie }) = Wire::decode(&out[0].1) else { panic!() };
        assert!(w.s.take_new_serials().is_empty(), "the challenge is stateless: nothing issued yet");
        // a cookie for another address does not work
        let out = w.s.handle(addr_from("127.0.0.2", 1), &Wire::Hs1 { cookie, msg: msg1.clone() }.encode(), 0);
        assert!(matches!(Wire::decode(&out[0].1), Some(Wire::Challenge { .. })));
        let (reply, _) = w.handshake(addr(1), &Packet::Register, 1).unwrap();
        let Packet::Issued { serial } = reply else { panic!("{reply:?}") };
        assert_eq!(w.s.take_new_serials(), [serial.clone()]);
        assert!(matches!(w.join(addr(1), hello(&serial, "r", ""), 2), Packet::Welcome { .. }));
        // a retry returns the same serial; an old cookie stops working after two minutes
        let (again, _) = w.handshake(addr(1), &Packet::Register, 5).unwrap();
        assert_eq!(again, Packet::Issued { serial });
        let out = w.s.handle(addr(3), &Wire::Hs1 { cookie, msg: msg1 }.encode(), 200);
        assert!(matches!(Wire::decode(&out[0].1), Some(Wire::Challenge { .. })));
    }

    #[test]
    fn register_is_rate_limited_per_ip_and_can_be_closed() {
        let mut w = World::new("");
        for p in 0..REGISTRATIONS_PER_HOUR as u16 {
            let (r, _) = w.handshake(addr_from(&format!("127.0.0.{}", 10 + p), 1), &Packet::Register, 0).unwrap();
            assert!(matches!(r, Packet::Issued { .. }), "{r:?}");
        }
        // a sixth from the same address
        let mut w = World::new("");
        let mut results = vec![];
        for p in 0..=REGISTRATIONS_PER_HOUR as u16 {
            results.push(w.handshake(addr(100 + p), &Packet::Register, 0).unwrap().0);
        }
        assert!(results[..REGISTRATIONS_PER_HOUR].iter().all(|r| matches!(r, Packet::Issued { .. })));
        assert_eq!(results[REGISTRATIONS_PER_HOUR], Packet::Reject(Reason::RateLimited));
        w.s.expire(3601);
        assert!(matches!(w.handshake(addr(999), &Packet::Register, 3601).unwrap().0, Packet::Issued { .. }));
        w.s.set_open(false);
        assert_eq!(w.handshake(addr(1), &Packet::Register, 3602).unwrap().0, Packet::Reject(Reason::RegistrationClosed));
    }

    #[test]
    fn unauthenticated_replies_are_never_bigger_than_the_request() {
        let mut w = World::standard();
        // key request: padded so the reply is smaller
        let req = Wire::KeyRequest.encode();
        let rsp = w.s.handle(addr(1), &req, 0).remove(0).1;
        assert!(rsp.len() < req.len());
        assert_eq!(Wire::decode(&rsp), Some(Wire::KeyReply { key: w.s.public_key() }));
        // a first handshake message without a cookie gets only a small challenge
        let (msg1, _) = secure::client_start(&w.s.public_key(), &hello("AAA", "r", "")).unwrap();
        let hs = Wire::Hs1 { cookie: [0; 16], msg: msg1 }.encode();
        assert!(w.s.handle(addr(1), &hs, 0).remove(0).1.len() <= hs.len());
        // garbage and short keys request get nothing
        assert!(w.s.handle(addr(1), &[20; 10], 0).is_empty());
    }

    #[test]
    fn handshakes_are_private_and_only_for_the_real_server_key() {
        let mut w = World::standard();
        let (msg1, _) = secure::client_start(&w.s.public_key(), &hello("AAA", "secretroom", "")).unwrap();
        assert!(!msg1.windows(3).any(|x| x == b"AAA") && !msg1.windows(10).any(|x| x == b"secretroom"));
        // a client that pinned some other key (an impostor server, or a changed identity) gets no answer
        let (_, wrong_pub) = secure::generate_keypair();
        let (bad1, _) = secure::client_start(&wrong_pub, &hello("AAA", "r", "")).unwrap();
        let cookie = match Wire::decode(&w.s.handle(addr(1), &Wire::Hs1 { cookie: [0; 16], msg: bad1.clone() }.encode(), 0)[0].1) {
            Some(Wire::Challenge { cookie }) => cookie,
            _ => panic!(),
        };
        assert!(w.s.handle(addr(1), &Wire::Hs1 { cookie, msg: bad1 }.encode(), 0).is_empty());
    }

    #[test]
    fn a_retransmitted_first_message_gets_the_same_answer_not_a_second_session() {
        let mut w = World::standard();
        let (msg1, pending) = secure::client_start(&w.s.public_key(), &hello("AAA", "r", "")).unwrap();
        let hs = |cookie| Wire::Hs1 { cookie, msg: msg1.clone() }.encode();
        let Some(Wire::Challenge { cookie }) = Wire::decode(&w.s.handle(addr(1), &hs([0; 16]), 0)[0].1) else { panic!() };
        let first = w.s.handle(addr(1), &hs(cookie), 0);
        let again = w.s.handle(addr(1), &hs(cookie), 1);
        assert_eq!(first, again);
        let Some(Wire::Hs2 { msg }) = Wire::decode(&first[0].1) else { panic!() };
        assert!(matches!(pending.finish(&msg).unwrap().0, Packet::Welcome { vip: [10, 77, 0, 1] }));
        assert_eq!(w.s.sessions.len(), 1);
    }

    #[test]
    fn handshakes_cost_a_rate_limit_per_address() {
        let mut w = World::new("");
        let mut answered = 0;
        for p in 0..HANDSHAKES_PER_MIN + 10 {
            let (msg1, _) = secure::client_start(&w.s.public_key(), &Packet::Register).unwrap();
            let from = addr(1000 + p as u16);
            let Some(Wire::Challenge { cookie }) = Wire::decode(&w.s.handle(from, &Wire::Hs1 { cookie: [0; 16], msg: msg1.clone() }.encode(), 0)[0].1) else { panic!() };
            if !w.s.handle(from, &Wire::Hs1 { cookie, msg: msg1 }.encode(), 0).is_empty() {
                answered += 1;
            }
        }
        assert_eq!(answered, HANDSHAKES_PER_MIN, "the rest is dropped without any answer");
    }

    #[test]
    fn country_filter_silences_blocked_sources() {
        let mut w = World::standard();
        w.s.set_allow(Box::new(|ip| ip.to_string().starts_with("127.")));
        let (msg1, _) = secure::client_start(&w.s.public_key(), &hello("AAA", "r", "")).unwrap();
        let blocked = addr_from("8.8.8.8", 1);
        assert!(w.s.handle(blocked, &Wire::Hs1 { cookie: [0; 16], msg: msg1 }.encode(), 0).is_empty());
        assert!(w.s.handle(blocked, &Wire::KeyRequest.encode(), 0).is_empty());
        assert!(matches!(w.join(addr(1), hello("AAA", "r", ""), 0), Packet::Welcome { .. }));
    }

    #[test]
    fn serial_format() {
        assert_eq!(format_serial([0xAB, 0xCD, 0, 1, 2, 3, 4, 5]), "ABCD-0001-0203-0405");
    }

    #[test]
    fn only_game_traffic_is_relayed() {
        let mut w = World::standard();
        w.quick(addr(1), "AAA", "r", 0);
        let vb = w.quick(addr(2), "BBB", "r", 0);
        let mut ok = |w: &mut World, p: Packet| w.send(addr(1), p, 1).len();
        // the engine's own datagrams on its two ports pass
        assert_eq!(ok(&mut w, data(vb, 8088, game_packet(b"hello"))), 1);
        assert_eq!(ok(&mut w, data(vb, 8086, game_packet(b"hello"))), 1);
        // anything else does not: not a game packet, a foreign port, a broadcast outside the lobby, too big
        assert_eq!(ok(&mut w, data(vb, 8088, b"GET / HTTP/1.1\r\n\r\n".to_vec())), 0, "not a game packet");
        assert_eq!(ok(&mut w, data(vb, 8088, vec![0; 64])), 0);
        assert_eq!(ok(&mut w, data(vb, 53, game_packet(b"hello"))), 0, "foreign port");
        assert_eq!(ok(&mut w, data(vb, 8088, game_packet(&[7; 2000]))), 0, "oversized");
        assert_eq!(ok(&mut w, Packet::Data { vip: vb, src_port: 8088, dst_port: 443, payload: game_packet(b"x") }), 0);
        assert_eq!(ok(&mut w, Packet::Bcast { vip: [0; 4], src_port: 8088, dst_port: 8088, payload: game_packet(b"x") }), 0, "broadcast is for the lobby port only");
        assert_eq!(ok(&mut w, bcast(1)), 1);
        // a corrupted game packet fails its CRC
        let mut bad = game_packet(b"hello");
        bad[8] ^= 0x40;
        assert_eq!(ok(&mut w, data(vb, 8088, bad)), 0);
    }

    #[test]
    fn a_flooding_session_is_cut_off_but_others_are_not() {
        let mut w = World::standard();
        w.quick(addr(1), "AAA", "r", 0);
        w.quick(addr(2), "BBB", "r", 0);
        let sent = (0..SESSION_PACKETS_PER_SEC + 50).filter(|_| !w.send(addr(1), bcast(1), 5).is_empty()).count();
        assert_eq!(sent as u32, SESSION_PACKETS_PER_SEC);
        assert_eq!(w.send(addr(2), bcast(1), 5).len(), 1, "another player is unaffected");
        assert_eq!(w.send(addr(1), bcast(1), 6).len(), 1, "the limit is per second");
        // bytes are limited too
        let big = game_packet(&[9; 1000]);
        let n = (0..1000).filter(|_| !w.send(addr(2), Packet::Bcast { vip: [0; 4], src_port: 8086, dst_port: 8086, payload: big.clone() }, 9).is_empty()).count();
        assert!(n as u32 * 1030 <= SESSION_BYTES_PER_SEC + 1030 && n > 100, "{n}");
    }

    #[test]
    fn stats_describe_the_relay_without_leaking_other_rooms() {
        let mut w = World::standard();
        w.s.set_version("0.1.0-test");
        w.quick(addr(1), "AAA", "alpha", 0);
        w.quick(addr(2), "BBB", "alpha", 0);
        w.quick(addr(3), "DDD", "beta", 0);
        for _ in 0..4 {
            w.send(addr(1), bcast(1), 10);
        }
        w.s.expire(11); // closes second 10
        let out = w.send(addr(1), Packet::StatsRequest, 11);
        let Packet::Stats(st) = &out[0].1 else { panic!("{out:?}") };
        assert_eq!((st.rooms, st.players, st.room_players, st.max_rooms), (2, 3, 2, MAX_ROOMS as u16));
        assert_eq!((st.pps, st.uptime_secs, st.version.as_str()), (4, 11, "0.1.0-test"));
        assert_eq!(st.load_pct, 1, "3 of 2048 players, 2 of 256 rooms");
        assert!(st.kbps > 0);
        // an idle second reads as zero
        w.s.expire(20);
        let Packet::Stats(st) = &w.send(addr(1), Packet::StatsRequest, 20)[0].1 else { panic!() };
        assert_eq!(st.pps, 0);
    }

    #[test]
    fn one_address_cannot_hold_unlimited_sessions() {
        let mut serials = String::new();
        for i in 0..SESSIONS_PER_IP + 2 {
            serials += &format!("S{i}\n");
        }
        let mut w = World::new(&serials);
        for i in 0..SESSIONS_PER_IP {
            w.quick(addr_from("203.0.113.5", 1000 + i as u16), &format!("S{i}"), &format!("room{i}"), 0);
        }
        let over = hello(&format!("S{SESSIONS_PER_IP}"), "x", "");
        assert_eq!(w.join(addr_from("203.0.113.5", 2000), over.clone(), 0), Packet::Reject(Reason::RateLimited));
        assert!(matches!(w.join(addr_from("203.0.113.6", 2000), over, 0), Packet::Welcome { .. }), "a different address is fine");
    }

    #[test]
    fn ipv6_limits_apply_to_the_whole_64() {
        assert_eq!(rate_key("2001:db8:1:2:aaaa:bbbb:cccc:dddd".parse().unwrap()), rate_key("2001:db8:1:2::1".parse().unwrap()));
        assert_ne!(rate_key("2001:db8:1:2::1".parse().unwrap()), rate_key("2001:db8:1:3::1".parse().unwrap()));
        assert_eq!(rate_key("::ffff:192.0.2.1".parse().unwrap()), "192.0.2.1".parse::<IpAddr>().unwrap());
        let mut w = World::standard();
        for i in 0..SERIAL_TRIES {
            let from: SocketAddr = format!("[2001:db8::{:x}]:5000", i + 1).parse().unwrap(); // a new address each time, same /64
            assert_eq!(w.join(from, hello(&format!("NOPE{i}"), "r", ""), 0), Packet::Reject(Reason::UnknownSerial));
            w.s.hs_rate.clear(); // the handshake limit is its own test; this one is about serial guessing
        }
        let from: SocketAddr = "[2001:db8::ffff]:5000".parse().unwrap();
        assert_eq!(w.join(from, hello("NOPE", "r", ""), 0), Packet::Reject(Reason::RateLimited), "serial guessing is cut off");
    }

    #[test]
    fn sign_ups_have_a_global_hourly_cap() {
        let mut w = World::new("");
        let mut issued = 0;
        'outer: for a in 0..=255u8 {
            for b in 0..=255u8 {
                match w.handshake(addr_from(&format!("198.51.{a}.{b}"), 4000), &Packet::Register, 0).unwrap().0 {
                    Packet::Issued { .. } => issued += 1,
                    Packet::Reject(Reason::RateLimited) => break 'outer,
                    p => panic!("{p:?}"),
                }
            }
        }
        assert_eq!(issued, REGISTRATIONS_PER_HOUR_TOTAL);
        // the hour passes
        assert!(matches!(w.handshake(addr_from("198.51.100.1", 4000), &Packet::Register, 3700).unwrap().0, Packet::Issued { .. }));
    }

    #[test]
    fn checksums_are_compared_and_a_disagreement_is_reported_to_the_room() {
        let mut w = World::standard();
        let (a, b, c) = (addr(1), addr(2), addr(3));
        w.quick(a, "AAA", "r", 0);
        w.quick(b, "BBB", "r", 0);
        // agreeing checksums say nothing at all
        assert!(w.send(a, Packet::Crc { frame: 100, crc: 7, host: false }, 1).is_empty());
        assert!(w.send(b, Packet::Crc { frame: 100, crc: 7, host: false }, 1).is_empty());
        // two players who differ: told to both, but nobody can say who is right
        assert!(w.send(a, Packet::Crc { frame: 200, crc: 1, host: false }, 2).is_empty(), "waits for the other player");
        let out = w.send(b, Packet::Crc { frame: 200, crc: 2, host: false }, 2);
        assert_eq!(out.iter().map(|o| o.0).collect::<Vec<_>>().len(), 2);
        let Packet::Desync(v) = &out[0].1 else { panic!("{out:?}") };
        assert_eq!((v.frame, v.certain, v.host), (200, false, None));
        assert_eq!(v.players.iter().map(|e| (e.vip, e.crc, e.odd)).collect::<Vec<_>>(), [([10, 77, 0, 1], 1, false), ([10, 77, 0, 2], 2, false)]);
        // three players: the one against two others is named
        let mut w = World::standard();
        w.quick(a, "AAA", "r", 0);
        w.quick(b, "BBB", "r", 0);
        w.quick(c, "DDD", "r", 0);
        w.send(a, Packet::Crc { frame: 300, crc: 5, host: false }, 1);
        w.send(c, Packet::Crc { frame: 300, crc: 5, host: false }, 1);
        let out = w.send(b, Packet::Crc { frame: 300, crc: 9, host: false }, 1);
        assert_eq!(out.len(), 3, "everyone in the room is told");
        let Packet::Desync(v) = &out[0].1 else { panic!() };
        assert!(v.certain);
        assert_eq!(v.players.iter().filter(|e| e.odd).map(|e| e.vip).collect::<Vec<_>>(), [[10, 77, 0, 2]]);
    }

    #[test]
    fn the_operator_log_names_joins_desyncs_and_closed_rooms() {
        let (a, b) = (addr(1), addr(2));
        let mut w = World::standard();
        w.quick(a, "AAA", "r", 0);
        w.quick(b, "BBB", "r", 0);
        w.send(a, Packet::Crc { frame: 100, crc: 7, host: true }, 1);
        w.send(b, Packet::Crc { frame: 100, crc: 7, host: false }, 1);
        w.send(a, Packet::Crc { frame: 200, crc: 1, host: true }, 2);
        w.send(b, Packet::Crc { frame: 200, crc: 2, host: false }, 2);
        w.send(a, Packet::Leave, 3);
        w.send(b, Packet::Leave, 3);
        let log = w.s.take_events().join("\n");
        assert!(log.contains("join: 10.77.0.1") && log.contains("opened room 'r', 1 in it") && log.contains("entered room 'r', 2 in it"), "{log}");
        assert!(log.contains("DESYNC room 'r' frame 200") && log.contains("10.77.0.2=00000002 (odd)") && log.contains("host 10.77.0.1; certain; 1 frames agreed"), "{log}");
        assert!(log.contains("leave: 10.77.0.1") && log.contains("room 'r' closed: 1 checked frames agreed (last 100), 1 desync verdicts"), "{log}");
        assert!(w.s.take_events().is_empty(), "drained");
    }

    #[test]
    fn sync_reports_are_bounded_and_per_room() {
        let mut w = World::standard();
        w.quick(addr(1), "AAA", "r1", 0);
        w.quick(addr(2), "BBB", "r2", 0);
        // a lone player in a room never gets a verdict, and different rooms are not mixed
        for f in 0..40 {
            assert!(w.send(addr(1), Packet::Crc { frame: f, crc: 1, host: false }, 1).is_empty());
            assert!(w.send(addr(2), Packet::Crc { frame: f, crc: 2, host: false }, 1).is_empty());
        }
        assert!(w.s.sync["r1"].frames.len() <= SYNC_FRAMES_KEPT);
        // a diverged game gets a few verdicts, not an endless stream
        let mut w = World::standard();
        w.quick(addr(1), "AAA", "r", 0);
        w.quick(addr(2), "BBB", "r", 0);
        let mut sent = 0;
        for f in 0..20 {
            w.send(addr(1), Packet::Crc { frame: f, crc: 1, host: false }, 1);
            sent += w.send(addr(2), Packet::Crc { frame: f, crc: 2, host: false }, 1).len();
        }
        assert_eq!(sent, 2 * SYNC_VERDICTS_PER_ROOM as usize);
        // leaving a room clears what it had reported
        w.send(addr(1), Packet::Leave, 2);
        w.send(addr(2), Packet::Leave, 2);
        assert!(w.s.sync.is_empty());
    }

    #[test]
    fn the_host_is_the_master() {
        let mut w = World::standard();
        let (host, guest) = (addr(1), addr(2));
        w.quick(host, "AAA", "r", 0);
        w.quick(guest, "BBB", "r", 0);
        // two players who differ: with the host flagged, the guest is the one out of sync, for certain
        w.send(host, Packet::Crc { frame: 100, crc: 1, host: true }, 1);
        let out = w.send(guest, Packet::Crc { frame: 100, crc: 2, host: false }, 1);
        let Packet::Desync(v) = &out[0].1 else { panic!("{out:?}") };
        assert!(v.certain);
        assert_eq!(v.host, Some([10, 77, 0, 1]));
        assert_eq!(v.players.iter().map(|e| (e.vip, e.odd)).collect::<Vec<_>>(), [([10, 77, 0, 1], false), ([10, 77, 0, 2], true)]);
        // the host wins even against a majority: the master's simulation is the reference
        let mut w = World::standard();
        w.quick(host, "AAA", "r", 0);
        w.quick(guest, "BBB", "r", 0);
        w.quick(addr(3), "DDD", "r", 0);
        w.send(guest, Packet::Crc { frame: 100, crc: 9, host: false }, 1);
        w.send(addr(3), Packet::Crc { frame: 100, crc: 9, host: false }, 1);
        let out = w.send(host, Packet::Crc { frame: 100, crc: 1, host: true }, 1);
        let Packet::Desync(v) = &out[0].1 else { panic!() };
        assert_eq!(v.players.iter().filter(|e| e.odd).count(), 2);
        // two players both claiming to be the host cancel out: back to majority rules (a tie of two is not certain)
        let mut w = World::standard();
        w.quick(host, "AAA", "r", 0);
        w.quick(guest, "BBB", "r", 0);
        w.send(host, Packet::Crc { frame: 100, crc: 1, host: true }, 1);
        let out = w.send(guest, Packet::Crc { frame: 100, crc: 2, host: true }, 1);
        let Packet::Desync(v) = &out[0].1 else { panic!() };
        assert_eq!((v.certain, v.host), (false, None));
    }
}
