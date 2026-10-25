//! Relay state machine. No sockets or clocks in here: `handle` takes the sender and the time and returns
//! the datagrams to send, so it is testable without a network.
use generals_proto::{Packet, Reason, Vip};
use std::collections::{HashMap, HashSet};
use std::net::{IpAddr, SocketAddr};

pub const SESSION_TIMEOUT_SECS: u64 = 30;
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

struct Session {
    serial: String,
    vip: Vip,
    last_seen: u64,
}

pub type Rng = Box<dyn FnMut() -> Option<[u8; 8]> + Send>;

/// 64 random bits from the OS (Linux/macOS).
pub fn os_random() -> Option<[u8; 8]> {
    use std::io::Read;
    let mut b = [0u8; 8];
    std::fs::File::open("/dev/urandom").ok()?.read_exact(&mut b).ok()?;
    Some(b)
}

pub struct Server {
    serials: Serials,
    sessions: HashMap<SocketAddr, Session>,
    open: bool,
    rng: Rng,
    recent: HashMap<SocketAddr, (u64, String)>,
    by_ip: HashMap<IpAddr, Vec<u64>>,
    new_serials: Vec<String>,
}

type Out = Vec<(SocketAddr, Packet)>;

impl Server {
    /// Open registration: clients without a serial are issued one automatically.
    pub fn new(serials: Serials) -> Server {
        Server {
            serials,
            sessions: HashMap::new(),
            open: true,
            rng: Box::new(os_random),
            recent: HashMap::new(),
            by_ip: HashMap::new(),
            new_serials: vec![],
        }
    }

    /// `false` = only admin-issued serials (Register is refused).
    pub fn set_open(&mut self, open: bool) {
        self.open = open;
    }

    pub fn set_rng(&mut self, rng: Rng) {
        self.rng = rng;
    }

    /// Serials auto-issued since the last call; the caller must persist them.
    pub fn take_new_serials(&mut self) -> Vec<String> {
        std::mem::take(&mut self.new_serials)
    }

    /// Replace the serial list (e.g. after the file changed). Sessions whose serial is no longer valid are dropped.
    pub fn set_serials(&mut self, serials: Serials) {
        self.sessions.retain(|_, s| serials.active.contains(&s.serial) && !serials.revoked.contains(&s.serial));
        self.serials = serials;
    }

    pub fn expire(&mut self, now: u64) {
        self.sessions.retain(|_, s| now.saturating_sub(s.last_seen) < SESSION_TIMEOUT_SECS);
        self.recent.retain(|_, (t, _)| now.saturating_sub(*t) < REGISTER_DEDUP_SECS);
        self.by_ip.retain(|_, h| {
            h.retain(|t| now.saturating_sub(*t) < 3600);
            !h.is_empty()
        });
    }

    fn register(&mut self, from: SocketAddr, now: u64) -> Packet {
        if !self.open {
            return Packet::Reject(Reason::RegistrationClosed);
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
            return Packet::Reject(Reason::Full); // no entropy: refuse rather than issue a guessable serial
        };
        let serial = format_serial(bytes);
        hist.push(now);
        self.serials.active.insert(serial.clone());
        self.recent.insert(from, (now, serial.clone()));
        self.new_serials.push(serial.clone());
        Packet::Issued { serial }
    }

    pub fn handle(&mut self, from: SocketAddr, pkt: Packet, now: u64) -> Out {
        // ponytail: sessions are keyed by source address, so a spoofed source can inject traffic.
        // Add a per-session token in Welcome if that matters.
        match pkt {
            Packet::Hello { serial } => return vec![(from, self.hello(from, serial, now))],
            Packet::Register => return vec![(from, self.register(from, now))],
            _ => {}
        }
        let Some(me) = self.sessions.get_mut(&from) else {
            return vec![(from, Packet::Reject(Reason::NotAuthed))];
        };
        me.last_seen = now;
        let my_vip = me.vip;
        match pkt {
            Packet::Ping => vec![(from, Packet::Ping)],
            Packet::Bcast { src_port, dst_port, payload, .. } => self
                .sessions
                .iter()
                .filter(|(a, _)| **a != from)
                .map(|(a, _)| (*a, Packet::Bcast { vip: my_vip, src_port, dst_port, payload: payload.clone() }))
                .collect(),
            Packet::Data { vip, src_port, dst_port, payload } => self
                .sessions
                .iter()
                .filter(|(a, s)| **a != from && s.vip == vip)
                .map(|(a, _)| (*a, Packet::Data { vip: my_vip, src_port, dst_port, payload: payload.clone() }))
                .collect(),
            _ => vec![],
        }
    }

    fn hello(&mut self, from: SocketAddr, serial: String, now: u64) -> Packet {
        if self.serials.revoked.contains(&serial) {
            return Packet::Reject(Reason::Revoked);
        }
        if !self.serials.active.contains(&serial) {
            return Packet::Reject(Reason::UnknownSerial);
        }
        // Same address re-sending Hello is a retry; a different address using a live serial is a duplicate.
        if let Some(s) = self.sessions.get_mut(&from) {
            if s.serial == serial {
                s.last_seen = now;
                return Packet::Welcome { vip: s.vip };
            }
        }
        if self.sessions.iter().any(|(a, s)| *a != from && s.serial == serial) {
            return Packet::Reject(Reason::InUse);
        }
        let used: HashSet<u8> = self.sessions.values().map(|s| s.vip[3]).collect();
        let Some(n) = (1..=254u8).find(|n| !used.contains(n)) else {
            return Packet::Reject(Reason::Full);
        };
        let vip = [VIP_PREFIX[0], VIP_PREFIX[1], VIP_PREFIX[2], n];
        self.sessions.insert(from, Session { serial, vip, last_seen: now });
        Packet::Welcome { vip }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn addr(p: u16) -> SocketAddr {
        format!("127.0.0.1:{p}").parse().unwrap()
    }
    fn hello(s: &str) -> Packet {
        Packet::Hello { serial: s.into() }
    }
    fn server() -> Server {
        Server::new(Serials::parse("# c\nAAA first\nBBB\n!CCC\n"))
    }

    #[test]
    fn serial_checks() {
        let mut s = server();
        assert_eq!(s.handle(addr(1), hello("ZZZ"), 0), [(addr(1), Packet::Reject(Reason::UnknownSerial))]);
        assert_eq!(s.handle(addr(1), hello("CCC"), 0), [(addr(1), Packet::Reject(Reason::Revoked))]);
        assert_eq!(s.handle(addr(1), hello("AAA"), 0), [(addr(1), Packet::Welcome { vip: [10, 77, 0, 1] })]);
        // retry from the same address is fine, a second address is not
        assert_eq!(s.handle(addr(1), hello("AAA"), 1), [(addr(1), Packet::Welcome { vip: [10, 77, 0, 1] })]);
        assert_eq!(s.handle(addr(2), hello("AAA"), 1), [(addr(2), Packet::Reject(Reason::InUse))]);
    }

    #[test]
    fn serial_frees_after_timeout() {
        let mut s = server();
        s.handle(addr(1), hello("AAA"), 0);
        s.expire(SESSION_TIMEOUT_SECS);
        assert!(matches!(s.handle(addr(2), hello("AAA"), SESSION_TIMEOUT_SECS)[0].1, Packet::Welcome { .. }));
    }

    #[test]
    fn unauthenticated_traffic_is_rejected() {
        let mut s = server();
        let out = s.handle(addr(9), Packet::Bcast { vip: [0; 4], src_port: 8086, dst_port: 8086, payload: vec![1] }, 0);
        assert_eq!(out, [(addr(9), Packet::Reject(Reason::NotAuthed))]);
    }

    #[test]
    fn relays_broadcast_and_unicast() {
        let mut s = server();
        s.handle(addr(1), hello("AAA"), 0); // vip .1
        s.handle(addr(2), hello("BBB"), 0); // vip .2
        let b = s.handle(addr(1), Packet::Bcast { vip: [0; 4], src_port: 8086, dst_port: 8086, payload: vec![7] }, 1);
        assert_eq!(b, [(addr(2), Packet::Bcast { vip: [10, 77, 0, 1], src_port: 8086, dst_port: 8086, payload: vec![7] })]);
        let d = s.handle(addr(2), Packet::Data { vip: [10, 77, 0, 1], src_port: 8088, dst_port: 8088, payload: vec![8] }, 1);
        assert_eq!(d, [(addr(1), Packet::Data { vip: [10, 77, 0, 2], src_port: 8088, dst_port: 8088, payload: vec![8] })]);
        // unknown destination goes nowhere
        assert!(s.handle(addr(2), Packet::Data { vip: [10, 77, 0, 99], src_port: 8088, dst_port: 8088, payload: vec![] }, 1).is_empty());
    }

    #[test]
    fn revoking_drops_live_session() {
        let mut s = server();
        s.handle(addr(1), hello("AAA"), 0);
        s.set_serials(Serials::parse("!AAA\nBBB"));
        assert_eq!(s.handle(addr(1), Packet::Ping, 1), [(addr(1), Packet::Reject(Reason::NotAuthed))]);
    }

    fn counting_rng() -> Rng {
        let mut n = 0u8;
        Box::new(move || {
            n += 1;
            Some([n; 8])
        })
    }

    #[test]
    fn register_issues_a_working_serial() {
        let mut s = Server::new(Serials::default());
        s.set_rng(counting_rng());
        let Packet::Issued { serial } = s.handle(addr(1), Packet::Register, 0).remove(0).1 else { panic!() };
        assert_eq!(serial, "0101-0101-0101-0101");
        assert_eq!(s.take_new_serials(), [serial.clone()]);
        assert!(s.take_new_serials().is_empty());
        assert!(matches!(s.handle(addr(1), hello(&serial), 1)[0].1, Packet::Welcome { .. }));
    }

    #[test]
    fn register_retry_returns_same_serial_and_costs_one_quota() {
        let mut s = Server::new(Serials::default());
        s.set_rng(counting_rng());
        let a = s.handle(addr(1), Packet::Register, 0);
        assert_eq!(s.handle(addr(1), Packet::Register, 5), a);
        assert_eq!(s.take_new_serials().len(), 1);
    }

    #[test]
    fn register_is_rate_limited_per_ip() {
        let mut s = Server::new(Serials::default());
        s.set_rng(counting_rng());
        for p in 0..REGISTRATIONS_PER_HOUR as u16 {
            assert!(matches!(s.handle(addr(100 + p), Packet::Register, 0)[0].1, Packet::Issued { .. }));
        }
        assert_eq!(s.handle(addr(999), Packet::Register, 1)[0].1, Packet::Reject(Reason::RateLimited));
        s.expire(3601);
        assert!(matches!(s.handle(addr(999), Packet::Register, 3601)[0].1, Packet::Issued { .. }));
    }

    #[test]
    fn closed_server_refuses_register() {
        let mut s = server();
        s.set_open(false);
        assert_eq!(s.handle(addr(1), Packet::Register, 0)[0].1, Packet::Reject(Reason::RegistrationClosed));
    }

    #[test]
    fn serial_format() {
        assert_eq!(format_serial([0xAB, 0xCD, 0, 1, 2, 3, 4, 5]), "ABCD-0001-0203-0405");
    }
}
