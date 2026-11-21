//! Two-client RA2 multiplayer path: in-process relay → agents in an `ra2:` room → lobby (map + ready +
//! start) → `arm_tunnel` → token auth and filtered tunnel traffic.
//!
//! Default (CI-safe): fake game sockets prove auth, spoof drop, and packet filter.
//! `--real`: also write launch inis and start two `ra2engine` processes (Xvfb when available). Needs
//! game files and an engine built with patch 0006 (`[Tunnel] Token=`).
//!
//! ```text
//! cargo run -p generals-core --example ra2_mp_harness
//! cargo run -p generals-core --example ra2_mp_harness -- --real --secs 25
//! ```
//! Env: `RA2_DATA` (default `~/rp-steam/game/RA2`), `RA2ENGINE` (default `~/rp-build/ra2engine`),
//! `RA2_MP_LOG` (default `/tmp/ra2-mp-harness`).
use generals_core::agent::{Agent, Status};
use generals_core::ra2launch::{self, Ra2Setup, Tunnel};
use generals_core::ra2lobby::{Lobby, RoomSettings, Seat};
use generals_server::{Serials, Server};
use std::net::UdpSocket;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

fn spawn_relay(serials: &str) -> (String, Arc<AtomicBool>) {
    let sock = UdpSocket::bind("127.0.0.1:0").unwrap();
    sock.set_read_timeout(Some(Duration::from_millis(100))).unwrap();
    let addr = sock.local_addr().unwrap().to_string();
    let stop = Arc::new(AtomicBool::new(false));
    let mut server = Server::new(Serials::parse(serials));
    server.set_open(true);
    let mut n = 0u64;
    server.set_rng(Box::new(move || {
        n += 1;
        Some(n.to_be_bytes())
    }));
    let s2 = stop.clone();
    std::thread::spawn(move || {
        let start = Instant::now();
        let mut buf = [0u8; 2048];
        while !s2.load(Ordering::Relaxed) {
            if let Ok((n, from)) = sock.recv_from(&mut buf) {
                for (to, out) in server.handle(from, &buf[..n], start.elapsed().as_secs()) {
                    let _ = sock.send_to(&out, to);
                }
            }
        }
    });
    (addr, stop)
}

fn wait_connected(a: &Agent) -> [u8; 4] {
    let t = Instant::now();
    loop {
        match a.status() {
            Status::Connected(v) => return v,
            Status::Rejected(r) => panic!("rejected {r}"),
            Status::Connecting if t.elapsed() > Duration::from_secs(10) => panic!("timed out"),
            Status::Connecting => std::thread::sleep(Duration::from_millis(40)),
        }
    }
}

fn room_master(a: &Agent) -> bool {
    let t = Instant::now();
    while t.elapsed() < Duration::from_secs(5) {
        if let Some(m) = a.diagnostics().and_then(|d| d.room_master) {
            return m;
        }
        std::thread::sleep(Duration::from_millis(20));
    }
    panic!("no room_master");
}

fn pump(lobby: &mut Lobby, agent: &Agent, vip: u8, name: &str, master: bool, now_ms: u64) {
    lobby.set_self(vip, name, Some(master));
    for (from, blob) in agent.take_lobby() {
        lobby.receive(now_ms, from, &blob);
    }
    lobby.prune(now_ms);
    for blob in lobby.outgoing() {
        assert!(agent.send_lobby(&blob));
    }
}

fn spawner(agent: &Agent) -> UdpSocket {
    let s = UdpSocket::bind("127.0.0.1:0").unwrap();
    s.set_read_timeout(Some(Duration::from_secs(3))).unwrap();
    s.connect(("127.0.0.1", agent.tunnel_port())).unwrap();
    s
}

fn engine_has_token_auth(exe: &Path) -> bool {
    let Ok(bytes) = std::fs::read(exe) else {
        return false;
    };
    let pat = [0xFFu8, 0xFF, 0xFF, 0xFF, b'R', b'P', b'A', b'U'];
    bytes.windows(pat.len()).any(|w| w == pat)
}

fn home_join(rel: &str) -> PathBuf {
    PathBuf::from(std::env::var_os("HOME").unwrap_or_else(|| "/tmp".into())).join(rel)
}

struct Args {
    real: bool,
    secs: u64,
    map: String,
}

fn parse_args() -> Args {
    let mut real = false;
    let mut secs = 12u64;
    let mut map = "xmp01t4.map".into();
    let a: Vec<String> = std::env::args().skip(1).collect();
    let mut i = 0;
    while i < a.len() {
        match a[i].as_str() {
            "--real" => real = true,
            "--secs" => {
                i += 1;
                secs = a.get(i).and_then(|s| s.parse().ok()).unwrap_or(secs);
            }
            "--map" => {
                i += 1;
                if let Some(m) = a.get(i) {
                    map = m.clone();
                }
            }
            "-h" | "--help" => {
                eprintln!("ra2_mp_harness [--real] [--secs N] [--map name.map]");
                std::process::exit(0);
            }
            other => panic!("unknown arg {other}"),
        }
        i += 1;
    }
    Args { real, secs, map }
}

fn has_xvfb() -> bool {
    Command::new("xvfb-run").arg("-a").arg("true").stdout(Stdio::null()).stderr(Stdio::null()).status().map(|s| s.success()).unwrap_or(false)
}

fn start_engine(engine: &Path, data: &Path, ini: &Path, log: &Path, title: &str) -> Child {
    let use_xvfb = has_xvfb();
    let log_err = log.with_extension("err");
    if use_xvfb {
        Command::new("xvfb-run")
            .arg("-a")
            .arg(engine)
            .arg("--data")
            .arg(data)
            .arg("--launch")
            .arg(ini)
            .arg("--windowed")
            .arg("--width")
            .arg("1024")
            .arg("--height")
            .arg("768")
            .arg("--no-audio")
            .arg("--title")
            .arg(title)
            .stdout(Stdio::from(std::fs::File::create(log).unwrap()))
            .stderr(Stdio::from(std::fs::File::create(log_err).unwrap()))
            .spawn()
            .expect("xvfb engine")
    } else {
        Command::new(engine)
            .arg("--data")
            .arg(data)
            .arg("--launch")
            .arg(ini)
            .arg("--windowed")
            .arg("--width")
            .arg("1024")
            .arg("--height")
            .arg("768")
            .arg("--no-audio")
            .arg("--title")
            .arg(title)
            .stdout(Stdio::from(std::fs::File::create(log).unwrap()))
            .stderr(Stdio::from(std::fs::File::create(log_err).unwrap()))
            .spawn()
            .expect("engine")
    }
}

fn main() {
    let args = parse_args();
    let log_dir = PathBuf::from(std::env::var_os("RA2_MP_LOG").unwrap_or_else(|| "/tmp/ra2-mp-harness".into()));
    let _ = std::fs::create_dir_all(&log_dir);
    eprintln!("ra2_mp_harness: log {}", log_dir.display());

    let (server, stop) = spawn_relay("AAA\nBBB\n");
    let a = Agent::start(&server, "AAA", "ra2:harness", "pw", None).expect("agent a");
    let b = Agent::start(&server, "BBB", "ra2:harness", "pw", None).expect("agent b");
    eprintln!("relay {server}; agents connecting…");

    let (va, vb) = (wait_connected(&a), wait_connected(&b));
    let (ia, ib) = (va[3], vb[3]);
    let (ma, mb) = (room_master(&a), room_master(&b));
    assert_ne!(ma, mb, "one master");
    let (host, guest, host_vip, guest_vip) = if ma { (&a, &b, ia, ib) } else { (&b, &a, ib, ia) };
    eprintln!("connected host=10.77.0.{host_vip} guest=10.77.0.{guest_vip}");

    let (mut hl, mut gl) = (Lobby::default(), Lobby::default());
    let t0 = Instant::now();
    let now = || t0.elapsed().as_millis() as u64;
    for _ in 0..10 {
        pump(&mut hl, host, host_vip, "Host", true, now());
        pump(&mut gl, guest, guest_vip, "Guest", false, now());
        std::thread::sleep(Duration::from_millis(40));
    }
    hl.set_seat(Seat { side: 0, color: 0, start: 1, team: -1, ready: false, rev: 0 }).unwrap();
    hl.set_settings(RoomSettings {
        map: args.map.clone(),
        map_name: args.map.trim_end_matches(".map").into(),
        starts: 4,
        credits: 10000,
        speed: 1,
        ai: vec![],
        rev: 0,
    })
    .expect("map");
    let mut rev = None;
    let t = Instant::now();
    while t.elapsed() < Duration::from_secs(3) {
        pump(&mut hl, host, host_vip, "Host", true, now());
        pump(&mut gl, guest, guest_vip, "Guest", false, now());
        if let Some(s) = gl.view().settings {
            rev = Some(s.rev);
            break;
        }
        std::thread::sleep(Duration::from_millis(30));
    }
    let rev = rev.expect("settings reached guest");
    gl.set_seat(Seat { side: 8, color: 1, start: 2, team: -1, ready: true, rev }).unwrap();
    let t = Instant::now();
    while t.elapsed() < Duration::from_secs(3) {
        pump(&mut gl, guest, guest_vip, "Guest", false, now());
        std::thread::sleep(Duration::from_millis(30));
        pump(&mut hl, host, host_vip, "Host", true, now());
        if hl.view().blocked.is_empty() {
            break;
        }
        std::thread::sleep(Duration::from_millis(30));
    }
    assert_eq!(hl.view().blocked, "", "{:?}", hl.view());
    let setup = hl.start(0x00C0_FFEE).expect("start");
    let mut guest_got = None;
    let t = Instant::now();
    while t.elapsed() < Duration::from_secs(3) {
        pump(&mut hl, host, host_vip, "Host", true, now());
        std::thread::sleep(Duration::from_millis(30));
        pump(&mut gl, guest, guest_vip, "Guest", false, now());
        if let Some(s) = gl.view().start {
            guest_got = Some(s);
            break;
        }
        std::thread::sleep(Duration::from_millis(30));
    }
    let guest_setup = guest_got.expect("guest Start");
    assert_eq!(guest_setup.seed, setup.seed);
    eprintln!("lobby ok: map={} seed={:#x} players={}", setup.map, setup.seed, setup.players.len());
    let _ = std::fs::write(log_dir.join("setup.json"), serde_json::to_string_pretty(&setup).unwrap());

    let (ta, tb) = (host.arm_tunnel(), guest.arm_tunnel());
    eprintln!("tokens armed; host tunnel :{} guest :{}", host.tunnel_port(), guest.tunnel_port());

    // (A) fake engines — always
    let prefix = [0xFFu8, 0xFF, 0xFF, 0xFF, b'R', b'P', b'A', b'U'];
    let (gh, gg, spoof) = (spawner(host), spawner(guest), spawner(host));
    let recv = |s: &UdpSocket| {
        let mut buf = [0u8; 2048];
        let n = s.recv(&mut buf).expect("recv");
        buf[..n].to_vec()
    };
    let quiet = |s: &UdpSocket| {
        let mut buf = [0u8; 2048];
        s.set_read_timeout(Some(Duration::from_millis(250))).unwrap();
        s.recv(&mut buf).is_err()
    };
    spoof.send(&generals_proto::ra2_game_packet(b"pre")).unwrap();
    assert!(quiet(&spoof), "spoof before token must be ignored");
    gh.send(&[&prefix[..], &ta[..]].concat()).unwrap();
    assert_eq!(recv(&gh), prefix, "host token ack");
    gg.send(&[&prefix[..], &tb[..]].concat()).unwrap();
    assert_eq!(recv(&gg), prefix, "guest token ack");
    let hdr = |from: u16, to: u16, p: &[u8]| [&from.to_be_bytes()[..], &to.to_be_bytes(), p].concat();
    let pkt = generals_proto::ra2_game_packet(b"harness");
    let until = Instant::now() + Duration::from_secs(args.secs.min(5).max(1));
    let mut exchanged = 0u32;
    while Instant::now() < until {
        gg.send(&hdr(guest_vip as u16, host_vip as u16, &pkt)).unwrap();
        assert_eq!(recv(&gh)[..4], hdr(guest_vip as u16, host_vip as u16, &pkt)[..4]);
        gh.send(&hdr(host_vip as u16, guest_vip as u16, &pkt)).unwrap();
        assert_eq!(recv(&gg)[..4], hdr(host_vip as u16, guest_vip as u16, &pkt)[..4]);
        exchanged += 1;
        std::thread::sleep(Duration::from_millis(50));
    }
    spoof.send(&[&prefix[..], &ta[..]].concat()).unwrap();
    spoof.send(&hdr(host_vip as u16, guest_vip as u16, &pkt)).unwrap();
    gh.send(&hdr(host_vip as u16, guest_vip as u16, b"garbage")).unwrap();
    assert!(quiet(&spoof) && quiet(&gg), "spoof + non-game filter");
    eprintln!("(A) fake tunnel ok: {exchanged} round-trips, spoof ignored");

    if args.real {
        let data = PathBuf::from(std::env::var_os("RA2_DATA").unwrap_or_else(|| home_join("rp-steam/game/RA2").into()));
        let engine = PathBuf::from(std::env::var_os("RA2ENGINE").unwrap_or_else(|| home_join("rp-build/ra2engine").into()));
        if !data.join("ra2md.mix").is_file() {
            eprintln!("(B) skip real: no RA2 data at {}", data.display());
        } else if !engine.is_file() {
            eprintln!("(B) skip real: no engine at {}", engine.display());
        } else if !engine_has_token_auth(&engine) {
            eprintln!(
                "(B) skip real: {} lacks patch 0006 token auth. Rebuild:\n  sh ra2/patches/apply.sh && cmake -S ra2/engine -B ra2/build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build ra2/build && cp -f ra2/build/ra2engine \"$HOME/rp-build/ra2engine\"",
                engine.display()
            );
        } else {
            let map_path = data.join(&args.map);
            let map_for_ini = if map_path.is_file() { map_path.as_path() } else { Path::new(args.map.as_str()) };
            let mut children: Vec<(String, Child)> = Vec::new();
            let write_side = |label: &str, agent: &Agent, vip: u8, token: [u8; 16], local: usize, setup: &Ra2Setup| -> (String, Child) {
                let mut s = setup.clone();
                s.tunnel = Some(Tunnel { port: agent.tunnel_port(), id: vip, token });
                let ini_body = ra2launch::launch_ini(&s, map_for_ini, local).expect("ini");
                let ini_path = log_dir.join(format!("{label}.ini"));
                std::fs::write(&ini_path, &ini_body).unwrap();
                let log = log_dir.join(format!("{label}.log"));
                let child = start_engine(&engine, &data, &ini_path, &log, &format!("ra2-mp-{label}"));
                eprintln!("(B) started {label} pid={} ini={}", child.id(), ini_path.display());
                (label.to_string(), child)
            };
            children.push(write_side("host", host, host_vip, ta, 0, &setup));
            let guest_local = setup.players.iter().position(|p| p.tunnel_id == Some(guest_vip)).expect("guest seat");
            children.push(write_side("guest", guest, guest_vip, tb, guest_local, &setup));
            let deadline = Instant::now() + Duration::from_secs(args.secs);
            let mut saw_traffic = false;
            while Instant::now() < deadline {
                for (name, child) in &mut children {
                    if let Ok(Some(st)) = child.try_wait() {
                        eprintln!("(B) {name} exited early: {st:?}");
                    }
                }
                if let (Some(da), Some(db)) = (host.diagnostics(), guest.diagnostics()) {
                    if da.down_kbps > 0 || db.down_kbps > 0 || da.up_kbps > 0 || db.up_kbps > 0 {
                        saw_traffic = true;
                        eprintln!(
                            "(B) tunnel traffic host up/down={}/{} guest={}/{} kbps",
                            da.up_kbps, da.down_kbps, db.up_kbps, db.down_kbps
                        );
                    }
                }
                std::thread::sleep(Duration::from_millis(500));
            }
            for (name, mut child) in children {
                let _ = child.kill();
                let _ = child.wait();
                eprintln!("(B) stopped {name}");
            }
            if saw_traffic {
                eprintln!("(B) real run saw tunnel kbps; logs in {}", log_dir.display());
            } else {
                eprintln!("(B) real run finished (no kbps sample — check engine logs in {})", log_dir.display());
            }
        }
    }

    stop.store(true, Ordering::Relaxed);
    eprintln!("ok");
}
