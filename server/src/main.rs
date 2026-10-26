//! rallypoint-server [--serials FILE] [--bind ADDR] [--key FILE] <run | issue [label] | revoke SERIAL | list | key>
//! Settings come from the environment (see .env.example): ALLOWED_COUNTRIES, GEO_DIR, ALLOW_PRIVATE,
//! GENERALS_SERIALS, GENERALS_BIND, GENERALS_REGISTRATION, GENERALS_KEY.
use generals_proto::secure::{self, Key};
use generals_proto::Wire;
use generals_server::{format_serial, geo, os_random, Serials, Server};
use std::io::Write;
use std::net::UdpSocket;
use std::time::{Duration, Instant, SystemTime};

/// The release this binary was built from (CI sets RALLYPOINT_VERSION), else the crate version.
fn version() -> &'static str {
    option_env!("RALLYPOINT_VERSION").unwrap_or(env!("CARGO_PKG_VERSION"))
}

fn read(path: &str) -> String {
    std::fs::read_to_string(path).unwrap_or_default()
}

fn env(k: &str, d: &str) -> String {
    std::env::var(k).ok().filter(|v| !v.trim().is_empty()).unwrap_or_else(|| d.to_string())
}

/// Serial-file line for a label: single line, no control characters.
fn clean(label: &str) -> String {
    label.chars().filter(|c| !c.is_control()).collect::<String>().trim().to_string()
}

/// The server's identity: a key pair made on first start and kept (0600) next to the serials. Players pin the
/// public half on their first connection, so keep this file; losing it makes every player see a changed identity.
fn identity(path: &str) -> std::io::Result<(Key, Key)> {
    if let Ok(text) = std::fs::read_to_string(path) {
        let mut parts = text.split_whitespace();
        if let (Some(private), Some(public)) = (parts.next().and_then(secure::from_hex), parts.next().and_then(secure::from_hex)) {
            return Ok((private, public));
        }
        return Err(std::io::Error::other(format!("{path} exists but is not a valid key file; fix or remove it")));
    }
    let (private, public) = secure::generate_keypair();
    #[cfg(unix)]
    let mut f = {
        use std::os::unix::fs::OpenOptionsExt;
        std::fs::OpenOptions::new().write(true).create_new(true).mode(0o600).open(path)?
    };
    #[cfg(not(unix))]
    let mut f = std::fs::OpenOptions::new().write(true).create_new(true).open(path)?;
    writeln!(f, "{} {}", secure::to_hex(&private), secure::to_hex(&public))?;
    Ok((private, public))
}

fn main() -> std::io::Result<()> {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    if args.iter().any(|a| a == "--version" || a == "-V") {
        println!("rallypoint-server {}", version());
        return Ok(());
    }
    let mut take = |flag: &str, default: &str| match args.iter().position(|a| a == flag) {
        Some(i) if i + 1 < args.len() => {
            args.remove(i);
            args.remove(i)
        }
        _ => default.to_string(),
    };
    let file = take("--serials", &env("GENERALS_SERIALS", "serials.txt"));
    let bind = take("--bind", &env("GENERALS_BIND", "0.0.0.0:7777"));
    let key_file = take("--key", &env("GENERALS_KEY", &format!("{}.key", file.trim_end_matches(".txt"))));
    let open = env("GENERALS_REGISTRATION", "open") != "closed";

    match args.first().map(String::as_str) {
        Some("issue") => {
            let serial = format_serial(os_random().ok_or_else(|| std::io::Error::other("no OS randomness"))?);
            let mut f = std::fs::OpenOptions::new().create(true).append(true).open(&file)?;
            writeln!(f, "{serial} {}", clean(&args[1..].join(" ")))?;
            println!("{serial}");
        }
        Some("revoke") if args.len() == 2 => {
            let s = &args[1];
            let text = read(&file);
            if !Serials::parse(&text).active.contains(s) {
                eprintln!("no active serial {s}");
                std::process::exit(1);
            }
            let out: Vec<String> = text
                .lines()
                .map(|l| if l.split_whitespace().next() == Some(s) { format!("!{l}") } else { l.to_string() })
                .collect();
            // write-then-rename so a crash can't leave a half-written serial file
            let tmp = format!("{file}.tmp");
            std::fs::write(&tmp, out.join("\n") + "\n")?;
            std::fs::rename(&tmp, &file)?;
        }
        Some("list") => print!("{}", read(&file)),
        Some("key") => {
            let (_, public) = identity(&key_file)?;
            println!("identity {}\npublic key {}", secure::fingerprint(&public), secure::to_hex(&public));
        }
        Some("run") => run(&file, &bind, open, &key_file)?,
        _ => eprintln!("usage: rallypoint-server [--serials FILE] [--bind ADDR] [--key FILE] <run | issue [label] | revoke SERIAL | list | key>"),
    }
    Ok(())
}

fn run(file: &str, bind: &str, open: bool, key_file: &str) -> std::io::Result<()> {
    let sock = UdpSocket::bind(bind)?;
    sock.set_read_timeout(Some(Duration::from_secs(1)))?;
    let mut server = Server::new(Serials::parse(&read(file)));
    server.set_open(open);
    server.set_version(version());
    let (private, public) = identity(key_file)?;
    server.set_identity(private, public);
    eprintln!("identity {} (players pin this the first time they connect; `rallypoint-server key` shows it)", secure::fingerprint(&public));

    // Optional country limit, e.g. ALLOWED_COUNTRIES=NZ,AU
    let countries: Vec<String> = env("ALLOWED_COUNTRIES", "").split(',').map(|c| c.trim().to_string()).filter(|c| !c.is_empty()).collect();
    if countries.is_empty() {
        eprintln!("country limit: none");
    } else {
        // Off unless asked for. Behind Docker's userland proxy (IPv6 clients, some setups) every client appears to come from
        // the bridge gateway, 172.x, a "private" address: letting those through would let everyone past the country limit.
        let allow_private = env("ALLOW_PRIVATE", "false") == "true";
        if allow_private && std::path::Path::new("/.dockerenv").exists() {
            eprintln!("country limit: ALLOW_PRIVATE=true inside Docker lets through anyone whose address Docker rewrites to its own (IPv6 clients, userland proxy)");
        }
        let (filter, warnings) = geo::load(std::path::Path::new(&env("GEO_DIR", "/data/geo")), &countries, allow_private);
        for w in warnings {
            eprintln!("country limit: {w}");
        }
        eprintln!("country limit: {}", countries.join(","));
        server.set_allow(Box::new(move |ip| filter.allows(ip)));
    }

    let mtime = || std::fs::metadata(file).and_then(|m| m.modified()).ok();
    let mut seen: Option<SystemTime> = mtime();
    let start = Instant::now();
    let mut buf = [0u8; 2048];
    eprintln!("rallypoint-server {} listening on {bind}, serials from {file}, sign-up {}", version(), if open { "open" } else { "closed" });
    // Reload when the serial file changed. Also done on every Hello, so a serial issued a moment ago works at once.
    let mut reload = |server: &mut Server| {
        let m = mtime();
        if m != seen {
            seen = m;
            server.set_serials(Serials::parse(&read(file)));
        }
    };
    // Housekeeping (expiry, reading the serial file) runs once a second, not once per datagram: a flood of
    // packets must not be able to make each one trigger a scan of every session or a stat() of the file.
    let (mut last_tick, mut last_hello_reload) = (u64::MAX, u64::MAX);
    loop {
        let now = start.elapsed().as_secs();
        if let Ok((n, from)) = sock.recv_from(&mut buf) {
            {
                // A handshake may be a join with a serial issued a moment ago: look at the file first (once a second at most).
                if matches!(Wire::decode(&buf[..n]), Some(Wire::Hs1 { .. })) && last_hello_reload != now {
                    last_hello_reload = now;
                    reload(&mut server);
                }
                for (to, out) in server.handle(from, &buf[..n], now) {
                    // A failed send (unreachable peer, full buffer) must never take the server down.
                    let _ = sock.send_to(&out, to);
                }
                for line in server.take_events() {
                    eprintln!("[{}] {line}", uptime(now));
                }
                // Persist auto-issued serials right away so a restart keeps them (and the reload below sees them).
                for s in server.take_new_serials() {
                    let saved = std::fs::OpenOptions::new().create(true).append(true).open(file).and_then(|mut f| writeln!(f, "{s} auto"));
                    if let Err(e) = saved {
                        eprintln!("could not save a new serial to {file}: {e}"); // still valid until restart
                    }
                }
            }
        }
        if now != last_tick {
            last_tick = now;
            server.expire(now);
            for line in server.take_events() {
                eprintln!("[{}] {line}", uptime(now));
            }
            reload(&mut server);
        }
    }
}

/// Seconds since start as `1h02m03s`, for log lines (the container log adds wall-clock time).
fn uptime(s: u64) -> String {
    format!("{}h{:02}m{:02}s", s / 3600, s / 60 % 60, s % 60)
}
