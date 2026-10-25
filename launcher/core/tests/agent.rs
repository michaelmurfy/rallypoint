//! Two agents against the real relay state machine over loopback UDP, with fake engine sockets.
use generals_core::agent::{Agent, Status};
use generals_proto::{Packet, Reason};
use generals_server::{Serials, Server};
use std::net::UdpSocket;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

fn spawn_relay(serials: &str) -> (String, Arc<AtomicBool>) {
    spawn_relay_with(serials, true)
}

fn spawn_relay_with(serials: &str, open: bool) -> (String, Arc<AtomicBool>) {
    let sock = UdpSocket::bind("127.0.0.1:0").unwrap();
    sock.set_read_timeout(Some(Duration::from_millis(100))).unwrap();
    let addr = sock.local_addr().unwrap().to_string();
    let stop = Arc::new(AtomicBool::new(false));
    let (s2, serials) = (stop.clone(), Serials::parse(serials));
    std::thread::spawn(move || {
        let mut server = Server::new(serials);
        server.set_open(open);
        let mut n = 0u8;
        server.set_rng(Box::new(move || {
            n += 1; // deterministic, so the test does not depend on OS randomness
            Some([n; 8])
        }));
        let start = Instant::now();
        let mut buf = [0u8; 2048];
        while !s2.load(Ordering::Relaxed) {
            if let Ok((n, from)) = sock.recv_from(&mut buf) {
                if let Some(p) = Packet::decode(&buf[..n]) {
                    for (to, out) in server.handle(from, p, start.elapsed().as_secs()) {
                        sock.send_to(&out.encode(), to).unwrap();
                    }
                }
            }
        }
    });
    (addr, stop)
}

fn wait(a: &Agent, want: impl Fn(Status) -> bool) -> Status {
    let t = Instant::now();
    while t.elapsed() < Duration::from_secs(5) {
        if want(a.status()) {
            break;
        }
        std::thread::sleep(Duration::from_millis(20));
    }
    a.status()
}

fn start(server: &str, serial: &str) -> Agent {
    Agent::start(server, serial, "room", "pw").unwrap()
}

fn engine_socket(agent: &Agent, port: u16) -> UdpSocket {
    let s = UdpSocket::bind("127.0.0.1:0").unwrap();
    s.set_read_timeout(Some(Duration::from_secs(3))).unwrap();
    s.send_to(&Packet::Bind { port }.encode(), agent.local).unwrap();
    s
}

#[test]
fn handshake_and_relay_between_two_clients() {
    let (server, stop) = spawn_relay("AAA\nBBB\n!CCC\n");
    let a = start(&server, "AAA");
    let b = start(&server, "BBB");
    let va = match wait(&a, |s| matches!(s, Status::Connected(_))) {
        Status::Connected(v) => v,
        s => panic!("a: {s:?}"),
    };
    assert!(matches!(wait(&b, |s| matches!(s, Status::Connected(_))), Status::Connected(_)));

    // Each client has a lobby socket (8086) and a game socket (8088).
    let (a_lobby, a_game) = (engine_socket(&a, 8086), engine_socket(&a, 8088));
    let (b_lobby, b_game) = (engine_socket(&b, 8086), engine_socket(&b, 8088));
    std::thread::sleep(Duration::from_millis(200)); // let Bind packets land

    let mut buf = [0u8; 2048];
    // A's lobby broadcast reaches B's lobby socket only.
    a_lobby.send_to(&Packet::Bcast { vip: [0; 4], src_port: 8086, dst_port: 8086, payload: b"hi".to_vec() }.encode(), a.local).unwrap();
    let n = b_lobby.recv(&mut buf).unwrap();
    assert_eq!(Packet::decode(&buf[..n]), Some(Packet::Bcast { vip: va, src_port: 8086, dst_port: 8086, payload: b"hi".to_vec() }));

    // B's game packet addressed to A's vip reaches A's game socket, not its lobby socket.
    b_game.send_to(&Packet::Data { vip: va, src_port: 8088, dst_port: 8088, payload: b"cmd".to_vec() }.encode(), b.local).unwrap();
    let n = a_game.recv(&mut buf).unwrap();
    assert!(matches!(Packet::decode(&buf[..n]), Some(Packet::Data { payload, dst_port: 8088, .. }) if payload == b"cmd"));
    a_lobby.set_read_timeout(Some(Duration::from_millis(300))).unwrap();
    assert!(a_lobby.recv(&mut buf).is_err());
    drop(b_lobby);
    stop.store(true, Ordering::Relaxed);
}

#[test]
fn bad_and_duplicate_serials_are_rejected() {
    let (server, stop) = spawn_relay("AAA\n!CCC\n");
    let bad = start(&server, "nope");
    assert_eq!(wait(&bad, |s| matches!(s, Status::Rejected(_))), Status::Rejected(Reason::UnknownSerial as u8));
    let revoked = start(&server, "CCC");
    assert_eq!(wait(&revoked, |s| matches!(s, Status::Rejected(_))), Status::Rejected(Reason::Revoked as u8));
    let first = start(&server, "AAA");
    wait(&first, |s| matches!(s, Status::Connected(_)));
    let second = start(&server, "AAA");
    assert_eq!(wait(&second, |s| matches!(s, Status::Rejected(_))), Status::Rejected(Reason::InUse as u8));
    stop.store(true, Ordering::Relaxed);
}

#[test]
fn client_without_serial_gets_one_automatically() {
    let (server, stop) = spawn_relay_with("", true);
    let a = start(&server, "");
    assert!(matches!(wait(&a, |s| matches!(s, Status::Connected(_))), Status::Connected(_)));
    let serial = a.take_issued().expect("serial issued");
    assert_eq!(serial.len(), 19); // XXXX-XXXX-XXXX-XXXX
    assert_eq!(a.take_issued(), None); // handed out once

    // The stored serial is what the next launch presents; the same serial cannot be used twice at once.
    let again = start(&server, &serial);
    assert_eq!(wait(&again, |s| matches!(s, Status::Rejected(_))), Status::Rejected(Reason::InUse as u8));
    drop(a);

    // A second client gets a different serial.
    let b = start(&server, "");
    wait(&b, |s| matches!(s, Status::Connected(_)));
    let second = b.take_issued().expect("second client issued a serial");
    assert_ne!(second, serial);
    stop.store(true, Ordering::Relaxed);
}

#[test]
fn closed_server_refuses_registration() {
    let (server, stop) = spawn_relay_with("", false);
    let a = start(&server, "");
    assert_eq!(wait(&a, |s| matches!(s, Status::Rejected(_))), Status::Rejected(Reason::RegistrationClosed as u8));
    stop.store(true, Ordering::Relaxed);
}

#[test]
fn rooms_are_separate_and_passworded_and_vanish_when_empty() {
    let (server, stop) = spawn_relay("AAA\nBBB\nDDD\nEEE\n");
    let a = Agent::start(&server, "AAA", "alpha", "secret").unwrap();
    wait(&a, |s| matches!(s, Status::Connected(_)));
    // wrong password is refused, right password joins
    let bad = Agent::start(&server, "BBB", "alpha", "wrong").unwrap();
    assert_eq!(wait(&bad, |s| matches!(s, Status::Rejected(_))), Status::Rejected(Reason::WrongPassword as u8));
    let b = Agent::start(&server, "DDD", "alpha", "secret").unwrap();
    assert!(matches!(wait(&b, |s| matches!(s, Status::Connected(_))), Status::Connected(_)));
    // a client in another room never sees alpha's broadcast
    let other = Agent::start(&server, "EEE", "beta", "").unwrap();
    wait(&other, |s| matches!(s, Status::Connected(_)));
    let (a_lobby, b_lobby, o_lobby) = (engine_socket(&a, 8086), engine_socket(&b, 8086), engine_socket(&other, 8086));
    std::thread::sleep(Duration::from_millis(200));
    a_lobby.send_to(&Packet::Bcast { vip: [0; 4], src_port: 8086, dst_port: 8086, payload: b"x".to_vec() }.encode(), a.local).unwrap();
    let mut buf = [0u8; 2048];
    assert!(b_lobby.recv(&mut buf).is_ok());
    o_lobby.set_read_timeout(Some(Duration::from_millis(400))).unwrap();
    assert!(o_lobby.recv(&mut buf).is_err());

    // everyone leaves: the room is gone, so its name can be reused with a different password
    drop((a, b));
    std::thread::sleep(Duration::from_millis(300));
    let reuse = Agent::start(&server, "AAA", "alpha", "brand new").unwrap();
    assert!(matches!(wait(&reuse, |s| matches!(s, Status::Connected(_))), Status::Connected(_)));
    stop.store(true, Ordering::Relaxed);
}
