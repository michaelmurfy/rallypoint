//! generals-server [--serials FILE] [--bind ADDR] <run | issue [label] | revoke SERIAL | list>
use generals_proto::Packet;
use generals_server::{format_serial, Serials, Server};
use std::io::{Read, Write};
use std::net::UdpSocket;
use std::time::{Duration, Instant, SystemTime};

fn read(path: &str) -> String {
    std::fs::read_to_string(path).unwrap_or_default()
}

fn main() -> std::io::Result<()> {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    let mut take = |flag: &str, default: &str| match args.iter().position(|a| a == flag) {
        Some(i) if i + 1 < args.len() => {
            args.remove(i);
            args.remove(i)
        }
        _ => default.to_string(),
    };
    // Env defaults let `docker exec generals-server issue ...` find the same files as the entrypoint.
    let env = |k: &str, d: &str| std::env::var(k).unwrap_or_else(|_| d.to_string());
    let file = take("--serials", &env("GENERALS_SERIALS", "serials.txt"));
    let bind = take("--bind", &env("GENERALS_BIND", "0.0.0.0:7777"));

    match args.first().map(String::as_str) {
        Some("issue") => {
            let mut b = [0u8; 8];
            std::fs::File::open("/dev/urandom")?.read_exact(&mut b)?; // server is Linux-only
            let serial = format_serial(b);
            let mut f = std::fs::OpenOptions::new().create(true).append(true).open(&file)?;
            writeln!(f, "{serial} {}", args[1..].join(" "))?;
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
            std::fs::write(&file, out.join("\n") + "\n")?;
        }
        Some("list") => print!("{}", read(&file)),
        Some("run") => run(&file, &bind)?,
        _ => eprintln!("usage: generals-server [--serials FILE] [--bind ADDR] <run | issue [label] | revoke SERIAL | list>"),
    }
    Ok(())
}

fn run(file: &str, bind: &str) -> std::io::Result<()> {
    let sock = UdpSocket::bind(bind)?;
    sock.set_read_timeout(Some(Duration::from_secs(1)))?;
    let mut server = Server::new(Serials::parse(&read(file)));
    let mtime = || std::fs::metadata(file).and_then(|m| m.modified()).ok();
    let mut seen: Option<SystemTime> = mtime();
    let start = Instant::now();
    let mut buf = [0u8; 2048];
    eprintln!("listening on {bind}, serials from {file}");
    loop {
        let now = start.elapsed().as_secs();
        if let Ok((n, from)) = sock.recv_from(&mut buf) {
            if let Some(p) = Packet::decode(&buf[..n]) {
                for (to, out) in server.handle(from, p, now) {
                    sock.send_to(&out.encode(), to)?;
                }
            }
        }
        server.expire(now);
        if mtime() != seen {
            seen = mtime();
            server.set_serials(Serials::parse(&read(file)));
        }
    }
}
