//! Dev tool: join a relay room like the launcher does, print `ready <agent address> <virtual IP>` and stay
//! connected. Lets an engine (or a test harness) be pointed at a real agent without the GUI.
//! `cargo run -p generals-core --example agent_cli -- 127.0.0.1:7777 room password`
use generals_core::agent::{Agent, Status};
use std::time::{Duration, Instant};

fn main() {
    let a: Vec<String> = std::env::args().collect();
    let (server, room, pw) = (a.get(1).expect("server"), a.get(2).expect("room"), a.get(3).map(String::as_str).unwrap_or(""));
    let agent = Agent::start(server, "", room, pw).expect("start");
    let t = Instant::now();
    loop {
        match agent.status() {
            Status::Connected(v) => {
                println!("ready {} {}.{}.{}.{}", agent.local, v[0], v[1], v[2], v[3]);
                break;
            }
            Status::Rejected(r) => panic!("rejected {r}"),
            Status::Connecting if t.elapsed() > Duration::from_secs(10) => panic!("timed out"),
            Status::Connecting => std::thread::sleep(Duration::from_millis(50)),
        }
    }
    std::thread::sleep(Duration::from_secs(60));
}
