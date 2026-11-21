//! A Red Alert 2 game as the launcher hands it to the engine: the map, who plays (you, computer players and, online,
//! the other players with their tunnel ids) and the match options. Everything here may come from another player
//! (the online lobby), so `validate` checks every field and cleans the names before anything reaches the engine.
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

pub const MAX_PLAYERS: usize = 8;
/// The engine's countries 0..9 (America, Korea, France, Germany, Great Britain, Libya, Iraq, Cuba, Russia, Yuri); -1
/// random. Yuri (9) needs an engine that lists it (`Country=9`, otherwise the engine stops with "unknown Country").
pub const SIDES: i32 = 10;
/// Multiplayer colours 0..7 (yellow, red, blue, green, orange, teal, purple, pink); -1 any free one.
pub const COLORS: i32 = 8;
/// Teams A..D; -1 none.
pub const TEAMS: i32 = 4;
pub const CREDITS: std::ops::RangeInclusive<u32> = 1000..=100_000;
/// The launch file's `GameSpeed`: 0 is the fastest, 6 the slowest.
pub const SLOWEST: u8 = 6;
pub const MAP_EXTENSIONS: [&str; 4] = ["map", "mpr", "yrm", "yro"];

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Difficulty {
    Easy,
    Medium,
    Brutal,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Ra2Player {
    /// A human's lobby name; ignored for a computer player.
    #[serde(default)]
    pub name: String,
    /// `None` for a human.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub ai: Option<Difficulty>,
    pub side: i32,
    pub color: i32,
    /// 1..=the map's start count, or -1 random.
    pub start: i32,
    pub team: i32,
    /// Online humans: the last octet of their room address. Filled by the host, checked here.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tunnel_id: Option<u8>,
}

/// The local tunnel the engine talks through, added by the launcher at launch (never taken from another player).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct Tunnel {
    pub port: u16,
    /// This player's own tunnel id.
    pub id: u8,
    /// What the engine must present to the agent (`Agent::arm_tunnel`); never sent to or taken from the page.
    #[serde(skip)]
    pub token: [u8; 16],
}

fn default_true() -> bool {
    true
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Ra2Setup {
    /// The map's file name, looked up in the player's own map folders (never a path).
    pub map: String,
    pub players: Vec<Ra2Player>,
    pub credits: u32,
    pub speed: u8,
    /// Random crates (engine `[Game] Crates=`).
    #[serde(default = "default_true")]
    pub crates: bool,
    /// Superweapons (engine `[Game] SuperWeapons=`).
    #[serde(default = "default_true")]
    pub superweapons: bool,
    /// Mid-game alliances (engine `[Game] AlliesAllowed=`).
    #[serde(default = "default_true")]
    pub allies: bool,
    /// AI auto-repair (engine `[Game] AutoRepair=`; CnCNet `[IQ] RepairSell=0`).
    #[serde(default = "default_true")]
    pub auto_repair: bool,
    /// Floor for random crates when crates are on (engine `CrateMinimum=`; CnCNet
    /// Increased=8, Extreme=40). `None` = the rules default.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub crate_minimum: Option<u32>,
    #[serde(default)]
    pub seed: u32,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tunnel: Option<Tunnel>,
}

/// A map file name with no folder in it and a map extension.
pub fn safe_map_name(name: &str) -> bool {
    let ext_ok = Path::new(name).extension().is_some_and(|e| MAP_EXTENSIONS.iter().any(|x| e.eq_ignore_ascii_case(x)));
    ext_ok && name.len() <= 128 && !name.starts_with('.') && !name.contains(['/', '\\', ':', '\0'])
}

/// A name safe to write into spawn.ini: the lobby rules, without the characters that end a key or a section.
pub fn clean_name(s: &str) -> String {
    let kept: String = s.chars().filter(|c| !"[]=;".contains(*c)).collect();
    crate::config::clean_player_name(&kept)
}

/// `setup` checked against a map with `starts` start positions; names come back cleaned. `online`: every human needs
/// a distinct tunnel id; offline there is exactly one human (you).
pub fn validate(mut setup: Ra2Setup, starts: usize, online: bool) -> Result<Ra2Setup, String> {
    if !safe_map_name(&setup.map) {
        return Err("That is not a map file name".into());
    }
    let n = setup.players.len();
    if n < 2 {
        return Err("A game needs at least two players".into());
    }
    if n > MAX_PLAYERS {
        return Err(format!("At most {MAX_PLAYERS} players"));
    }
    if n > starts {
        return Err(format!("This map has {starts} start positions, and there are {n} players"));
    }
    if !CREDITS.contains(&setup.credits) {
        return Err("Starting credits must be between 1,000 and 100,000".into());
    }
    if setup.speed > SLOWEST {
        return Err("Unknown game speed".into());
    }
    let (mut seen_start, mut seen_color, mut seen_tunnel) = (vec![], vec![], vec![]);
    for p in &mut setup.players {
        if !(-1..SIDES).contains(&p.side) || !(-1..COLORS).contains(&p.color) || !(-1..TEAMS).contains(&p.team) {
            return Err("A player's side, colour or team is out of range".into());
        }
        if p.start != -1 && !(1..=starts as i32).contains(&p.start) {
            return Err("A start position is not on this map".into());
        }
        for (v, seen, what) in [(p.start, &mut seen_start, "start position"), (p.color, &mut seen_color, "colour")] {
            if v != -1 {
                if seen.contains(&v) {
                    return Err(format!("Two players have the same {what}"));
                }
                seen.push(v);
            }
        }
        if p.ai.is_some() {
            p.name.clear();
            p.tunnel_id = None;
            continue;
        }
        p.name = clean_name(&p.name);
        if p.name.is_empty() {
            p.name = "Player".into();
        }
        if online {
            let id = p.tunnel_id.ok_or("A player has no tunnel id")?;
            if id == 0 || seen_tunnel.contains(&id) {
                return Err("Two players have the same tunnel id".into());
            }
            seen_tunnel.push(id);
        } else {
            p.tunnel_id = None;
        }
    }
    let humans = setup.players.iter().filter(|p| p.ai.is_none()).count();
    if !online && humans != 1 {
        return Err("A skirmish has one human player".into());
    }
    if online && humans < 2 {
        return Err("An online game needs at least two people".into());
    }
    setup.tunnel = None; // only the launcher's own agent says where the tunnel is
    Ok(setup)
}


/// The engine bundled with the launcher, if this build has one (`ra2game.exe` on Windows: it has no console window).
pub fn engine(resources: Option<&Path>) -> Option<PathBuf> {
    let exe = if cfg!(windows) { "ra2game.exe" } else { "ra2engine" };
    resources.map(|r| r.join("ra2engine").join(exe)).filter(|p| p.is_file())
}

/// The engine's launch file (ra2/README.md, "The launch file") for a checked `setup`: `map` is the exact `Map=` value
/// (a multimd.mix entry such as `xmp01t4.map`, or a path to a plain map file), `local` the index of the player this
/// machine plays. Online (a tunnel in `setup`) it is a network game; slot 0 must then be the host, a human. The
/// engine's INI cuts a value at `;`, so none may contain one.
pub fn launch_ini(setup: &Ra2Setup, map: &str, local: usize) -> Result<String, String> {
    use std::fmt::Write;
    if map.contains([';', '\n', '\r']) {
        return Err("The map's folder has a character the game cannot read (; or a line break). Move the map to another folder".into());
    }
    let p = setup.players.get(local).ok_or("This player is not in the game")?;
    if p.ai.is_some() {
        return Err("This player is not in the game".into());
    }
    let online = setup.tunnel.is_some();
    if online && setup.players[0].ai.is_some() {
        return Err("The host must be the first player".into());
    }
    let pick = |v: i32, max: i32| if (0..max).contains(&v) { v.to_string() } else { "random".to_string() };
    let yn = |b: bool| if b { "yes" } else { "no" };
    let mut f = String::new();
    let _ = writeln!(
        f,
        "[Game]\nMode={}\nMap={map}\nLocalPlayer={local}\nCredits={}\nGameSpeed={}\nCrates={}\nSuperWeapons={}\nAlliesAllowed={}\nAutoRepair={}",
        if online { "network" } else { "skirmish" },
        setup.credits,
        setup.speed.min(SLOWEST),
        yn(setup.crates),
        yn(setup.superweapons),
        yn(setup.allies),
        yn(setup.auto_repair),
    );
    if setup.crates {
        if let Some(n) = setup.crate_minimum {
            let _ = writeln!(f, "CrateMinimum={n}");
        }
    }
    if online || setup.seed != 0 {
        let _ = writeln!(f, "Seed={}", setup.seed);
    }
    if let Some(t) = setup.tunnel {
        let token: String = t.token.iter().map(|b| format!("{b:02x}")).collect();
        let _ = writeln!(f, "\n[Tunnel]\nAddress=127.0.0.1\nPort={}\nVersion=2\nToken={token}", t.port);
    }
    for (n, p) in setup.players.iter().take(MAX_PLAYERS).enumerate() {
        let _ = writeln!(f, "\n[Player{n}]");
        match p.ai {
            Some(d) => {
                let _ = writeln!(f, "Type=ai\nAI={}", match d { Difficulty::Easy => "easy", Difficulty::Medium => "medium", Difficulty::Brutal => "brutal" });
            }
            None => {
                let _ = writeln!(f, "Type=human\nName={}", clean_name(&p.name));
                if let (true, Some(id)) = (online, p.tunnel_id) {
                    let _ = writeln!(f, "TunnelId={id}");
                }
            }
        }
        let team = if (0..TEAMS).contains(&p.team) { ((b'A' + p.team as u8) as char).to_string() } else { "none".into() };
        let start = if (1..=MAX_PLAYERS as i32).contains(&p.start) { p.start.to_string() } else { "random".into() };
        let _ = writeln!(f, "Country={}\nColor={}\nStart={start}\nTeam={team}", pick(p.side, SIDES), pick(p.color, COLORS));
    }
    Ok(f)
}

/// The engine's command line: the game files, the launch file, the window. A size of 0 x 0 is the screen's own.
pub fn command(engine: &Path, game_dir: &Path, launch_file: &Path, display: &crate::ra2::Ra2Config) -> std::process::Command {
    let mut c = std::process::Command::new(engine);
    c.arg("--data").arg(game_dir).arg("--launch").arg(launch_file);
    if display.width > 0 && display.height > 0 {
        c.arg("--width").arg(display.width.to_string()).arg("--height").arg(display.height.to_string());
    }
    c.arg(if display.windowed { "--windowed" } else { "--fullscreen" }).arg("--title").arg("Red Alert 2");
    c
}

#[cfg(test)]
mod tests {
    use super::*;

    fn human(name: &str) -> Ra2Player {
        Ra2Player { name: name.into(), ai: None, side: -1, color: -1, start: -1, team: -1, tunnel_id: None }
    }
    fn ai() -> Ra2Player {
        Ra2Player { ai: Some(Difficulty::Brutal), name: "x".into(), ..human("") }
    }
    fn setup(players: Vec<Ra2Player>) -> Ra2Setup {
        Ra2Setup {
            map: "Little Big Lake.yrm".into(),
            players,
            credits: 10000,
            speed: 2,
            crates: true,
            superweapons: true,
            allies: true,
            auto_repair: true,
            crate_minimum: None,
            seed: 0,
            tunnel: Some(Tunnel { port: 1, id: 1, token: [0; 16] }),
        }
    }

    #[test]
    fn a_skirmish_fits_the_map_and_has_unique_starts_and_colours() {
        let ok = validate(setup(vec![human("Me"), ai()]), 2, false).unwrap();
        assert_eq!((ok.players[1].name.as_str(), ok.tunnel), ("", None), "an AI has no name; the tunnel is never taken from the setup");
        assert!(validate(setup(vec![human("Me"), ai(), ai()]), 2, false).unwrap_err().contains("2 start positions"));
        assert!(validate(setup(vec![human("Me")]), 4, false).is_err(), "nobody to play against");
        assert!(validate(setup(vec![human("Me"), ai(), ai(), ai(), ai(), ai(), ai(), ai(), ai()]), 9, false).is_err());
        let mut s = setup(vec![human("Me"), ai()]);
        s.players[0].start = 2;
        s.players[1].start = 2;
        assert!(validate(s.clone(), 2, false).unwrap_err().contains("same start"));
        s.players[1].start = 3;
        assert!(validate(s.clone(), 2, false).unwrap_err().contains("not on this map"));
        s.players[1].start = 1;
        s.players[0].color = 4;
        s.players[1].color = 4;
        assert!(validate(s.clone(), 2, false).unwrap_err().contains("same colour"));
        s.players[1].color = 8;
        assert!(validate(s.clone(), 2, false).is_err(), "colour out of range");
        s.players[1].color = -1;
        s.credits = 5;
        assert!(validate(s, 2, false).is_err());
        assert!(validate(setup(vec![human("a"), human("b")]), 2, false).unwrap_err().contains("one human"));
    }

    #[test]
    fn names_and_map_names_cannot_break_out() {
        let s = validate(setup(vec![human(" [Boss]=1;\nmore text here "), ai()]), 2, false).unwrap();
        assert_eq!(s.players[0].name, "Boss1more te");
        assert_eq!(validate(setup(vec![human("\u{7}é"), ai()]), 2, false).unwrap().players[0].name, "Player");
        for bad in ["../x.map", "C:x.map", "a/b.yrm", "x.ini", ".map", "a\\b.mpr"] {
            assert!(!safe_map_name(bad), "{bad}");
        }
        assert!(safe_map_name("Golden State Fwy.yrm") && safe_map_name("tn01t2.MAP"));
    }

    #[test]
    fn the_launch_file_says_everything_the_engine_needs() {
        let mut s = validate(setup(vec![human("Me"), ai()]), 4, false).unwrap();
        s.players[0] = Ra2Player { side: 8, color: 2, start: 3, team: 1, ..s.players[0].clone() };
        let ini = launch_ini(&s, "/maps/Lake.yrm", 0).unwrap();
        assert_eq!(
            ini,
            "[Game]\nMode=skirmish\nMap=/maps/Lake.yrm\nLocalPlayer=0\nCredits=10000\nGameSpeed=2\nCrates=yes\nSuperWeapons=yes\nAlliesAllowed=yes\nAutoRepair=yes\n\n[Player0]\nType=human\nName=Me\nCountry=8\nColor=2\nStart=3\nTeam=B\n\n[Player1]\nType=ai\nAI=brutal\nCountry=random\nColor=random\nStart=random\nTeam=none\n"
        );
        assert!(launch_ini(&s, "/a;b/x.map", 0).is_err(), "a ; would cut the value");
        assert!(launch_ini(&s, "/x.map", 1).is_err(), "the computer is not this machine");
        assert!(launch_ini(&s, "xmp01t4.map", 0).unwrap().contains("Map=xmp01t4.map\n"), "a multimd entry is Map= as-is");
        s.crates = false;
        s.superweapons = false;
        s.allies = false;
        s.auto_repair = false;
        let off = launch_ini(&s, "/maps/Lake.yrm", 0).unwrap();
        for want in ["Crates=no\n", "SuperWeapons=no\n", "AlliesAllowed=no\n", "AutoRepair=no\n"] {
            assert!(off.contains(want), "{want} in {off}");
        }
        assert!(!off.contains("CrateMinimum="), "no crate floor when crates are off");
        s.crates = true;
        s.crate_minimum = Some(40);
        let dense = launch_ini(&s, "/maps/Lake.yrm", 0).unwrap();
        assert!(dense.contains("Crates=yes\n") && dense.contains("CrateMinimum=40\n"), "{dense}");
        // online: network mode, the tunnel, everyone's tunnel id and the seed
        let (mut a, mut b) = (human("Host"), human("Guest"));
        a.tunnel_id = Some(1);
        b.tunnel_id = Some(7);
        let mut s = validate(setup(vec![a, b, ai()]), 4, true).unwrap();
        s.seed = 99;
        s.tunnel = Some(Tunnel { port: 50000, id: 7, token: [0xAB; 16] });
        let ini = launch_ini(&s, "C:\\RA2\\x.map", 1).unwrap();
        for want in ["Mode=network\n", "LocalPlayer=1\n", "Seed=99\n", "[Tunnel]\nAddress=127.0.0.1\nPort=50000\nVersion=2\nToken=abababababababababababababababab\n", "Name=Host\nTunnelId=1\n", "Name=Guest\nTunnelId=7\n"] {
            assert!(ini.contains(want), "{want} in {ini}");
        }
        s.players.swap(0, 2);
        assert!(launch_ini(&s, "/x.map", 1).unwrap_err().contains("host"));
    }

    #[test]
    fn online_humans_need_distinct_tunnel_ids() {
        let mut a = human("a");
        a.tunnel_id = Some(2);
        let mut b = human("b");
        b.tunnel_id = Some(2);
        assert!(validate(setup(vec![a.clone(), b.clone()]), 4, true).unwrap_err().contains("same tunnel"));
        b.tunnel_id = Some(3);
        assert!(validate(setup(vec![a.clone(), b.clone(), ai()]), 4, true).is_ok());
        assert!(validate(setup(vec![a, ai()]), 4, true).unwrap_err().contains("two people"));
        assert!(validate(setup(vec![human("x"), b]), 4, true).unwrap_err().contains("no tunnel"));
    }
}
