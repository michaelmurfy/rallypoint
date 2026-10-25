//! Tunnel agent. The engine (patched, `GEN_RELAY`) sends its UDP traffic to a loopback socket here;
//! the agent authenticates to the relay with the serial and forwards datagrams verbatim both ways.
//! Engine sockets register with `Bind{port}` (or by sending), keyed by their logical port (8086 lobby, 8088 game).
use generals_proto::{Packet, Reason, Vip};
use std::collections::HashMap;
use std::io;
use std::net::{SocketAddr, ToSocketAddrs, UdpSocket};
use std::sync::atomic::{AtomicBool, Ordering};
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
    stop: Arc<AtomicBool>,
    threads: Vec<JoinHandle<()>>,
}

impl Agent {
    /// An empty `serial` asks the server to register us; the issued serial is available from `take_issued`.
    pub fn start(server: &str, serial: &str) -> io::Result<Agent> {
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
        let ports: Arc<Mutex<HashMap<u16, SocketAddr>>> = Arc::default();
        let serial = serial.to_string();

        // local -> relay
        let l2r = {
            let (relay, local, status, stop, ports) = (relay.try_clone()?, local.try_clone()?, status.clone(), stop.clone(), ports.clone());
            std::thread::spawn(move || {
                let mut buf = [0u8; 2048];
                while !stop.load(Ordering::Relaxed) {
                    let Ok((n, from)) = local.recv_from(&mut buf) else { continue };
                    match Packet::decode(&buf[..n]) {
                        Some(Packet::Bind { port }) => {
                            ports.lock().unwrap().insert(port, from);
                        }
                        Some(Packet::Bcast { src_port, .. }) | Some(Packet::Data { src_port, .. }) => {
                            ports.lock().unwrap().insert(src_port, from);
                            if matches!(*status.lock().unwrap(), Status::Connected(_)) {
                                let _ = relay.send(&buf[..n]);
                            }
                        }
                        _ => {}
                    }
                }
            })
        };

        // relay -> local, plus handshake and keepalive
        let r2l = {
            let (status, stop, issued) = (status.clone(), stop.clone(), issued.clone());
            std::thread::spawn(move || {
                let mut serial = serial;
                // Handshake packet: Hello with our serial, or Register while we have none.
                let opener = |serial: &str| if serial.is_empty() { Packet::Register } else { Packet::Hello { serial: serial.into() } }.encode();
                let mut buf = [0u8; 2048];
                let (mut last_rx, mut last_ping) = (Instant::now(), Instant::now());
                let _ = relay.send(&opener(&serial));
                while !stop.load(Ordering::Relaxed) {
                    match relay.recv(&mut buf) {
                        Ok(n) => {
                            last_rx = Instant::now();
                            match Packet::decode(&buf[..n]) {
                                Some(Packet::Welcome { vip }) => *status.lock().unwrap() = Status::Connected(vip),
                                Some(Packet::Issued { serial: s }) if serial.is_empty() => {
                                    serial = s.clone();
                                    *issued.lock().unwrap() = Some(s);
                                    let _ = relay.send(&opener(&serial));
                                }
                                Some(Packet::Reject(Reason::NotAuthed)) => {
                                    // server forgot us (restart/timeout): handshake again
                                    *status.lock().unwrap() = Status::Connecting;
                                    let _ = relay.send(&opener(&serial));
                                }
                                Some(Packet::Reject(r)) => {
                                    *status.lock().unwrap() = Status::Rejected(r as u8);
                                    return;
                                }
                                Some(Packet::Bcast { dst_port, .. }) | Some(Packet::Data { dst_port, .. }) => {
                                    if let Some(to) = ports.lock().unwrap().get(&dst_port) {
                                        let _ = local.send_to(&buf[..n], to);
                                    }
                                }
                                _ => {}
                            }
                        }
                        Err(_) => {} // read timeout: fall through to the periodic work
                    }
                    let connected = matches!(*status.lock().unwrap(), Status::Connected(_));
                    if !connected {
                        let _ = relay.send(&opener(&serial)); // retry until accepted
                    } else if last_rx.elapsed() > SILENCE_LIMIT {
                        *status.lock().unwrap() = Status::Connecting;
                    } else if last_ping.elapsed() > PING_EVERY {
                        let _ = relay.send(&Packet::Ping.encode());
                        last_ping = Instant::now();
                    }
                }
            })
        };

        Ok(Agent { local: local_addr, status, issued, stop, threads: vec![l2r, r2l] })
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
        self.stop.store(true, Ordering::Relaxed);
        for t in self.threads.drain(..) {
            let _ = t.join(); // threads wake within one TICK
        }
    }
}
