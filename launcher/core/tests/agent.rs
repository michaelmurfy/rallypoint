//! Two agents against the real relay state machine over loopback UDP, with fake engine sockets.
use generals_core::agent::{Agent, Status};
use generals_proto::{Packet, Reason};
use generals_server::{Serials, Server};
use std::net::UdpSocket;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

fn spawn_relay(serials: &str) -> (String, Arc<AtomicBool>) {
    let sock = UdpSocket::bind("127.0.0.1:0").unwrap();
    sock.set_read_timeout(Some(Duration::from_millis(100))).unwrap();
    let addr = sock.local_addr().unwrap().to_string();
    let stop = Arc::new(AtomicBool::new(false));
    let (s2, serials) = (stop.clone(), Serials::parse(serials));
    std::thread::spawn(move || {
        let mut server = Server::new(serials);
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

fn engine_socket(agent: &Agent, port: u16) -> UdpSocket {
    let s = UdpSocket::bind("127.0.0.1:0").unwrap();
    s.set_read_timeout(Some(Duration::from_secs(3))).unwrap();
    s.send_to(&Packet::Bind { port }.encode(), agent.local).unwrap();
    s
}

#[test]
fn handshake_and_relay_between_two_clients() {
    let (server, stop) = spawn_relay("AAA\nBBB\n!CCC\n");
    let a = Agent::start(&server, "AAA").unwrap();
    let b = Agent::start(&server, "BBB").unwrap();
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
    let bad = Agent::start(&server, "nope").unwrap();
    assert_eq!(wait(&bad, |s| matches!(s, Status::Rejected(_))), Status::Rejected(Reason::UnknownSerial as u8));
    let revoked = Agent::start(&server, "CCC").unwrap();
    assert_eq!(wait(&revoked, |s| matches!(s, Status::Rejected(_))), Status::Rejected(Reason::Revoked as u8));
    let first = Agent::start(&server, "AAA").unwrap();
    wait(&first, |s| matches!(s, Status::Connected(_)));
    let second = Agent::start(&server, "AAA").unwrap();
    assert_eq!(wait(&second, |s| matches!(s, Status::Rejected(_))), Status::Rejected(Reason::InUse as u8));
    stop.store(true, Ordering::Relaxed);
}
