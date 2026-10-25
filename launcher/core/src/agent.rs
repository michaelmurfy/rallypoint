//! Tunnel agent. The engine (patched, `GEN_RELAY`) sends its UDP traffic to a loopback socket here; the agent
//! signs up with the relay if needed, joins the room, and forwards datagrams both ways. Sign-up is invisible to
//! the user: with no stored serial the agent registers and hands the issued serial back through `take_issued`.
//! Engine sockets register with `Bind{port}` (or by sending), keyed by logical port (8086 lobby, 8088 game).
use generals_proto::{is_game_packet, room_verifier, Cookie, Packet, Reason, Vip, GAME_PORTS, LOBBY_PORT};
use std::collections::{HashMap, VecDeque};
use std::io;
use std::net::{SocketAddr, ToSocketAddrs, UdpSocket};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
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
const SILENCE_LIMIT: Duration = Duration::from_secs(20);

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
        }
    }
}

pub struct Agent {
    /// Where the engine should send its traffic (loopback).
    pub local: SocketAddr,
    health: Arc<Mutex<Health>>,
    status: Arc<Mutex<Status>>,
    issued: Arc<Mutex<Option<String>>>,
    token: Arc<AtomicU64>,
    relay: UdpSocket,
    stop: Arc<AtomicBool>,
    threads: Vec<JoinHandle<()>>,
}

impl Agent {
    /// Joins `room` (creating it with `password` if it does not exist). An empty `serial` makes the agent sign
    /// up first; the serial it is issued is available once from `take_issued` and should be stored.
    pub fn start(server: &str, serial: &str, room: &str, password: &str) -> io::Result<Agent> {
        let server = server.to_socket_addrs()?.next().ok_or_else(|| io::Error::other("server address does not resolve"))?;
        let relay = UdpSocket::bind("0.0.0.0:0")?;
        relay.connect(server)?; // also makes the kernel drop datagrams from anyone else
        relay.set_read_timeout(Some(TICK))?;
        let local = UdpSocket::bind("127.0.0.1:0")?;
        local.set_read_timeout(Some(TICK))?;
        let local_addr = local.local_addr()?;

        let status = Arc::new(Mutex::new(Status::Connecting));
        let stop = Arc::new(AtomicBool::new(false));
        let issued: Arc<Mutex<Option<String>>> = Arc::default();
        let token = Arc::new(AtomicU64::new(0));
        let ports: Arc<Mutex<HashMap<u16, SocketAddr>>> = Arc::default();
        let health: Arc<Mutex<Health>> = Arc::default();
        // bytes to / from the relay, for the traffic rates
        let bytes: Arc<[AtomicU64; 2]> = Arc::new([AtomicU64::new(0), AtomicU64::new(0)]);
        let (serial, room, verifier) = (serial.to_string(), room.to_string(), room_verifier(room, password));

        // local -> relay
        let l2r = {
            let (relay, local, status, stop, ports, token, bytes) =
                (relay.try_clone()?, local.try_clone()?, status.clone(), stop.clone(), ports.clone(), token.clone(), bytes.clone());
            std::thread::spawn(move || {
                let mut buf = [0u8; 2048];
                while !stop.load(Ordering::Relaxed) {
                    let Ok((n, from)) = local.recv_from(&mut buf) else { continue };
                    match Packet::decode(&buf[..n]) {
                        // The tunnel is for the game only: its two ports and datagrams the engine made.
                        Some(Packet::Bind { port }) if GAME_PORTS.contains(&port) => {
                            ports.lock().unwrap().insert(port, from);
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
                            if matches!(*status.lock().unwrap(), Status::Connected(_)) {
                                bytes[0].fetch_add(n as u64, Ordering::Relaxed);
                                let signed = Packet::Signed { token: token.load(Ordering::Relaxed), inner: Box::new(p) };
                                let _ = relay.send(&signed.encode());
                            }
                        }
                        _ => {}
                    }
                }
            })
        };

        // relay -> local, plus handshake and keepalive
        let r2l = {
            let (relay, status, stop, issued, token, health, bytes) = (relay.try_clone()?, status.clone(), stop.clone(), issued.clone(), token.clone(), health.clone(), bytes.clone());
            std::thread::spawn(move || {
                let (mut serial, mut cookie): (String, Cookie) = (serial, [0; 16]);
                // Handshake packet: sign-up while we have no serial, otherwise join the room.
                let opener = |serial: &str, cookie: Cookie| {
                    if serial.is_empty() { Packet::Register { cookie } } else { Packet::Hello { serial: serial.into(), room: room.clone(), verifier } }.encode()
                };
                let mut buf = [0u8; 2048];
                let (mut last_rx, mut last_ping, mut last_rate) = (Instant::now(), Instant::now(), (Instant::now(), 0u64, 0u64));
                let _ = relay.send(&opener(&serial, cookie));
                while !stop.load(Ordering::Relaxed) {
                    if let Ok(n) = relay.recv(&mut buf) {
                        last_rx = Instant::now();
                        match Packet::decode(&buf[..n]) {
                            Some(Packet::Welcome { vip, token: t }) => {
                                token.store(t, Ordering::Relaxed);
                                *status.lock().unwrap() = Status::Connected(vip);
                            }
                            Some(Packet::Challenge { cookie: c }) if serial.is_empty() => {
                                cookie = c;
                                let _ = relay.send(&opener(&serial, cookie));
                            }
                            Some(Packet::Issued { serial: s }) if serial.is_empty() => {
                                serial = s.clone();
                                *issued.lock().unwrap() = Some(s);
                                let _ = relay.send(&opener(&serial, cookie));
                            }
                            Some(Packet::Reject(Reason::NotAuthed)) => {
                                // server forgot us (restart/timeout): handshake again
                                *status.lock().unwrap() = Status::Connecting;
                                let _ = relay.send(&opener(&serial, cookie));
                            }
                            Some(Packet::Reject(r)) => {
                                *status.lock().unwrap() = Status::Rejected(r as u8);
                                return;
                            }
                            Some(Packet::Ping) => health.lock().unwrap().pong(Instant::now()),
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
                            // A relay (or anyone spoofing one) only gets to hand the engine game datagrams for its own two ports.
                            Some(Packet::Bcast { dst_port, payload, .. } | Packet::Data { dst_port, payload, .. }) => {
                                if GAME_PORTS.contains(&dst_port) && is_game_packet(&payload) {
                                    if let Some(to) = ports.lock().unwrap().get(&dst_port) {
                                        bytes[1].fetch_add(n as u64, Ordering::Relaxed);
                                        let _ = local.send_to(&buf[..n], to);
                                    }
                                }
                            }
                            _ => {}
                        }
                    }
                    // periodic work (also runs after a read timeout)
                    let connected = matches!(*status.lock().unwrap(), Status::Connected(_));
                    if !connected {
                        let _ = relay.send(&opener(&serial, cookie)); // retry until accepted
                    } else if last_rx.elapsed() > SILENCE_LIMIT {
                        *status.lock().unwrap() = Status::Connecting;
                    } else if last_ping.elapsed() > PING_EVERY {
                        let t = token.load(Ordering::Relaxed);
                        health.lock().unwrap().ping_sent(Instant::now());
                        for inner in [Packet::Ping, Packet::StatsRequest] {
                            let _ = relay.send(&Packet::Signed { token: t, inner: Box::new(inner) }.encode());
                        }
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

        Ok(Agent { local: local_addr, health, status, issued, token, relay, stop, threads: vec![l2r, r2l] })
    }

    /// The serial the server issued to us, once; the caller must store it.
    pub fn take_issued(&self) -> Option<String> {
        self.issued.lock().unwrap().take()
    }

    pub fn status(&self) -> Status {
        *self.status.lock().unwrap()
    }

    /// Latency, loss, traffic and the relay's own numbers; `None` unless connected.
    pub fn diagnostics(&self) -> Option<Diagnostics> {
        matches!(self.status(), Status::Connected(_)).then(|| self.health.lock().unwrap().view())
    }
}

impl Drop for Agent {
    fn drop(&mut self) {
        // Tell the server now so an empty room disappears immediately instead of after the session timeout.
        if matches!(self.status(), Status::Connected(_)) {
            let leave = Packet::Signed { token: self.token.load(Ordering::Relaxed), inner: Box::new(Packet::Leave) };
            let _ = self.relay.send(&leave.encode());
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
