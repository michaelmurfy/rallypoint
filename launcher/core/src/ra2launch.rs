//! A Red Alert 2 game as the launcher hands it to the engine: the map, who plays (you, computer players and, online,
//! the other players with their tunnel ids) and the match options. Everything here may come from another player
//! (the online lobby), so `validate` checks every field and cleans the names before anything reaches the engine.
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

pub const MAX_PLAYERS: usize = 8;
/// Yuri's Revenge countries 0..9 (America, Korea, France, Germany, Great Britain, Libya, Iraq, Cuba, Russia, Yuri); -1 random.
pub const SIDES: i32 = 10;
/// Multiplayer colours 0..7 (yellow, red, blue, green, orange, teal, purple, pink); -1 any free one.
pub const COLORS: i32 = 8;
/// Teams A..D; -1 none.
pub const TEAMS: i32 = 4;
pub const CREDITS: std::ops::RangeInclusive<u32> = 1000..=100_000;
/// spawn.ini `GameSpeed`: 0 is the fastest, 6 the slowest.
pub const SLOWEST: u8 = 6;
pub const MAP_EXTENSIONS: [&str; 4] = ["map", "mpr", "yrm", "yro"];

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Difficulty {
    Easy,
    Medium,
    Hard,
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
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Ra2Setup {
    /// The map's file name, looked up in the player's own map folders (never a path).
    pub map: String,
    pub players: Vec<Ra2Player>,
    pub credits: u32,
    pub speed: u8,
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

/// The engine bundled with the launcher, if this build has one.
pub fn engine(resources: Option<&Path>) -> Option<PathBuf> {
    let exe = if cfg!(windows) { "ra2engine.exe" } else { "ra2engine" };
    resources.map(|r| r.join("ra2engine").join(exe)).filter(|p| p.is_file())
}

/// What the engine is given: the checked setup, this player's name, the display settings and the map's own path.
#[derive(Debug, Serialize)]
pub struct Launch {
    pub setup: Ra2Setup,
    pub player_name: String,
    pub display: crate::ra2::Ra2Config,
    pub map_path: PathBuf,
}

pub fn write_launch(file: &Path, l: &Launch) -> Result<(), String> {
    std::fs::write(file, serde_json::to_vec_pretty(l).map_err(|e| e.to_string())?).map_err(|e| e.to_string())
}

/// How the engine is started: in the game folder, with the setup written as JSON beside the launcher's data.
/// shortcut: the engine's launch interface is not final; this passes the setup file as the only argument. Upgrade when
/// the engine's spawner settles (likely a spawn.ini written from `Ra2Setup` and `[Tunnel]`/`[OtherN]` sections).
pub fn command(engine: &Path, game_dir: &Path, setup_file: &Path) -> std::process::Command {
    let mut c = std::process::Command::new(engine);
    c.current_dir(game_dir).arg(setup_file);
    c
}

#[cfg(test)]
mod tests {
    use super::*;

    fn human(name: &str) -> Ra2Player {
        Ra2Player { name: name.into(), ai: None, side: -1, color: -1, start: -1, team: -1, tunnel_id: None }
    }
    fn ai() -> Ra2Player {
        Ra2Player { ai: Some(Difficulty::Hard), name: "x".into(), ..human("") }
    }
    fn setup(players: Vec<Ra2Player>) -> Ra2Setup {
        Ra2Setup { map: "Little Big Lake.yrm".into(), players, credits: 10000, speed: 2, seed: 0, tunnel: Some(Tunnel { port: 1, id: 1 }) }
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
