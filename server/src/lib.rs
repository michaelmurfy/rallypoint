//! Relay state machine. No sockets or clocks in here: `handle` takes the sender and the time and returns
//! the datagrams to send, so it is testable without a network.
use generals_proto::{ct_eq, sha256, Cookie, Packet, Reason, Verifier, Vip};
pub mod geo;

use std::collections::{HashMap, HashSet};
use std::net::{IpAddr, SocketAddr};

pub const SESSION_TIMEOUT_SECS: u64 = 30;
pub const MAX_ROOMS: usize = 256;
pub const ROOM_SIZE: usize = 8; // Generals' player limit
/// Wrong room passwords allowed per source address per window before it is cut off.
const PASSWORD_TRIES: u32 = 10;
const PASSWORD_WINDOW_SECS: u64 = 600;
pub const REGISTRATIONS_PER_HOUR: usize = 5;
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
    token: u64,
    last_seen: u64,
}

type Out = Vec<(SocketAddr, Packet)>;

/// Rooms are created by the first player to join and live only while someone is in them.
pub struct Server {
    serials: Serials,
    sessions: HashMap<SocketAddr, Session>,
    rooms: HashMap<String, Verifier>,
    open: bool,
    rng: Rng,
    secret: [u8; 32],
    allow: Box<dyn Fn(IpAddr) -> bool + Send>,
    recent: HashMap<SocketAddr, (u64, String)>,
    by_ip: HashMap<IpAddr, Vec<u64>>,
    bad_passwords: HashMap<IpAddr, (u64, u32)>,
    new_serials: Vec<String>,
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
            allow: Box::new(|_| true),
            recent: HashMap::new(),
            by_ip: HashMap::new(),
            bad_passwords: HashMap::new(),
            new_serials: vec![],
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
        self.sessions.retain(|_, s| now.saturating_sub(s.last_seen) < SESSION_TIMEOUT_SECS);
        self.gc_rooms();
        self.recent.retain(|_, (t, _)| now.saturating_sub(*t) < REGISTER_DEDUP_SECS);
        self.by_ip.retain(|_, h| {
            h.retain(|t| now.saturating_sub(*t) < 3600);
            !h.is_empty()
        });
        self.bad_passwords.retain(|_, (t, _)| now.saturating_sub(*t) < PASSWORD_WINDOW_SECS);
    }

    /// A room exists only while it has members.
    fn gc_rooms(&mut self) {
        let used: HashSet<&String> = self.sessions.values().map(|s| &s.room).collect();
        self.rooms.retain(|name, _| used.contains(name));
    }

    fn cookie(&self, ip: IpAddr, bucket: u64) -> Cookie {
        let h = sha256(&[&self.secret, &ip_bytes(ip), &bucket.to_be_bytes()]);
        h[..16].try_into().unwrap()
    }

    pub fn handle(&mut self, from: SocketAddr, pkt: Packet, now: u64) -> Out {
        // ponytail: every in-session packet is signed with a per-session token, but the wire is not encrypted:
        // an on-path attacker can still read or replay traffic. Add a handshake key exchange if that matters.
        match pkt {
            Packet::Register { cookie } => {
                if !(self.allow)(from.ip()) {
                    return vec![]; // silent: say nothing to blocked sources
                }
                vec![(from, self.register(from, cookie, now))]
            }
            Packet::Hello { serial, room, verifier } => {
                if !(self.allow)(from.ip()) {
                    return vec![];
                }
                vec![(from, self.hello(from, serial, room, verifier, now))]
            }
            Packet::Signed { token, inner } => self.in_session(from, token, *inner, now),
            _ => vec![], // unsigned or server-to-client packets are never valid here
        }
    }

    fn in_session(&mut self, from: SocketAddr, token: u64, pkt: Packet, now: u64) -> Out {
        let Some(me) = self.sessions.get_mut(&from) else {
            return vec![(from, Packet::Reject(Reason::NotAuthed))];
        };
        if !ct_eq(&me.token.to_be_bytes(), &token.to_be_bytes()) {
            return vec![]; // wrong token: could be a spoofer, so do not answer
        }
        me.last_seen = now;
        let (my_vip, room) = (me.vip, me.room.clone());
        let same_room = |s: &Session| s.room == room;
        match pkt {
            Packet::Ping => vec![(from, Packet::Ping)],
            Packet::Leave => {
                self.sessions.remove(&from);
                self.gc_rooms();
                vec![]
            }
            Packet::Bcast { src_port, dst_port, payload, .. } => self
                .sessions
                .iter()
                .filter(|(a, s)| **a != from && same_room(s))
                .map(|(a, _)| (*a, Packet::Bcast { vip: my_vip, src_port, dst_port, payload: payload.clone() }))
                .collect(),
            Packet::Data { vip, src_port, dst_port, payload } => self
                .sessions
                .iter()
                .filter(|(a, s)| **a != from && same_room(s) && s.vip == vip)
                .map(|(a, _)| (*a, Packet::Data { vip: my_vip, src_port, dst_port, payload: payload.clone() }))
                .collect(),
            _ => vec![],
        }
    }

    /// Stateless sign-up: first ask gets a Challenge cookie bound to the source address, and only a client that
    /// can receive at that address can echo it back. This stops spoofed sign-ups filling the serial file.
    fn register(&mut self, from: SocketAddr, cookie: Cookie, now: u64) -> Packet {
        if !self.open {
            return Packet::Reject(Reason::RegistrationClosed);
        }
        if self.secret == [0; 32] {
            return Packet::Reject(Reason::Full); // no entropy: refuse rather than issue guessable serials
        }
        let bucket = now / 60;
        let fresh = self.cookie(from.ip(), bucket);
        let valid = cookie == fresh || (bucket > 0 && cookie == self.cookie(from.ip(), bucket - 1));
        if !valid {
            return Packet::Challenge { cookie: fresh };
        }
        if let Some((t, serial)) = self.recent.get(&from) {
            if now.saturating_sub(*t) < REGISTER_DEDUP_SECS {
                return Packet::Issued { serial: serial.clone() };
            }
        }
        let hist = self.by_ip.entry(from.ip()).or_default();
        hist.retain(|t| now.saturating_sub(*t) < 3600);
        if hist.len() >= REGISTRATIONS_PER_HOUR {
            return Packet::Reject(Reason::RateLimited);
        }
        let Some(bytes) = (self.rng)() else {
            return Packet::Reject(Reason::Full);
        };
        let serial = format_serial(bytes);
        hist.push(now);
        self.serials.active.insert(serial.clone());
        self.recent.insert(from, (now, serial.clone()));
        self.new_serials.push(serial.clone());
        Packet::Issued { serial }
    }

    fn hello(&mut self, from: SocketAddr, serial: String, room: String, verifier: Verifier, now: u64) -> Packet {
        if self.serials.revoked.contains(&serial) {
            return Packet::Reject(Reason::Revoked);
        }
        if !self.serials.active.contains(&serial) {
            return Packet::Reject(Reason::UnknownSerial);
        }
        if !valid_room(&room) {
            return Packet::Reject(Reason::BadRoom);
        }
        // A retry of the same join is answered with the same session. Anything else from this address starts over.
        if let Some(s) = self.sessions.get_mut(&from) {
            if s.serial == serial && s.room == room && self.rooms.get(&room).is_some_and(|v| ct_eq(v, &verifier)) {
                s.last_seen = now;
                return Packet::Welcome { vip: s.vip, token: s.token };
            }
            self.sessions.remove(&from);
            self.gc_rooms();
        }
        if self.sessions.values().any(|s| s.serial == serial) {
            return Packet::Reject(Reason::InUse);
        }
        let ip = from.ip();
        if self.bad_passwords.get(&ip).is_some_and(|&(t, n)| now.saturating_sub(t) < PASSWORD_WINDOW_SECS && n >= PASSWORD_TRIES) {
            return Packet::Reject(Reason::RateLimited);
        }
        match self.rooms.get(&room) {
            Some(v) if !ct_eq(v, &verifier) => {
                let e = self.bad_passwords.entry(ip).or_insert((now, 0));
                if now.saturating_sub(e.0) >= PASSWORD_WINDOW_SECS {
                    *e = (now, 0);
                }
                e.1 += 1;
                return Packet::Reject(Reason::WrongPassword);
            }
            Some(_) => {}
            None if self.rooms.len() >= MAX_ROOMS => return Packet::Reject(Reason::Full),
            None => {}
        }
        let used: HashSet<u8> = self.sessions.values().filter(|s| s.room == room).map(|s| s.vip[3]).collect();
        if used.len() >= ROOM_SIZE {
            return Packet::Reject(Reason::RoomFull);
        }
        let Some(n) = (1..=254u8).find(|n| !used.contains(n)) else {
            return Packet::Reject(Reason::RoomFull);
        };
        let Some(t) = (self.rng)() else {
            return Packet::Reject(Reason::Full);
        };
        let (vip, token) = ([VIP_PREFIX[0], VIP_PREFIX[1], VIP_PREFIX[2], n], u64::from_be_bytes(t));
        self.rooms.entry(room.clone()).or_insert(verifier);
        self.sessions.insert(from, Session { serial, room, vip, token, last_seen: now });
        Packet::Welcome { vip, token }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use generals_proto::room_verifier;

    fn addr(p: u16) -> SocketAddr {
        format!("127.0.0.1:{p}").parse().unwrap()
    }
    fn addr_from(ip: &str, p: u16) -> SocketAddr {
        format!("{ip}:{p}").parse().unwrap()
    }
    fn hello(s: &str, room: &str, pw: &str) -> Packet {
        Packet::Hello { serial: s.into(), room: room.into(), verifier: [pw.len() as u8; 32].map(|b| b ^ room.len() as u8) }
    }
    fn server() -> Server {
        let mut s = Server::new(Serials::parse("# c\nAAA first\nBBB\nDDD\n!CCC\n"));
        s.set_rng(counting_rng());
        s
    }
    fn counting_rng() -> Rng {
        let mut n = 0u8;
        Box::new(move || {
            n += 1;
            Some([n; 8])
        })
    }
    fn welcome(out: Out) -> (Vip, u64) {
        match &out[0].1 {
            Packet::Welcome { vip, token } => (*vip, *token),
            p => panic!("expected Welcome, got {p:?}"),
        }
    }
    fn sign(token: u64, inner: Packet) -> Packet {
        Packet::Signed { token, inner: Box::new(inner) }
    }
    fn bcast(b: u8) -> Packet {
        Packet::Bcast { vip: [0; 4], src_port: 8086, dst_port: 8086, payload: vec![b] }
    }

    #[test]
    fn serial_checks() {
        let mut s = server();
        assert_eq!(s.handle(addr(1), hello("ZZZ", "r", ""), 0), [(addr(1), Packet::Reject(Reason::UnknownSerial))]);
        assert_eq!(s.handle(addr(1), hello("CCC", "r", ""), 0), [(addr(1), Packet::Reject(Reason::Revoked))]);
        let (vip, tok) = welcome(s.handle(addr(1), hello("AAA", "r", ""), 0));
        assert_eq!(vip, [10, 77, 0, 1]);
        // retry from the same address returns the same session; a second address cannot reuse the serial
        assert_eq!(welcome(s.handle(addr(1), hello("AAA", "r", ""), 1)), (vip, tok));
        assert_eq!(s.handle(addr(2), hello("AAA", "r", ""), 1), [(addr(2), Packet::Reject(Reason::InUse))]);
    }

    #[test]
    fn serial_frees_after_timeout() {
        let mut s = server();
        s.handle(addr(1), hello("AAA", "r", ""), 0);
        s.expire(SESSION_TIMEOUT_SECS);
        assert!(matches!(s.handle(addr(2), hello("AAA", "r", ""), SESSION_TIMEOUT_SECS)[0].1, Packet::Welcome { .. }));
    }

    #[test]
    fn rooms_are_password_protected_and_isolated() {
        let mut s = server();
        let (_, ta) = welcome(s.handle(addr(1), Packet::Hello { serial: "AAA".into(), room: "lan".into(), verifier: room_verifier("lan", "pw") }, 0));
        // wrong password is refused, right one joins
        let wrong = Packet::Hello { serial: "BBB".into(), room: "lan".into(), verifier: room_verifier("lan", "nope") };
        assert_eq!(s.handle(addr(2), wrong, 0), [(addr(2), Packet::Reject(Reason::WrongPassword))]);
        let (vb, tb) = welcome(s.handle(addr(2), Packet::Hello { serial: "BBB".into(), room: "lan".into(), verifier: room_verifier("lan", "pw") }, 0));
        assert_eq!(vb, [10, 77, 0, 2]);
        // a different room gets its own address space and sees nothing from "lan"
        let (vd, td) = welcome(s.handle(addr(3), Packet::Hello { serial: "DDD".into(), room: "other".into(), verifier: room_verifier("other", "") }, 0));
        assert_eq!(vd, [10, 77, 0, 1]);
        assert_eq!(s.handle(addr(1), sign(ta, bcast(1)), 1).iter().map(|o| o.0).collect::<Vec<_>>(), [addr(2)]);
        assert!(s.handle(addr(3), sign(td, bcast(2)), 1).is_empty());
        // unicast cannot cross rooms: .2 exists only in "lan", so a player in "other" reaches nobody
        let cross = Packet::Data { vip: [10, 77, 0, 2], src_port: 8088, dst_port: 8088, payload: vec![] };
        assert!(s.handle(addr(3), sign(td, cross), 1).is_empty());
        // ...while inside "lan" the same address is delivered
        let local = Packet::Data { vip: [10, 77, 0, 1], src_port: 8088, dst_port: 8088, payload: vec![] };
        assert_eq!(s.handle(addr(2), sign(tb, local), 1).iter().map(|o| o.0).collect::<Vec<_>>(), [addr(1)]);
    }

    #[test]
    fn empty_rooms_remove_themselves() {
        let mut s = server();
        let (_, t) = welcome(s.handle(addr(1), hello("AAA", "r1", ""), 0));
        welcome(s.handle(addr(2), hello("BBB", "r2", ""), 0));
        assert_eq!(s.room_count(), 2);
        s.handle(addr(1), sign(t, Packet::Leave), 1); // explicit leave
        assert_eq!(s.room_count(), 1);
        s.expire(SESSION_TIMEOUT_SECS + 1); // timeout
        assert_eq!(s.room_count(), 0);
        // the name is free again, with a new password
        let h = Packet::Hello { serial: "AAA".into(), room: "r2".into(), verifier: room_verifier("r2", "new") };
        assert!(matches!(s.handle(addr(5), h, SESSION_TIMEOUT_SECS + 2)[0].1, Packet::Welcome { .. }));
    }

    #[test]
    fn room_limits_and_names() {
        let mut s = server();
        for bad in ["", " x", "x ", "a/b", "<script>", &"x".repeat(33)] {
            assert_eq!(s.handle(addr(1), hello("AAA", bad, ""), 0)[0].1, Packet::Reject(Reason::BadRoom), "{bad:?}");
        }
        let mut full = Serials::default();
        for i in 0..=ROOM_SIZE {
            full.active.insert(format!("S{i}"));
        }
        let mut s = server();
        s.set_serials(full);
        for i in 0..ROOM_SIZE {
            welcome(s.handle(addr(100 + i as u16), hello(&format!("S{i}"), "big", ""), 0));
        }
        let ninth = hello(&format!("S{ROOM_SIZE}"), "big", "");
        assert_eq!(s.handle(addr(200), ninth, 0)[0].1, Packet::Reject(Reason::RoomFull));
    }

    #[test]
    fn password_guessing_is_rate_limited_per_address() {
        let mut s = server();
        welcome(s.handle(addr(1), Packet::Hello { serial: "AAA".into(), room: "r".into(), verifier: room_verifier("r", "pw") }, 0));
        let guess = |i: u8| Packet::Hello { serial: "BBB".into(), room: "r".into(), verifier: [i; 32] };
        for i in 0..PASSWORD_TRIES as u8 {
            assert_eq!(s.handle(addr(9), guess(i), 1)[0].1, Packet::Reject(Reason::WrongPassword));
        }
        assert_eq!(s.handle(addr(9), guess(99), 2)[0].1, Packet::Reject(Reason::RateLimited));
        // even the right password is refused from that address until the window passes
        let right = Packet::Hello { serial: "BBB".into(), room: "r".into(), verifier: room_verifier("r", "pw") };
        assert_eq!(s.handle(addr(9), right.clone(), 3)[0].1, Packet::Reject(Reason::RateLimited));
        s.expire(PASSWORD_WINDOW_SECS + 2);
        assert!(matches!(s.handle(addr(9), right, PASSWORD_WINDOW_SECS + 2)[0].1, Packet::Welcome { .. }));
    }

    #[test]
    fn unsigned_and_wrongly_signed_traffic_is_ignored() {
        let mut s = server();
        let (_, t) = welcome(s.handle(addr(1), hello("AAA", "r", ""), 0));
        welcome(s.handle(addr(2), hello("BBB", "r", ""), 0));
        assert!(s.handle(addr(1), bcast(1), 1).is_empty(), "unsigned");
        assert!(s.handle(addr(1), sign(t ^ 1, bcast(1)), 1).is_empty(), "bad token: no reply at all");
        assert_eq!(s.handle(addr(7), sign(t, bcast(1)), 1), [(addr(7), Packet::Reject(Reason::NotAuthed))], "unknown source");
        assert_eq!(s.handle(addr(1), sign(t, bcast(1)), 1).len(), 1);
    }

    #[test]
    fn revoking_drops_live_session_and_room() {
        let mut s = server();
        let (_, t) = welcome(s.handle(addr(1), hello("AAA", "r", ""), 0));
        s.set_serials(Serials::parse("!AAA\nBBB"));
        assert_eq!(s.room_count(), 0);
        assert_eq!(s.handle(addr(1), sign(t, Packet::Ping), 1), [(addr(1), Packet::Reject(Reason::NotAuthed))]);
    }

    #[test]
    fn register_needs_a_cookie_and_issues_a_working_serial() {
        let mut s = Server::new(Serials::default());
        s.set_rng(counting_rng());
        let Packet::Challenge { cookie } = s.handle(addr(1), Packet::Register { cookie: [0; 16] }, 0).remove(0).1 else { panic!() };
        assert!(s.take_new_serials().is_empty(), "challenge is stateless: nothing issued yet");
        // a cookie for another address does not work
        assert!(matches!(s.handle(addr_from("127.0.0.2", 1), Packet::Register { cookie }, 0)[0].1, Packet::Challenge { .. }));
        let Packet::Issued { serial } = s.handle(addr(1), Packet::Register { cookie }, 1).remove(0).1 else { panic!() };
        assert_eq!(s.take_new_serials(), [serial.clone()]);
        assert!(matches!(s.handle(addr(1), hello(&serial, "r", ""), 2)[0].1, Packet::Welcome { .. }));
        // a retry returns the same serial; an old cookie stops working after two minutes
        assert_eq!(s.handle(addr(1), Packet::Register { cookie }, 5)[0].1, Packet::Issued { serial });
        assert!(matches!(s.handle(addr(3), Packet::Register { cookie }, 200)[0].1, Packet::Challenge { .. }));
    }

    #[test]
    fn register_is_rate_limited_per_ip_and_can_be_closed() {
        let mut s = Server::new(Serials::default());
        s.set_rng(counting_rng());
        let issue = |s: &mut Server, port: u16, now: u64| {
            let Packet::Challenge { cookie } = s.handle(addr(port), Packet::Register { cookie: [0; 16] }, now).remove(0).1 else { panic!() };
            s.handle(addr(port), Packet::Register { cookie }, now).remove(0).1
        };
        for p in 0..REGISTRATIONS_PER_HOUR as u16 {
            assert!(matches!(issue(&mut s, 100 + p, 0), Packet::Issued { .. }));
        }
        assert_eq!(issue(&mut s, 999, 1), Packet::Reject(Reason::RateLimited));
        s.expire(3601);
        assert!(matches!(issue(&mut s, 999, 3601), Packet::Issued { .. }));
        s.set_open(false);
        assert_eq!(s.handle(addr(1), Packet::Register { cookie: [0; 16] }, 3602)[0].1, Packet::Reject(Reason::RegistrationClosed));
    }

    #[test]
    fn challenge_is_no_bigger_than_the_request() {
        let mut s = server();
        let req = Packet::Register { cookie: [0; 16] };
        let rsp = s.handle(addr(1), req.clone(), 0).remove(0).1;
        assert!(rsp.encode().len() <= req.encode().len(), "no reflection amplification");
        let h = hello("AAA", "r", "");
        assert!(s.handle(addr(1), h.clone(), 0)[0].1.encode().len() <= h.encode().len());
    }

    #[test]
    fn country_filter_silences_blocked_sources() {
        let mut s = server();
        s.set_allow(Box::new(|ip| ip.to_string().starts_with("127.")));
        assert!(s.handle(addr_from("8.8.8.8", 1), hello("AAA", "r", ""), 0).is_empty());
        assert!(s.handle(addr_from("8.8.8.8", 1), Packet::Register { cookie: [0; 16] }, 0).is_empty());
        assert!(matches!(s.handle(addr(1), hello("AAA", "r", ""), 0)[0].1, Packet::Welcome { .. }));
    }

    #[test]
    fn serial_format() {
        assert_eq!(format_serial([0xAB, 0xCD, 0, 1, 2, 3, 4, 5]), "ABCD-0001-0203-0405");
    }
}
