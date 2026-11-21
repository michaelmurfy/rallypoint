//! The launcher's own Red Alert 2 lobby, carried as small JSON blobs between the players of a relay room
//! (`Agent::send_lobby` / `take_lobby`). Every player says `hello` and its own seat (`ready`) about once a second; the
//! host (the room master) also sends the room's `state` and, to begin, `start` with the whole setup. Everything received
//! comes from other players, so it is size-capped, parsed strictly and range-checked, and only the host is heard on
//! the room's settings and the start.
use crate::ra2launch::{self, Difficulty, Ra2Player, Ra2Setup, COLORS, SIDES, TEAMS};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

pub const MAX_BLOB: usize = 1024;
/// A player not heard from for this long has left.
pub const PEER_TIMEOUT_MS: u64 = 6000;
/// How many polls the start is repeated for (the relay is UDP).
const START_REPEATS: u8 = 3;

/// One player's own choices. `start` is 1-based, -1 random.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct Seat {
    pub side: i32,
    pub color: i32,
    pub start: i32,
    pub team: i32,
    #[serde(default)]
    pub ready: bool,
}

impl Default for Seat {
    fn default() -> Self {
        Seat { side: -1, color: -1, start: -1, team: -1, ready: false }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct AiSeat {
    pub difficulty: Difficulty,
    pub side: i32,
    pub color: i32,
    pub start: i32,
    pub team: i32,
}

/// What the host chose for the room.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct RoomSettings {
    /// File name, as in `Ra2Setup::map`.
    pub map: String,
    /// The map's own name, for showing.
    pub map_name: String,
    pub starts: u8,
    pub credits: u32,
    pub speed: u8,
    #[serde(default)]
    pub ai: Vec<AiSeat>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "t", rename_all = "lowercase")]
pub enum Msg {
    Hello { name: String, master: bool },
    Ready(Seat),
    State(RoomSettings),
    Start(Ra2Setup),
}

fn seat_ok(side: i32, color: i32, start: i32, team: i32) -> bool {
    (-1..SIDES).contains(&side) && (-1..COLORS).contains(&color) && (-1..=ra2launch::MAX_PLAYERS as i32).contains(&start) && start != 0 && (-1..TEAMS).contains(&team)
}

/// A blob for `send_lobby`; an error if it would not fit.
pub fn encode(m: &Msg) -> Result<Vec<u8>, String> {
    let b = serde_json::to_vec(m).map_err(|e| e.to_string())?;
    if b.len() > MAX_BLOB {
        return Err("The game setup is too big to send (try a map with a shorter file name)".into());
    }
    Ok(b)
}

/// A received blob, checked. Names come back cleaned.
pub fn decode(b: &[u8]) -> Option<Msg> {
    if b.len() > MAX_BLOB {
        return None;
    }
    match serde_json::from_slice(b).ok()? {
        Msg::Hello { name, master } => Some(Msg::Hello { name: ra2launch::clean_name(&name), master }),
        Msg::Ready(s) if seat_ok(s.side, s.color, s.start, s.team) => Some(Msg::Ready(s)),
        Msg::State(r)
            if ra2launch::safe_map_name(&r.map)
                && (2..=ra2launch::MAX_PLAYERS as u8).contains(&r.starts)
                && ra2launch::CREDITS.contains(&r.credits)
                && r.speed <= ra2launch::SLOWEST
                && r.ai.len() < ra2launch::MAX_PLAYERS
                && r.ai.iter().all(|a| seat_ok(a.side, a.color, a.start, a.team)) =>
        {
            let map_name: String = r.map_name.chars().filter(|c| !c.is_control()).take(64).collect();
            Some(Msg::State(RoomSettings { map_name, ..r }))
        }
        // the receiver checks it again against its own copy of the map before launching
        Msg::Start(s) => ra2launch::validate(s, ra2launch::MAX_PLAYERS, true).ok().map(Msg::Start),
        _ => None,
    }
}

#[derive(Debug, Clone)]
struct Peer {
    name: String,
    master: bool,
    seat: Seat,
    heard_ms: u64,
}

#[derive(Debug, Clone, Serialize)]
pub struct PlayerView {
    pub id: u8,
    pub name: String,
    pub host: bool,
    pub me: bool,
    #[serde(flatten)]
    pub seat: Seat,
}

#[derive(Debug, Clone, Serialize)]
pub struct LobbyView {
    pub me: u8,
    pub host: u8,
    pub am_host: bool,
    pub players: Vec<PlayerView>,
    pub settings: Option<RoomSettings>,
    /// Why Start is not possible yet; empty when it is.
    pub blocked: String,
    /// A start from the host for this player, given once.
    pub start: Option<Ra2Setup>,
}

#[derive(Debug, Default)]
pub struct Lobby {
    me: u8,
    name: String,
    /// What the relay said about this player (`None`: an older server, so the lowest address hosts).
    master: Option<bool>,
    seat: Seat,
    peers: BTreeMap<u8, Peer>,
    /// The settings and who sent them (this player when hosting).
    settings: Option<(u8, RoomSettings)>,
    start_in: Option<Ra2Setup>,
    start_out: Option<(Ra2Setup, u8)>,
    last_seed: Option<u32>,
}

impl Lobby {
    /// Who this player is, from the agent: the last octet of its room address, its name, and whether it is the master.
    /// A new address (rejoined) starts the lobby over.
    pub fn set_self(&mut self, me: u8, name: &str, master: Option<bool>) {
        if me != self.me {
            *self = Lobby { seat: self.seat, ..Default::default() };
            self.me = me;
        }
        self.name = ra2launch::clean_name(name);
        self.master = master;
    }

    pub fn host(&self) -> u8 {
        if self.master == Some(true) {
            return self.me;
        }
        if let Some((id, _)) = self.peers.iter().find(|(_, p)| p.master) {
            return *id;
        }
        self.peers.keys().copied().chain([self.me]).min().unwrap_or(self.me)
    }

    pub fn is_host(&self) -> bool {
        self.host() == self.me
    }

    pub fn receive(&mut self, now_ms: u64, from: u8, blob: &[u8]) {
        if from == self.me || from == 0 {
            return;
        }
        let Some(m) = decode(blob) else { return };
        let fresh = || Peer { name: String::new(), master: false, seat: Seat::default(), heard_ms: now_ms };
        match m {
            Msg::Hello { name, master } => {
                let p = self.peers.entry(from).or_insert_with(fresh);
                (p.name, p.master, p.heard_ms) = (name, master, now_ms);
            }
            Msg::Ready(s) => {
                let p = self.peers.entry(from).or_insert_with(fresh);
                (p.seat, p.heard_ms) = (s, now_ms);
            }
            Msg::State(r) if from == self.host() => self.settings = Some((from, r)),
            Msg::Start(s) if from == self.host() && self.last_seed != Some(s.seed) && s.players.iter().any(|p| p.tunnel_id == Some(self.me)) => {
                self.last_seed = Some(s.seed);
                self.start_in = Some(s);
            }
            _ => {}
        }
    }

    pub fn prune(&mut self, now_ms: u64) {
        self.peers.retain(|_, p| now_ms.saturating_sub(p.heard_ms) < PEER_TIMEOUT_MS);
    }

    pub fn set_seat(&mut self, seat: Seat) -> Result<(), String> {
        if !seat_ok(seat.side, seat.color, seat.start, seat.team) {
            return Err("That choice is out of range".into());
        }
        self.seat = seat;
        Ok(())
    }

    /// The host's choice of map and options (checked by the caller against its own map list).
    pub fn set_settings(&mut self, r: RoomSettings) -> Result<(), String> {
        if !self.is_host() {
            return Err("Only the host chooses the map and options".into());
        }
        let b = encode(&Msg::State(r.clone()))?;
        decode(&b).ok_or("Those settings are out of range")?;
        self.settings = Some((self.me, r));
        Ok(())
    }

    fn current_settings(&self) -> Option<&RoomSettings> {
        self.settings.as_ref().filter(|(from, _)| *from == self.host()).map(|(_, r)| r)
    }

    /// What to send this time round.
    pub fn outgoing(&mut self) -> Vec<Vec<u8>> {
        let mut out = vec![Msg::Hello { name: self.name.clone(), master: self.master == Some(true) }, Msg::Ready(self.seat)];
        if self.is_host() {
            out.extend(self.current_settings().cloned().map(Msg::State));
            if let Some((s, left)) = self.start_out.as_mut() {
                out.push(Msg::Start(s.clone()));
                *left -= 1;
                if *left == 0 {
                    self.start_out = None;
                }
            }
        }
        out.iter().filter_map(|m| encode(m).ok()).collect()
    }

    /// The host's game, or why it cannot start yet. The host is player 0 (the engine's lockstep master), then the
    /// others by address, then the computer players.
    pub fn build_start(&self, seed: u32) -> Result<Ra2Setup, String> {
        if !self.is_host() {
            return Err("Only the host can start the game".into());
        }
        let r = self.current_settings().ok_or("Choose a map first")?;
        if self.peers.is_empty() {
            return Err("Waiting for another player to join the room".into());
        }
        if let Some(p) = self.peers.values().find(|p| !p.seat.ready) {
            return Err(format!("Waiting for {} to be ready", if p.name.is_empty() { "a player" } else { &p.name }));
        }
        let human = |id: u8, name: &str, s: Seat| Ra2Player { name: name.into(), ai: None, side: s.side, color: s.color, start: s.start, team: s.team, tunnel_id: Some(id) };
        let mut players: Vec<Ra2Player> = std::iter::once(human(self.me, &self.name, self.seat)).chain(self.peers.iter().map(|(id, p)| human(*id, &p.name, p.seat))).collect();
        players.extend(r.ai.iter().map(|a| Ra2Player { name: String::new(), ai: Some(a.difficulty), side: a.side, color: a.color, start: a.start, team: a.team, tunnel_id: None }));
        let setup = Ra2Setup { map: r.map.clone(), players, credits: r.credits, speed: r.speed, seed, tunnel: None };
        let setup = ra2launch::validate(setup, r.starts as usize, true)?;
        encode(&Msg::Start(setup.clone()))?;
        Ok(setup)
    }

    /// Start (host): the setup goes out over the next few polls and comes back for this player's own launch.
    pub fn start(&mut self, seed: u32) -> Result<Ra2Setup, String> {
        let s = self.build_start(seed)?;
        self.start_out = Some((s.clone(), START_REPEATS));
        Ok(s)
    }

    pub fn view(&mut self) -> LobbyView {
        let host = self.host();
        let mut players: Vec<PlayerView> = std::iter::once(PlayerView { id: self.me, name: self.name.clone(), host: host == self.me, me: true, seat: self.seat })
            .chain(self.peers.iter().map(|(id, p)| PlayerView { id: *id, name: p.name.clone(), host: host == *id, me: false, seat: p.seat }))
            .collect();
        players.sort_by_key(|p| p.id);
        LobbyView {
            me: self.me,
            host,
            am_host: host == self.me,
            players,
            settings: self.current_settings().cloned(),
            blocked: if host == self.me { self.build_start(0).err().unwrap_or_default() } else { String::new() },
            start: self.start_in.take(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn blob(m: &Msg) -> Vec<u8> {
        encode(m).unwrap()
    }
    fn settings() -> RoomSettings {
        RoomSettings { map: "Lake.yrm".into(), map_name: "Lake".into(), starts: 4, credits: 10000, speed: 2, ai: vec![AiSeat { difficulty: Difficulty::Easy, side: -1, color: -1, start: -1, team: -1 }] }
    }

    #[test]
    fn messages_round_trip_and_bad_ones_are_dropped() {
        for m in [Msg::Hello { name: "Ann".into(), master: true }, Msg::Ready(Seat { side: 8, color: 7, start: 8, team: 3, ready: true }), Msg::State(settings())] {
            assert_eq!(decode(&blob(&m)), Some(m));
        }
        assert_eq!(String::from_utf8(blob(&Msg::Ready(Seat::default()))).unwrap(), r#"{"t":"ready","side":-1,"color":-1,"start":-1,"team":-1,"ready":false}"#);
        assert_eq!(decode(br#"{"t":"hello","name":"[x]=y;\u0007","master":false}"#), Some(Msg::Hello { name: "xy".into(), master: false }));
        for bad in [
            &br#"{"t":"ready","side":9,"color":0,"start":1,"team":0}"#[..],
            br#"{"t":"ready","side":0,"color":0,"start":0,"team":0}"#,
            br#"{"t":"state","map":"../x.map","map_name":"x","starts":2,"credits":5000,"speed":1}"#,
            br#"{"t":"state","map":"x.map","map_name":"x","starts":9,"credits":5000,"speed":1}"#,
            br#"{"t":"nope"}"#,
            b"not json",
        ] {
            assert_eq!(decode(bad), None, "{}", String::from_utf8_lossy(bad));
        }
        let mut big = br#"{"t":"hello","master":false,"name":""#.to_vec();
        big.extend(vec![b'a'; MAX_BLOB]);
        big.extend(br#""}"#);
        assert_eq!(decode(&big), None);
    }

    #[test]
    fn the_master_hosts_and_only_the_host_sets_the_room_and_starts() {
        let (mut a, mut b) = (Lobby::default(), Lobby::default());
        a.set_self(1, "Ann", Some(true));
        b.set_self(2, "Bob", Some(false));
        a.set_settings(settings()).unwrap();
        assert_eq!(a.build_start(1).unwrap_err(), "Waiting for another player to join the room");
        for m in a.outgoing() {
            b.receive(0, 1, &m);
        }
        for m in b.outgoing() {
            a.receive(0, 2, &m);
        }
        assert_eq!((b.host(), b.view().settings.unwrap().map_name.as_str()), (1, "Lake"));
        assert!(b.set_settings(settings()).is_err(), "only the host sets the room");
        assert_eq!(a.view().blocked, "Waiting for Bob to be ready");
        // a forged state from a non-host is ignored
        let mut other = settings();
        other.map_name = "Forged".into();
        a.receive(0, 2, &blob(&Msg::State(other)));
        assert_eq!(a.view().settings.unwrap().map_name, "Lake");
        b.set_seat(Seat { ready: true, color: 3, ..Seat::default() }).unwrap();
        for m in b.outgoing() {
            a.receive(10, 2, &m);
        }
        assert_eq!(a.view().blocked, "");
        let s = a.start(42).unwrap();
        assert_eq!(s.players.iter().map(|p| p.tunnel_id).collect::<Vec<_>>(), [Some(1), Some(2), None]);
        let sent: Vec<Vec<u8>> = (0..4).flat_map(|_| a.outgoing()).collect();
        assert_eq!(sent.iter().filter(|m| m.starts_with(br#"{"t":"start""#)).count(), START_REPEATS as usize);
        for m in &sent {
            b.receive(20, 1, m);
        }
        let got = b.view().start.expect("Bob gets the start once");
        assert_eq!((got.seed, got.players[1].name.as_str()), (42, "Bob"));
        assert!(b.view().start.is_none(), "repeats of the same start are not launched twice");
        // nobody else's start is heard
        let mut c = Lobby::default();
        c.set_self(3, "Cy", Some(false));
        c.receive(0, 1, &blob(&Msg::Hello { name: "Ann".into(), master: true }));
        c.receive(0, 2, &blob(&Msg::Start(s.clone())));
        assert!(c.view().start.is_none(), "from a non-host");
        c.receive(0, 1, &blob(&Msg::Start(s)));
        assert!(c.view().start.is_none(), "a start that does not seat this player");
    }

    #[test]
    fn without_a_master_the_lowest_address_hosts_and_silent_players_leave() {
        let mut b = Lobby::default();
        b.set_self(5, "Bob", None);
        assert!(b.is_host());
        b.receive(0, 3, &blob(&Msg::Hello { name: "Ann".into(), master: false }));
        assert_eq!(b.host(), 3);
        b.prune(PEER_TIMEOUT_MS + 1);
        assert!(b.is_host() && b.view().players.len() == 1);
        let mut c = Lobby::default();
        c.set_self(5, "Bob", None);
        c.receive(0, 3, &blob(&Msg::Ready(Seat { color: 9, ..Seat::default() })));
        assert_eq!(c.view().players.len(), 1, "out-of-range seat dropped");
    }
}
