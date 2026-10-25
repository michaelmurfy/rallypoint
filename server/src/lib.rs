//! Relay state machine. No sockets or clocks in here: `handle` takes the sender and the time and returns
//! the datagrams to send, so it is testable without a network.
use generals_proto::{Packet, Reason, Vip};
use std::collections::{HashMap, HashSet};
use std::net::SocketAddr;

pub const SESSION_TIMEOUT_SECS: u64 = 30;
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

#[derive(Default)]
pub struct Server {
    serials: Serials,
    sessions: HashMap<SocketAddr, Session>,
}

type Out = Vec<(SocketAddr, Packet)>;

impl Server {
    pub fn new(serials: Serials) -> Server {
        Server { serials, sessions: HashMap::new() }
    }

    /// Replace the serial list (e.g. after the file changed). Sessions whose serial is no longer valid are dropped.
    pub fn set_serials(&mut self, serials: Serials) {
        self.sessions.retain(|_, s| serials.active.contains(&s.serial) && !serials.revoked.contains(&s.serial));
        self.serials = serials;
    }

    pub fn expire(&mut self, now: u64) {
        self.sessions.retain(|_, s| now.saturating_sub(s.last_seen) < SESSION_TIMEOUT_SECS);
    }

    pub fn handle(&mut self, from: SocketAddr, pkt: Packet, now: u64) -> Out {
        // ponytail: sessions are keyed by source address, so a spoofed source can inject traffic.
        // Add a per-session token in Welcome if that matters.
        if let Packet::Hello { serial } = pkt {
            return vec![(from, self.hello(from, serial, now))];
        }
        let Some(me) = self.sessions.get_mut(&from) else {
            return vec![(from, Packet::Reject(Reason::NotAuthed))];
        };
        me.last_seen = now;
        let my_vip = me.vip;
        match pkt {
            Packet::Ping => vec![(from, Packet::Ping)],
            Packet::Bcast { payload, .. } => self
                .sessions
                .iter()
                .filter(|(a, _)| **a != from)
                .map(|(a, _)| (*a, Packet::Bcast { vip: my_vip, payload: payload.clone() }))
                .collect(),
            Packet::Data { vip, payload } => self
                .sessions
                .iter()
                .filter(|(a, s)| **a != from && s.vip == vip)
                .map(|(a, _)| (*a, Packet::Data { vip: my_vip, payload: payload.clone() }))
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
        let out = s.handle(addr(9), Packet::Bcast { vip: [0; 4], payload: vec![1] }, 0);
        assert_eq!(out, [(addr(9), Packet::Reject(Reason::NotAuthed))]);
    }

    #[test]
    fn relays_broadcast_and_unicast() {
        let mut s = server();
        s.handle(addr(1), hello("AAA"), 0); // vip .1
        s.handle(addr(2), hello("BBB"), 0); // vip .2
        let b = s.handle(addr(1), Packet::Bcast { vip: [0; 4], payload: vec![7] }, 1);
        assert_eq!(b, [(addr(2), Packet::Bcast { vip: [10, 77, 0, 1], payload: vec![7] })]);
        let d = s.handle(addr(2), Packet::Data { vip: [10, 77, 0, 1], payload: vec![8] }, 1);
        assert_eq!(d, [(addr(1), Packet::Data { vip: [10, 77, 0, 2], payload: vec![8] })]);
        // unknown destination goes nowhere
        assert!(s.handle(addr(2), Packet::Data { vip: [10, 77, 0, 99], payload: vec![] }, 1).is_empty());
    }

    #[test]
    fn revoking_drops_live_session() {
        let mut s = server();
        s.handle(addr(1), hello("AAA"), 0);
        s.set_serials(Serials::parse("!AAA\nBBB"));
        assert_eq!(s.handle(addr(1), Packet::Ping, 1), [(addr(1), Packet::Reject(Reason::NotAuthed))]);
    }

    #[test]
    fn serial_format() {
        assert_eq!(format_serial([0xAB, 0xCD, 0, 1, 2, 3, 4, 5]), "ABCD-0001-0203-0405");
    }
}
