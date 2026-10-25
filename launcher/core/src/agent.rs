//! Tunnel agent. The engine (patched, `GEN_RELAY`) sends its UDP traffic to a loopback socket here; the agent
//! signs up with the relay if needed, joins the room, and forwards datagrams both ways. Sign-up is invisible to
//! the user: with no stored serial the agent registers and hands the issued serial back through `take_issued`.
//! Engine sockets register with `Bind{port}` (or by sending), keyed by logical port (8086 lobby, 8088 game).
use generals_proto::{room_verifier, Cookie, Packet, Reason, Vip};
use std::collections::HashMap;
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
const PING_EVERY: Duration = Duration::from_secs(5);
const SILENCE_LIMIT: Duration = Duration::from_secs(20);

pub struct Agent {
    /// Where the engine should send its traffic (loopback).
    pub local: SocketAddr,
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
        let (serial, room, verifier) = (serial.to_string(), room.to_string(), room_verifier(room, password));

        // local -> relay
        let l2r = {
            let (relay, local, status, stop, ports, token) =
                (relay.try_clone()?, local.try_clone()?, status.clone(), stop.clone(), ports.clone(), token.clone());
            std::thread::spawn(move || {
                let mut buf = [0u8; 2048];
                while !stop.load(Ordering::Relaxed) {
                    let Ok((n, from)) = local.recv_from(&mut buf) else { continue };
                    match Packet::decode(&buf[..n]) {
                        Some(Packet::Bind { port }) => {
                            ports.lock().unwrap().insert(port, from);
                        }
                        Some(p @ (Packet::Bcast { .. } | Packet::Data { .. })) => {
                            let src_port = match &p {
                                Packet::Bcast { src_port, .. } | Packet::Data { src_port, .. } => *src_port,
                                _ => unreachable!(),
                            };
                            ports.lock().unwrap().insert(src_port, from);
                            if matches!(*status.lock().unwrap(), Status::Connected(_)) {
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
            let (relay, status, stop, issued, token) = (relay.try_clone()?, status.clone(), stop.clone(), issued.clone(), token.clone());
            std::thread::spawn(move || {
                let (mut serial, mut cookie): (String, Cookie) = (serial, [0; 16]);
                // Handshake packet: sign-up while we have no serial, otherwise join the room.
                let opener = |serial: &str, cookie: Cookie| {
                    if serial.is_empty() { Packet::Register { cookie } } else { Packet::Hello { serial: serial.into(), room: room.clone(), verifier } }.encode()
                };
                let mut buf = [0u8; 2048];
                let (mut last_rx, mut last_ping) = (Instant::now(), Instant::now());
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
                            Some(p @ (Packet::Bcast { .. } | Packet::Data { .. })) => {
                                let dst = match &p {
                                    Packet::Bcast { dst_port, .. } | Packet::Data { dst_port, .. } => *dst_port,
                                    _ => unreachable!(),
                                };
                                if let Some(to) = ports.lock().unwrap().get(&dst) {
                                    let _ = local.send_to(&buf[..n], to);
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
                        let ping = Packet::Signed { token: token.load(Ordering::Relaxed), inner: Box::new(Packet::Ping) };
                        let _ = relay.send(&ping.encode());
                        last_ping = Instant::now();
                    }
                }
            })
        };

        Ok(Agent { local: local_addr, status, issued, token, relay, stop, threads: vec![l2r, r2l] })
    }

    /// The serial the server issued to us, once; the caller must store it.
    pub fn take_issued(&self) -> Option<String> {
        self.issued.lock().unwrap().take()
    }

    pub fn status(&self) -> Status {
        *self.status.lock().unwrap()
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
