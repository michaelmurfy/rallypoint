//! Two agents against the real relay state machine over loopback UDP, with fake engine sockets.
use generals_core::agent::{Agent, Status};
use generals_core::ra2lobby::{Lobby, RoomSettings, Seat};
use generals_proto::secure::{self, Key};
use generals_proto::{game_packet, Packet, Reason};
use generals_server::{Serials, Server};
use std::net::UdpSocket;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

fn spawn_relay(serials: &str) -> (String, Arc<AtomicBool>) {
    spawn_relay_with(serials, true)
}

fn spawn_relay_with(serials: &str, open: bool) -> (String, Arc<AtomicBool>) {
    let (addr, stop, _, _) = spawn_relay_full(serials, open);
    (addr, stop)
}

type Tap = Arc<Mutex<Vec<Vec<u8>>>>;

/// The relay on loopback. Also returns its identity key, and a record of every datagram it received or sent
/// (what an eavesdropper on the path would see).
fn spawn_relay_full(serials: &str, open: bool) -> (String, Arc<AtomicBool>, Key, Tap) {
    let sock = UdpSocket::bind("127.0.0.1:0").unwrap();
    sock.set_read_timeout(Some(Duration::from_millis(100))).unwrap();
    let addr = sock.local_addr().unwrap().to_string();
    let stop = Arc::new(AtomicBool::new(false));
    let tap: Tap = Arc::default();
    let mut server = Server::new(Serials::parse(serials));
    server.set_open(open);
    let mut n = 0u64;
    server.set_rng(Box::new(move || {
        n += 1; // deterministic, so the test does not depend on OS randomness
        Some(n.to_be_bytes())
    }));
    let key = server.public_key();
    let (s2, t2) = (stop.clone(), tap.clone());
    std::thread::spawn(move || {
        let start = Instant::now();
        let mut buf = [0u8; 2048];
        while !s2.load(Ordering::Relaxed) {
            if let Ok((n, from)) = sock.recv_from(&mut buf) {
                t2.lock().unwrap().push(buf[..n].to_vec());
                for (to, out) in server.handle(from, &buf[..n], start.elapsed().as_secs()) {
                    t2.lock().unwrap().push(out.clone());
                    sock.send_to(&out, to).unwrap();
                }
            }
        }
    });
    (addr, stop, key, tap)
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

/// First visit: the agent learns the relay's key.
fn start(server: &str, serial: &str) -> Agent {
    Agent::start(server, serial, "room", "pw", None).unwrap()
}

/// The token the launcher gives its engine (`GEN_RELAY_TOKEN`).
fn token_of(agent: &Agent) -> [u8; 16] {
    let hex = agent.token_hex();
    std::array::from_fn(|i| u8::from_str_radix(&hex[i * 2..i * 2 + 2], 16).unwrap())
}

fn engine_socket(agent: &Agent, port: u16) -> UdpSocket {
    let s = UdpSocket::bind("127.0.0.1:0").unwrap();
    s.set_read_timeout(Some(Duration::from_secs(3))).unwrap();
    s.send_to(&Packet::Auth { token: token_of(agent) }.encode(), agent.local).unwrap();
    s.send_to(&Packet::Bind { port }.encode(), agent.local).unwrap();
    s
}

#[test]
fn another_local_process_cannot_take_over_the_tunnel() {
    let (server, stop) = spawn_relay("AAA\nBBB\n");
    let a = start(&server, "AAA");
    let b = start(&server, "BBB");
    let va = match wait(&a, |s| matches!(s, Status::Connected(_))) {
        Status::Connected(v) => v,
        s => panic!("a: {s:?}"),
    };
    wait(&b, |s| matches!(s, Status::Connected(_)));
    let ea = engine_socket(&a, 8088);
    let eb = engine_socket(&b, 8088);
    // someone else on this machine tries to claim the port, with no token and with a wrong one
    let intruder = UdpSocket::bind("127.0.0.1:0").unwrap();
    intruder.set_read_timeout(Some(Duration::from_millis(400))).unwrap();
    intruder.send_to(&Packet::Bind { port: 8088 }.encode(), a.local).unwrap();
    intruder.send_to(&Packet::Data { vip: [10, 77, 0, 9], src_port: 8088, dst_port: 8088, payload: game_packet(b"x") }.encode(), a.local).unwrap();
    intruder.send_to(&Packet::Auth { token: [0; 16] }.encode(), a.local).unwrap();
    intruder.send_to(&Packet::Bind { port: 8088 }.encode(), a.local).unwrap();
    std::thread::sleep(Duration::from_millis(200));
    eb.send_to(&Packet::Data { vip: va, src_port: 8088, dst_port: 8088, payload: game_packet(b"orders") }.encode(), b.local).unwrap();
    let mut buf = [0u8; 2048];
    let n = ea.recv(&mut buf).unwrap();
    assert!(matches!(Packet::decode(&buf[..n]), Some(Packet::Data { payload, .. }) if payload == game_packet(b"orders")), "the engine still gets its traffic");
    assert!(intruder.recv(&mut buf).is_err(), "the intruder gets nothing");
    stop.store(true, Ordering::Relaxed);
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
    a_lobby.send_to(&Packet::Bcast { vip: [0; 4], src_port: 8086, dst_port: 8086, payload: game_packet(b"hi") }.encode(), a.local).unwrap();
    let n = b_lobby.recv(&mut buf).unwrap();
    assert_eq!(Packet::decode(&buf[..n]), Some(Packet::Bcast { vip: va, src_port: 8086, dst_port: 8086, payload: game_packet(b"hi") }));

    // B's game packet addressed to A's vip reaches A's game socket, not its lobby socket.
    b_game.send_to(&Packet::Data { vip: va, src_port: 8088, dst_port: 8088, payload: game_packet(b"cmd") }.encode(), b.local).unwrap();
    let n = a_game.recv(&mut buf).unwrap();
    assert!(matches!(Packet::decode(&buf[..n]), Some(Packet::Data { payload, dst_port: 8088, .. }) if payload == game_packet(b"cmd")));
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
    // The same serial again (a restarted launcher, or a new network address) takes over its old session with the same virtual address.
    let first = start(&server, "AAA");
    let vip = wait(&first, |s| matches!(s, Status::Connected(_)));
    let second = start(&server, "AAA");
    assert_eq!(wait(&second, |s| matches!(s, Status::Connected(_))), vip);
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

    // The stored serial is what the next launch presents; it takes over the old session instead of being locked out.
    let before = a.status();
    let again = start(&server, &serial);
    assert_eq!(wait(&again, |s| matches!(s, Status::Connected(_))), before);
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
    let a = Agent::start(&server, "AAA", "alpha", "secret", None).unwrap();
    wait(&a, |s| matches!(s, Status::Connected(_)));
    // wrong password is refused, right password joins
    let bad = Agent::start(&server, "BBB", "alpha", "wrong", None).unwrap();
    assert_eq!(wait(&bad, |s| matches!(s, Status::Rejected(_))), Status::Rejected(Reason::WrongPassword as u8));
    let b = Agent::start(&server, "DDD", "alpha", "secret", None).unwrap();
    assert!(matches!(wait(&b, |s| matches!(s, Status::Connected(_))), Status::Connected(_)));
    // a client in another room never sees alpha's broadcast
    let other = Agent::start(&server, "EEE", "beta", "", None).unwrap();
    wait(&other, |s| matches!(s, Status::Connected(_)));
    let (a_lobby, b_lobby, o_lobby) = (engine_socket(&a, 8086), engine_socket(&b, 8086), engine_socket(&other, 8086));
    std::thread::sleep(Duration::from_millis(200));
    a_lobby.send_to(&Packet::Bcast { vip: [0; 4], src_port: 8086, dst_port: 8086, payload: game_packet(b"x") }.encode(), a.local).unwrap();
    let mut buf = [0u8; 2048];
    assert!(b_lobby.recv(&mut buf).is_ok());
    o_lobby.set_read_timeout(Some(Duration::from_millis(400))).unwrap();
    assert!(o_lobby.recv(&mut buf).is_err());

    // everyone leaves: the room is gone, so its name can be reused with a different password
    drop((a, b));
    std::thread::sleep(Duration::from_millis(300));
    let reuse = Agent::start(&server, "AAA", "alpha", "brand new", None).unwrap();
    assert!(matches!(wait(&reuse, |s| matches!(s, Status::Connected(_))), Status::Connected(_)));
    stop.store(true, Ordering::Relaxed);
}

#[test]
fn the_tunnel_carries_game_packets_and_nothing_else() {
    let (server, stop) = spawn_relay("AAA\nBBB\n");
    let a = start(&server, "AAA");
    let b = start(&server, "BBB");
    let vb = match wait(&b, |s| matches!(s, Status::Connected(_))) {
        Status::Connected(v) => v,
        s => panic!("{s:?}"),
    };
    wait(&a, |s| matches!(s, Status::Connected(_)));
    let (a_game, b_game) = (engine_socket(&a, 8088), engine_socket(&b, 8088));
    std::thread::sleep(Duration::from_millis(200));
    let send = |payload: Vec<u8>, src: u16, dst: u16| a_game.send_to(&Packet::Data { vip: vb, src_port: src, dst_port: dst, payload }.encode(), a.local).unwrap();
    let mut buf = [0u8; 2048];
    b_game.set_read_timeout(Some(Duration::from_millis(400))).unwrap();
    // web traffic, a foreign port, an oversized datagram: none of it comes out the other end
    send(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n".to_vec(), 8088, 8088);
    send(game_packet(b"x"), 8088, 53);
    send(game_packet(b"x"), 4444, 8088);
    send(game_packet(&[1; 1500]), 8088, 8088);
    assert!(b_game.recv(&mut buf).is_err(), "nothing but game packets may pass");
    // a Bind for a port that is not the game's is ignored: that socket receives nothing
    let stranger = engine_socket(&b, 9999);
    stranger.set_read_timeout(Some(Duration::from_millis(400))).unwrap();
    send(game_packet(b"real"), 8088, 8088);
    assert!(b_game.recv(&mut buf).is_ok(), "a genuine game packet still passes");
    assert!(stranger.recv(&mut buf).is_err());
    stop.store(true, Ordering::Relaxed);
}

#[test]
fn diagnostics_show_latency_loss_and_what_the_relay_reports() {
    let (server, stop) = spawn_relay("AAA\nBBB\n");
    let a = start(&server, "AAA");
    let b = start(&server, "BBB");
    wait(&a, |s| matches!(s, Status::Connected(_)));
    wait(&b, |s| matches!(s, Status::Connected(_)));
    assert!(Agent::start(&server, "nope", "room", "pw", None).unwrap().diagnostics().is_none(), "nothing to show before connecting");
    let t = Instant::now();
    let d = loop {
        let d = a.diagnostics().unwrap();
        if d.rtt_ms.is_some() && d.server.is_some() {
            break d;
        }
        assert!(t.elapsed() < Duration::from_secs(6), "no diagnostics: {d:?}");
        std::thread::sleep(Duration::from_millis(100));
    };
    assert!(d.rtt_ms.unwrap() < 500 && d.loss_pct == Some(0));
    let s = d.server.unwrap();
    assert_eq!((s.rooms, s.players, s.room_players), (1, 2, 2));
    stop.store(true, Ordering::Relaxed);
}

#[test]
fn an_eavesdropper_sees_nothing_and_the_relay_identity_is_pinned() {
    let (server, stop, key, tap) = spawn_relay_full("AAA\nBBB\n", true);
    // first visit: the key is learned and offered for saving
    let a = Agent::start(&server, "AAA", "secret-room", "pw", None).unwrap();
    assert!(matches!(wait(&a, |s| matches!(s, Status::Connected(_))), Status::Connected(_)));
    assert_eq!(a.take_learned_key(), Some(key));
    assert_eq!(a.take_learned_key(), None);
    // later visits present the saved key; nothing new is learned
    let b = Agent::start(&server, "BBB", "secret-room", "pw", Some(key)).unwrap();
    let vb = match wait(&b, |s| matches!(s, Status::Connected(_))) {
        Status::Connected(v) => v,
        s => panic!("{s:?}"),
    };
    assert_eq!(b.take_learned_key(), None);
    let d = b.diagnostics().unwrap();
    assert_eq!((d.server_identity, d.identity_saved), (Some(secure::fingerprint(&key)), true));
    // game traffic both ways, then look at everything that crossed the wire
    let (ea, eb) = (engine_socket(&a, 8088), engine_socket(&b, 8088));
    std::thread::sleep(Duration::from_millis(200));
    let secret = game_packet(b"ATTACK-AT-DAWN-ATTACK-AT-DAWN");
    ea.send_to(&Packet::Data { vip: vb, src_port: 8088, dst_port: 8088, payload: secret.clone() }.encode(), a.local).unwrap();
    let mut buf = [0u8; 2048];
    assert!(eb.recv(&mut buf).is_ok());
    std::thread::sleep(Duration::from_millis(300));
    let seen = tap.lock().unwrap().clone();
    assert!(seen.len() > 10);
    let hay = |needle: &[u8]| seen.iter().any(|d| d.windows(needle.len()).any(|w| w == needle));
    for needle in [&b"AAA"[..], b"BBB", b"secret-room", b"ATTACK", &secret[..8], &[10, 77, 0][..]] {
        assert!(!hay(needle), "the wire shows {:?}", String::from_utf8_lossy(needle));
    }
    stop.store(true, Ordering::Relaxed);
}

#[test]
fn a_different_server_than_the_saved_one_is_refused() {
    let (real, stop1, real_key, _) = spawn_relay_full("AAA\n", true);
    let (impostor, stop2, _, _) = spawn_relay_full("AAA\n", true);
    // the saved identity belongs to `real`; the address now answers as someone else
    let a = Agent::start(&impostor, "AAA", "room", "pw", Some(real_key)).unwrap();
    assert_eq!(wait(&a, |s| matches!(s, Status::Rejected(_))), Status::Rejected(Reason::ServerKeyChanged as u8));
    // and a client that did not pin anything is not fooled by a second, different answer after the first
    let ok = Agent::start(&real, "AAA", "room", "pw", Some(real_key)).unwrap();
    assert!(matches!(wait(&ok, |s| matches!(s, Status::Connected(_))), Status::Connected(_)));
    stop1.store(true, Ordering::Relaxed);
    stop2.store(true, Ordering::Relaxed);
}

#[test]
fn checksums_go_to_the_relay_and_a_disagreement_comes_back_to_both_engines() {
    let (server, stop) = spawn_relay("AAA\nBBB\n");
    let a = start(&server, "AAA");
    let b = start(&server, "BBB");
    wait(&a, |s| matches!(s, Status::Connected(_)));
    wait(&b, |s| matches!(s, Status::Connected(_)));
    let (ea, eb) = (engine_socket(&a, 8088), engine_socket(&b, 8088));
    std::thread::sleep(Duration::from_millis(200));
    // each engine reports its own checksum for frame 100 to its local agent; they differ
    ea.send_to(&Packet::Crc { frame: 100, crc: 0x12C26F6A, host: true }.encode(), a.local).unwrap();
    eb.send_to(&Packet::Crc { frame: 100, crc: 0xE67396AE, host: false }.encode(), b.local).unwrap();
    let mut buf = [0u8; 256];
    for (name, sock) in [("a", &ea), ("b", &eb)] {
        let n = sock.recv(&mut buf).unwrap_or_else(|e| panic!("{name} got no verdict: {e}"));
        let Some(Packet::Desync(v)) = Packet::decode(&buf[..n]) else { panic!("{name}: {:?}", &buf[..n]) };
        assert_eq!((v.frame, v.certain, v.players.len()), (100, true, 2));
        assert!(v.host.is_some());
    }
    // and the launcher can show it
    let t = Instant::now();
    while a.diagnostics().and_then(|d| d.desync).is_none() && t.elapsed() < Duration::from_secs(3) {
        std::thread::sleep(Duration::from_millis(50));
    }
    let d = a.diagnostics().unwrap().desync.expect("shown to the player");
    assert_eq!((d.frame, d.players.len()), (100, 2));
    stop.store(true, Ordering::Relaxed);
}

fn vip_of(a: &Agent) -> [u8; 4] {
    match wait(a, |s| matches!(s, Status::Connected(_))) {
        Status::Connected(v) => v,
        s => panic!("{s:?}"),
    }
}

/// A fake Red Alert 2 game in tunnel mode (yrpp-spawner): `u16 sender, u16 receiver` big-endian, then the payload.
fn spawner(agent: &Agent) -> UdpSocket {
    let s = UdpSocket::bind("127.0.0.1:0").unwrap();
    s.set_read_timeout(Some(Duration::from_secs(3))).unwrap();
    s.connect(("127.0.0.1", agent.tunnel_port())).unwrap();
    s
}

#[test]
fn red_alert_2_tunnel_and_lobby_between_two_players() {
    let (server, stop) = spawn_relay("AAA\nBBB\n");
    let a = Agent::start(&server, "AAA", "ra2:room", "pw", None).unwrap();
    let b = Agent::start(&server, "BBB", "ra2:room", "pw", None).unwrap();
    let (ia, ib) = (vip_of(&a)[3] as u16, vip_of(&b)[3] as u16);
    let (ga, gb, spoof) = (spawner(&a), spawner(&b), spawner(&a));
    let recv = |s: &UdpSocket| { let mut buf = [0u8; 2048]; let n = s.recv(&mut buf).unwrap(); buf[..n].to_vec() };
    let quiet = |s: &UdpSocket| { let mut buf = [0u8; 2048]; s.set_read_timeout(Some(Duration::from_millis(300))).unwrap(); s.recv(&mut buf).is_err() };
    // each game presents its launch's token (ra2 patch 0006) and gets the 8-byte answer
    let prefix = [0xFF, 0xFF, 0xFF, 0xFF, b'R', b'P', b'A', b'U'];
    let (ta, tb) = (a.arm_tunnel(), b.arm_tunnel());
    let mut ping = vec![0u8; 50];
    ping[4..12].copy_from_slice(b"pingpong");
    spoof.send(&ping).unwrap();
    assert!(quiet(&spoof), "nothing answers before the token");
    ga.send(&[&prefix[..], &ta].concat()).unwrap();
    assert_eq!(recv(&ga), prefix);
    gb.send(&[&prefix[..], &tb].concat()).unwrap();
    assert_eq!(recv(&gb), prefix);
    // a ping is answered locally with its first 12 bytes
    ga.send(&ping).unwrap();
    assert_eq!(recv(&ga), &ping[..12]);
    let hdr = |from: u16, to: u16, p: &[u8]| [&from.to_be_bytes()[..], &to.to_be_bytes(), p].concat();
    let (orders, hello) = (generals_proto::ra2_game_packet(b"orders"), generals_proto::ra2_game_packet(b"hello"));
    gb.send(&hdr(ib, ia, &hello)).unwrap();
    assert_eq!(recv(&ga), &hdr(ib, ia, &hello)[..]);
    ga.send(&hdr(ia, ib, &orders)).unwrap();
    assert_eq!(recv(&gb), &hdr(ia, ib, &orders)[..]);
    // another local endpoint speaking as A, with A's token too, and a datagram the game did not make: dropped
    spoof.send(&[&prefix[..], &ta].concat()).unwrap();
    spoof.send(&hdr(ia, ib, &orders)).unwrap();
    ga.send(&hdr(ia, ib, b"not the game")).unwrap();
    assert!(quiet(&spoof) && quiet(&gb));
    // the next launch's token: the old game is no longer heard
    let _ = a.arm_tunnel();
    ga.send(&[&prefix[..], &ta].concat()).unwrap();
    ga.send(&hdr(ia, ib, &orders)).unwrap();
    assert!(quiet(&ga) && quiet(&gb), "a replayed old token");
    // lobby blobs reach the other player only
    assert!(a.send_lobby(b"ready"));
    assert!(!a.send_lobby(&[0; 1025]));
    let t = Instant::now();
    let got = loop {
        let got = b.take_lobby();
        if !got.is_empty() || t.elapsed() > Duration::from_secs(3) {
            break got;
        }
        std::thread::sleep(Duration::from_millis(20));
    };
    assert_eq!(got, vec![(ia as u8, b"ready".to_vec())]);
    assert!(a.take_lobby().is_empty());
    stop.store(true, Ordering::Relaxed);
}

fn room_master(a: &Agent) -> Option<bool> {
    let t = Instant::now();
    while t.elapsed() < Duration::from_secs(3) {
        if let Some(m) = a.diagnostics().and_then(|d| d.room_master) {
            return Some(m);
        }
        std::thread::sleep(Duration::from_millis(20));
    }
    None
}

/// One lobby poll like the launcher's `ra2_lobby_poll`: ingest, prune, emit.
fn pump(lobby: &mut Lobby, agent: &Agent, vip: u8, name: &str, master: Option<bool>, now_ms: u64) {
    lobby.set_self(vip, name, master);
    for (from, blob) in agent.take_lobby() {
        lobby.receive(now_ms, from, &blob);
    }
    lobby.prune(now_ms);
    for blob in lobby.outgoing() {
        assert!(agent.send_lobby(&blob), "lobby blob");
    }
}

/// Full RA2 path used by CI: `ra2:` room → lobby (map + ready + start) → `arm_tunnel` → token auth →
/// filtered game packets; a spoofed local peer is ignored.
#[test]
fn red_alert_2_lobby_ready_start_then_tunnel_auth() {
    let (server, stop) = spawn_relay("AAA\nBBB\n");
    let a = Agent::start(&server, "AAA", "ra2:harness", "pw", None).unwrap();
    let b = Agent::start(&server, "BBB", "ra2:harness", "pw", None).unwrap();
    let (va, vb) = (vip_of(&a), vip_of(&b));
    let (ia, ib) = (va[3], vb[3]);
    let (ma, mb) = (room_master(&a).expect("a master"), room_master(&b).expect("b master"));
    assert_ne!(ma, mb, "exactly one room master");
    // host_agent is always the room master; pass Some(true)/Some(false) into the lobby
    let (host, guest, host_agent, guest_agent, host_vip, guest_vip) = if ma {
        ("Ann", "Bob", &a, &b, ia, ib)
    } else {
        ("Ann", "Bob", &b, &a, ib, ia)
    };

    let (mut host_lobby, mut guest_lobby) = (Lobby::default(), Lobby::default());
    let t0 = Instant::now();
    let now = || t0.elapsed().as_millis() as u64;
    // hear each other
    for _ in 0..8 {
        pump(&mut host_lobby, host_agent, host_vip, host, Some(true), now());
        pump(&mut guest_lobby, guest_agent, guest_vip, guest, Some(false), now());
        std::thread::sleep(Duration::from_millis(40));
    }
    assert!(host_lobby.is_host());
    assert!(!guest_lobby.is_host());
    // seat first, then settings (host seat changes bump rev; ready after the final rev)
    host_lobby.set_seat(Seat { side: 0, color: 0, start: 1, team: -1, ready: false, rev: 0 }).unwrap();
    host_lobby
        .set_settings(RoomSettings {
            map: "xmp01t4.map".into(),
            map_name: "Coastal Interference".into(),
            starts: 4,
            credits: 10000,
            speed: 1,
            crates: true,
            superweapons: true,
            allies: true,
            auto_repair: true,
            crate_minimum: None,
            ai: vec![],
            rev: 0,
        })
        .unwrap();
    let t = Instant::now();
    let mut guest_rev = None;
    while t.elapsed() < Duration::from_secs(3) {
        pump(&mut host_lobby, host_agent, host_vip, host, Some(true), now());
        pump(&mut guest_lobby, guest_agent, guest_vip, guest, Some(false), now());
        if let Some(s) = guest_lobby.view().settings {
            guest_rev = Some(s.rev);
            break;
        }
        std::thread::sleep(Duration::from_millis(30));
    }
    let rev = guest_rev.expect("guest has map");
    guest_lobby.set_seat(Seat { side: 8, color: 1, start: 2, team: -1, ready: true, rev }).unwrap();
    let t = Instant::now();
    while t.elapsed() < Duration::from_secs(3) {
        // guest emits Ready first so the host can ingest it this tick
        pump(&mut guest_lobby, guest_agent, guest_vip, guest, Some(false), now());
        std::thread::sleep(Duration::from_millis(30));
        pump(&mut host_lobby, host_agent, host_vip, host, Some(true), now());
        if host_lobby.view().blocked.is_empty() {
            break;
        }
        std::thread::sleep(Duration::from_millis(30));
    }
    assert_eq!(host_lobby.view().blocked, "", "{:?}", host_lobby.view());
    let setup = host_lobby.start(0xC0FFEE).unwrap();
    assert_eq!(setup.map, "xmp01t4.map");
    assert_eq!(setup.players.len(), 2);
    assert!(setup.players.iter().all(|p| p.tunnel_id.is_some()));
    let mut guest_start = None;
    let t = Instant::now();
    while t.elapsed() < Duration::from_secs(3) {
        pump(&mut host_lobby, host_agent, host_vip, host, Some(true), now());
        std::thread::sleep(Duration::from_millis(30));
        pump(&mut guest_lobby, guest_agent, guest_vip, guest, Some(false), now());
        if let Some(s) = guest_lobby.view().start {
            guest_start = Some(s);
            break;
        }
        std::thread::sleep(Duration::from_millis(30));
    }
    let guest_start = guest_start.expect("guest received Start");
    assert_eq!(guest_start.seed, 0xC0FFEE);
    assert_eq!(guest_start.players[0].tunnel_id, Some(host_vip));
    assert_eq!(guest_start.players[1].tunnel_id, Some(guest_vip));

    // launch tokens (post-0006): only the authed endpoint may speak; spoof ignored; non-game payload dropped
    let prefix = [0xFF, 0xFF, 0xFF, 0xFF, b'R', b'P', b'A', b'U'];
    let (ta, tb) = (host_agent.arm_tunnel(), guest_agent.arm_tunnel());
    let (gh, gg, spoof) = (spawner(host_agent), spawner(guest_agent), spawner(host_agent));
    let recv = |s: &UdpSocket| {
        let mut buf = [0u8; 2048];
        let n = s.recv(&mut buf).unwrap();
        buf[..n].to_vec()
    };
    let quiet = |s: &UdpSocket| {
        let mut buf = [0u8; 2048];
        s.set_read_timeout(Some(Duration::from_millis(300))).unwrap();
        s.recv(&mut buf).is_err()
    };
    spoof.send(&generals_proto::ra2_game_packet(b"early")).unwrap();
    assert!(quiet(&spoof), "no traffic before token");
    gh.send(&[&prefix[..], &ta].concat()).unwrap();
    assert_eq!(recv(&gh), prefix);
    gg.send(&[&prefix[..], &tb].concat()).unwrap();
    assert_eq!(recv(&gg), prefix);
    let hdr = |from: u16, to: u16, p: &[u8]| [&from.to_be_bytes()[..], &to.to_be_bytes(), p].concat();
    let frame = generals_proto::ra2_game_packet(b"frame1");
    gg.send(&hdr(guest_vip as u16, host_vip as u16, &frame)).unwrap();
    assert_eq!(recv(&gh), hdr(guest_vip as u16, host_vip as u16, &frame));
    gh.send(&hdr(host_vip as u16, guest_vip as u16, &frame)).unwrap();
    assert_eq!(recv(&gg), hdr(host_vip as u16, guest_vip as u16, &frame));
    spoof.send(&[&prefix[..], &ta].concat()).unwrap();
    spoof.send(&hdr(host_vip as u16, guest_vip as u16, &frame)).unwrap();
    gh.send(&hdr(host_vip as u16, guest_vip as u16, b"not-a-game-packet")).unwrap();
    assert!(quiet(&spoof) && quiet(&gg), "spoof and non-game dropped");
    // launch_ini shape the engines would get
    let mut for_host = setup.clone();
    for_host.tunnel = Some(generals_core::ra2launch::Tunnel { port: host_agent.tunnel_port(), id: host_vip, token: ta });
    let ini = generals_core::ra2launch::launch_ini(&for_host, "xmp01t4.map", 0).unwrap();
    assert!(ini.contains("Mode=network") && ini.contains("Token=") && ini.contains(&format!("TunnelId={host_vip}")));
    stop.store(true, Ordering::Relaxed);
}
