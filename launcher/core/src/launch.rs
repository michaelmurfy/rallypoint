//! Build the engine command line. Resolution flags are the engine's existing `-xres/-yres`;
//! arbitrary-size and zoom support land with the Phase 2 engine patches.
use crate::proc::quiet;
use std::path::Path;
use std::process::Command;

#[derive(Debug, Clone, Default, serde::Deserialize)]
pub struct LaunchOptions {
    pub width: u32,
    pub height: u32,
    pub fullscreen: bool,
    /// Full path to a mod folder/.big, if any.
    pub mod_path: Option<String>,
    #[serde(default)]
    pub mod_args: Vec<String>,
    /// Tunnel agent address and our virtual IP; the patched engine reads these from the environment.
    #[serde(default)]
    pub relay: Option<(String, String)>,
    /// Extra environment for the engine (e.g. `GEN_AUTO_SKIRMISH=1`).
    #[serde(default)]
    pub env: Vec<(String, String)>,
    /// GenTool-style higher camera. Off unless the user asks, so online games keep the retail view.
    #[serde(default)]
    pub raised_camera: bool,
    /// Where to write what the engine prints (its network and error lines); replaced on every launch.
    #[serde(default)]
    pub log: Option<std::path::PathBuf>,
    /// Folder for the engine's per-frame simulation logs (see `syncdiff`). `None` = not recording.
    #[serde(default)]
    pub sync_logs: Option<std::path::PathBuf>,
}

pub fn args(o: &LaunchOptions) -> Vec<String> {
    let mut a = vec![
        if o.fullscreen { "-fullscreen" } else { "-win" }.to_string(),
        "-xres".into(),
        o.width.to_string(),
        "-yres".into(),
        o.height.to_string(),
    ];
    if let Some(m) = &o.mod_path {
        a.extend(["-mod".into(), m.clone()]);
    }
    a.extend(o.mod_args.iter().cloned());
    // The first ten seconds of simulation frame by frame, then every checksum frame (every 100) for the rest of the game: enough to see where two
    // players' games first differ, at the start or minutes in. (The engine decides which frames to write; see CRCDebug.cpp.)
    if let Some(dir) = &o.sync_logs {
        a.extend(["-DebugCRCFromFrame".into(), "0".into(), "-DebugCRCUntilFrame".into(), "1000000".into(), "-SaveDebugCRCPerFrame".into(), dir.to_string_lossy().to_string()]);
        // Per-object lines, not only the frame total, so a desync log says which object diverged.
        a.push("-LogObjectCRCs".into());
        // The game state is only written out when a checksum is taken (every 100 frames normally): every 10 narrows down where it went wrong.
        a.extend(["-NetCRCInterval".into(), "10".into(), "-ReplayCRCInterval".into(), "10".into()]);
    }
    // EA and Generals logo movies. The engine's own -nologo; the menu scene stays.
    a.push("-nologo".into());
    a
}

/// The engine only creates the last folder of the sync-log path and ignores failure, so a missing
/// parent means the per-frame files are never written. Make the folder before the game starts.
pub fn prepare_sync_logs(dir: &Path) -> std::io::Result<()> {
    std::fs::create_dir_all(dir)
}

/// Loading picture for a mod folder. The engine otherwise paints `Install_Final.bmp` from the game
/// folder, which is Zero Hour's. Shockwave ships `Install_Final_shw.bmp` beside its archives.
pub fn splash_bitmap(mod_dir: &Path) -> Option<std::path::PathBuf> {
    for name in ["Install_Final.bmp", "Install_Final_shw.bmp"] {
        let path = mod_dir.join(name);
        if path.is_file() {
            return Some(path);
        }
    }
    None
}

/// The game's own picture for the Play page: the mod's loading picture when it has one, else Zero Hour's from the game
/// folder. Read from the player's files at run time (nothing of it ships with Rallypoint). Only a plain BMP of sane size.
pub fn game_art(game_dir: &Path, mod_dir: Option<&Path>) -> Option<Vec<u8>> {
    let candidates = mod_dir.and_then(splash_bitmap).into_iter().chain(Some(game_dir.join("Install_Final.bmp")));
    for path in candidates {
        let Ok(meta) = std::fs::metadata(&path) else { continue };
        if !meta.is_file() || meta.len() < 64 || meta.len() > 6 * 1024 * 1024 {
            continue;
        }
        if let Ok(bytes) = std::fs::read(&path) {
            if bytes.starts_with(b"BM") {
                return Some(bytes);
            }
        }
    }
    None
}

/// Environment for a skirmish started from the map list: the map (a folder in the game's Maps folder, which `exists` checks),
/// how many computer opponents (1 to 7), how hard they are and which start position (1 to 8) the player takes. Anything else is refused: these come from the page.
pub fn skirmish_env(map: Option<&str>, ai: Option<u32>, difficulty: Option<&str>, start: Option<u32>, exists: impl Fn(&str) -> bool) -> Result<Vec<(String, String)>, String> {
    let mut env = vec![];
    if let Some(m) = map {
        if !crate::maporg::is_safe_name(m) || !exists(m) {
            return Err(format!("{m:?} isn't a map you have (is it switched on?)"));
        }
        env.push(("GEN_AUTO_MAP".to_string(), m.to_string()));
    }
    if let Some(n) = ai {
        if !(1..=7).contains(&n) {
            return Err("a skirmish has 1 to 7 opponents".into());
        }
        env.push(("GEN_AUTO_AI".to_string(), n.to_string()));
    }
    if let Some(d) = difficulty {
        if !["easy", "medium", "brutal"].contains(&d) {
            return Err("the difficulty is easy, medium or brutal".into());
        }
        env.push(("GEN_AUTO_DIFF".to_string(), d.to_string()));
    }
    if let Some(n) = start {
        if !(1..=8).contains(&n) {
            return Err("a start position is 1 to 8".into());
        }
        env.push(("GEN_AUTO_START".to_string(), n.to_string()));
    }
    Ok(env)
}

/// One place at the table of a skirmish, as the map preview sets it up.
#[derive(Debug, Clone, serde::Deserialize)]
pub struct SlotSpec {
    /// `player` (you, always first), `ai` or `closed`.
    pub kind: String,
    /// For an `ai`: easy, medium or brutal.
    pub difficulty: Option<String>,
    /// The general's internal name (`FactionChinaTankGeneral`); `None` is random.
    pub faction: Option<String>,
    /// Team 0 to 3; `None` is every one for themselves.
    pub team: Option<u32>,
    /// Start position 1 to 8 on the map; `None` is random.
    pub start: Option<u32>,
}

/// The whole table for `GEN_AUTO_SLOTS`: `player,<general|random>,<team|-1>,<start|0>`, `ai,<difficulty>,<general|random>,<team|-1>,<start|0>`
/// or `closed`, joined by `;`, you first. Generals must be ones the chosen game offers (`generals`), starts must not repeat.
pub fn skirmish_slots_env(slots: &[SlotSpec], generals: &[crate::generals::General]) -> Result<String, String> {
    if slots.len() < 2 || slots.len() > 8 {
        return Err("a skirmish has 2 to 8 places".into());
    }
    if slots[0].kind != "player" {
        return Err("you are the first place".into());
    }
    if !slots[1..].iter().any(|s| s.kind == "ai") {
        return Err("a skirmish needs at least one computer opponent".into());
    }
    let mut starts = std::collections::HashSet::new();
    let mut parts = vec![];
    for s in slots {
        if s.kind == "closed" {
            parts.push("closed".to_string());
            continue;
        }
        let faction = match &s.faction {
            None => "random".to_string(),
            Some(f) if generals.iter().any(|g| &g.id == f) => f.clone(),
            Some(f) => return Err(format!("{f:?} isn't a general in this game")),
        };
        let team = match s.team {
            None => "-1".to_string(),
            Some(t) if t <= 3 => t.to_string(),
            Some(_) => return Err("a team is 1 to 4".into()),
        };
        let start = match s.start {
            None => "0".to_string(),
            Some(n) if (1..=8).contains(&n) && starts.insert(n) => n.to_string(),
            Some(_) => return Err("a start position is 1 to 8 and each can be taken once".into()),
        };
        match s.kind.as_str() {
            "player" => parts.push(format!("player,{faction},{team},{start}")),
            "ai" => {
                let d = s.difficulty.as_deref().unwrap_or("easy");
                if !["easy", "medium", "brutal"].contains(&d) {
                    return Err("the difficulty is easy, medium or brutal".into());
                }
                parts.push(format!("ai,{d},{faction},{team},{start}"));
            }
            other => return Err(format!("{other:?} isn't a kind of place")),
        }
    }
    Ok(parts.join(";"))
}

/// Is the network ready for the game? No relay at all means offline play, which is fine. A relay that has
/// not accepted us yet would start the game on the wrong network (and fail with a socket error in the LAN
/// lobby), so that is refused. Returns the agent address and our virtual IP to hand to the engine.
pub fn relay_for_launch(agent: Option<(String, crate::agent::Status)>) -> Result<Option<(String, String)>, String> {
    use crate::agent::Status;
    match agent {
        None => Ok(None),
        Some((local, Status::Connected(v))) => Ok(Some((local, format!("{}.{}.{}.{}", v[0], v[1], v[2], v[3])))),
        Some((_, Status::Connecting)) => Err("Still connecting to the server. Try again in a moment.".into()),
        Some((_, Status::Rejected(_))) => Err("You are not connected to the server (see the Online tab). Fix that, or disconnect to play offline.".into()),
    }
}

/// Runs the engine from the game dir so it finds the `.big` files.
pub fn command(engine: &Path, game_dir: &Path, o: &LaunchOptions) -> Command {
    let mut c = Command::new(engine);
    quiet(&mut c).current_dir(game_dir).args(args(o)); // no stray console if the engine is a console-subsystem build
    c.envs(o.env.iter().map(|(k, v)| (k, v)));
    // The macOS engine only accepts resolutions it advertises; naming the chosen one makes any size (4K, ultrawide,
    // a custom value) valid. Other platforms ignore it.
    if o.width >= 640 && o.height >= 480 {
        c.env("GEN_EXTRA_RES", format!("{}x{}", o.width, o.height));
    }
    if let Some((agent, vip)) = &o.relay {
        c.env("GEN_RELAY", agent).env("GEN_RELAY_VIP", vip);
    }
    // Weather.ini is only in Zero Hour's archives. If the process is not started in that folder, the engine
    // reads this and loads them. A trailing separator is added on the engine side.
    c.env("GEN_ZH_PATH", game_dir);
    // The engine paints Install_Final.bmp from the game folder unless this names a mod's picture.
    if let Some(dir) = o.mod_path.as_deref() {
        if let Some(bmp) = splash_bitmap(Path::new(dir)) {
            c.env("GEN_SPLASH", bmp);
        }
    }
    if o.raised_camera {
        c.env("GEN_MAX_CAMERA_HEIGHT", "600");
        c.env("GEN_CAMERA_PITCH", "37");
    }
    if let Some(f) = o.log.as_ref().and_then(|p| open_log(p, o)) {
        if let Ok(g) = f.try_clone() {
            c.stdout(g);
        }
        c.stderr(f);
    }
    c
}

/// Fresh log file with a first line saying how the engine was started (never the room password or serial).
fn open_log(path: &Path, o: &LaunchOptions) -> Option<std::fs::File> {
    use std::io::Write;
    if let Some(d) = path.parent() {
        std::fs::create_dir_all(d).ok()?;
    }
    // The game before this one stays as engine.log.1: a crash or desync is often looked at after the next start.
    let _ = std::fs::rename(path, path.with_extension("log.1"));
    let mut f = std::fs::File::create(path).ok()?;
    let splash = o.mod_path.as_deref().and_then(|dir| splash_bitmap(Path::new(dir))).and_then(|p| p.file_name().map(|n| n.to_string_lossy().into_owned())).unwrap_or_else(|| "retail".into());
    let _ = writeln!(f, "[launcher] {}x{} fullscreen={} relay={} splash={}", o.width, o.height, o.fullscreen, o.relay.as_ref().map(|(a, v)| format!("agent {a}, virtual IP {v}")).unwrap_or_else(|| "off".into()), splash);
    let _ = writeln!(f, "[launcher] arguments: {}", args(o).join(" "));
    Some(f)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn skirmish_options_are_checked_before_they_reach_the_game() {
        let have = |n: &str| n == "Tournament Desert";
        let e = skirmish_env(Some("Tournament Desert"), Some(3), Some("brutal"), Some(2), have).unwrap();
        assert!(e.contains(&("GEN_AUTO_MAP".into(), "Tournament Desert".into())) && e.contains(&("GEN_AUTO_AI".into(), "3".into())) && e.contains(&("GEN_AUTO_DIFF".into(), "brutal".into())) && e.contains(&("GEN_AUTO_START".into(), "2".into())));
        assert!(skirmish_env(None, None, None, None, have).unwrap().is_empty());
        for bad_map in ["Missing", "../x", "a/b", ""] {
            assert!(skirmish_env(Some(bad_map), None, None, None, have).is_err(), "{bad_map:?}");
        }
        assert!(skirmish_env(None, Some(0), None, None, have).is_err() && skirmish_env(None, Some(8), None, None, have).is_err());
        assert!(skirmish_env(None, None, Some("god mode"), None, have).is_err());
        assert!(skirmish_env(None, None, None, Some(0), have).is_err() && skirmish_env(None, None, None, Some(9), have).is_err());
    }

    #[test]
    fn a_whole_skirmish_table_becomes_one_checked_setting() {
        use crate::generals::General;
        let gens = vec![General { id: "FactionAmerica".into(), name: "USA".into() }, General { id: "FactionChinaTankGeneral".into(), name: "Tank".into() }];
        let spec = |kind: &str, d: Option<&str>, f: Option<&str>, team: Option<u32>, start: Option<u32>| SlotSpec { kind: kind.into(), difficulty: d.map(String::from), faction: f.map(String::from), team, start };
        let table = vec![spec("player", None, Some("FactionAmerica"), Some(0), Some(2)), spec("ai", Some("brutal"), Some("FactionChinaTankGeneral"), Some(1), None), spec("ai", Some("easy"), None, None, Some(4)), spec("closed", None, None, None, None)];
        assert_eq!(skirmish_slots_env(&table, &gens).unwrap(), "player,FactionAmerica,0,2;ai,brutal,FactionChinaTankGeneral,1,0;ai,easy,random,-1,4;closed");
        let bad = |t: Vec<SlotSpec>| skirmish_slots_env(&t, &gens).is_err();
        assert!(bad(vec![spec("player", None, None, None, None)]), "needs an opponent");
        assert!(bad(vec![spec("ai", None, None, None, None), spec("ai", None, None, None, None)]), "you come first");
        assert!(bad(vec![spec("player", None, Some("FactionNope"), None, None), spec("ai", None, None, None, None)]), "a general the game does not have");
        assert!(bad(vec![spec("player", None, None, None, Some(1)), spec("ai", None, None, None, Some(1))]), "one start twice");
        assert!(bad(vec![spec("player", None, None, None, Some(9)), spec("ai", None, None, None, None)]));
        assert!(bad(vec![spec("player", None, None, Some(4), None), spec("ai", None, None, None, None)]));
        assert!(bad(vec![spec("player", None, None, None, None), spec("ai", Some("godlike"), None, None, None)]));
        assert!(bad(vec![spec("player", None, None, None, None), spec("zombie", None, None, None, None), spec("ai", None, None, None, None)]));
        assert!(bad((0..9).map(|i| spec(if i == 0 { "player" } else { "ai" }, None, None, None, None)).collect()), "more than eight places");
    }

    #[test]
    fn builds_args_with_mod() {
        let o = LaunchOptions { width: 3840, height: 1600, mod_path: Some("/m/Shockwave".into()), mod_args: vec!["-noshellmap".into()], ..Default::default() };
        assert_eq!(args(&o), ["-win", "-xres", "3840", "-yres", "1600", "-mod", "/m/Shockwave", "-noshellmap", "-nologo"]);
    }

    #[test]
    fn extra_env_is_passed() {
        let o = LaunchOptions { env: vec![("GEN_AUTO_SKIRMISH".into(), "1".into())], ..Default::default() };
        let c = command(Path::new("e"), Path::new("."), &o);
        assert!(c.get_envs().any(|(k, v)| k == "GEN_AUTO_SKIRMISH" && v == Some("1".as_ref())));
    }

    #[test]
    fn chosen_resolution_is_named_for_the_engine() {
        let o = LaunchOptions { width: 3440, height: 1440, fullscreen: true, ..Default::default() };
        assert_eq!(args(&o)[..5], ["-fullscreen", "-xres", "3440", "-yres", "1440"]);
        let c = command(Path::new("e"), Path::new("."), &o);
        assert!(c.get_envs().any(|(k, v)| k == "GEN_EXTRA_RES" && v == Some("3440x1440".as_ref())));
        // nonsense sizes are not advertised
        let c = command(Path::new("e"), Path::new("."), &LaunchOptions { width: 10, height: 10, ..Default::default() });
        assert!(!c.get_envs().any(|(k, _)| k == "GEN_EXTRA_RES"));
    }

    #[test]
    fn engine_output_goes_to_the_log_with_a_header() {
        let t = std::env::temp_dir().join(format!("gen-log-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        let log = t.join("sub").join("engine.log");
        let o = LaunchOptions { width: 1920, height: 1080, relay: Some(("127.0.0.1:5000".into(), "10.77.0.2".into())), log: Some(log.clone()), ..Default::default() };
        let _ = command(Path::new("e"), Path::new("."), &o);
        let text = std::fs::read_to_string(&log).unwrap();
        let mut lines = text.lines();
        assert_eq!(lines.next().unwrap(), "[launcher] 1920x1080 fullscreen=false relay=agent 127.0.0.1:5000, virtual IP 10.77.0.2 splash=retail");
        assert!(lines.next().unwrap().starts_with("[launcher] arguments: -win -xres 1920"));
        // starting again keeps the last run
        let _ = command(Path::new("e"), Path::new("."), &o);
        assert!(log.with_extension("log.1").is_file());
        let _ = std::fs::remove_dir_all(&t);
    }

    #[test]
    fn sync_logging_adds_the_engine_switches_only_when_asked() {
        let off = args(&LaunchOptions { width: 1, height: 1, ..Default::default() });
        assert!(!off.iter().any(|a| a.contains("CRC")));
        let on = args(&LaunchOptions { width: 1, height: 1, sync_logs: Some("/data/sync logs".into()), ..Default::default() });
        let i = on.iter().position(|a| a == "-SaveDebugCRCPerFrame").unwrap();
        assert_eq!(on[i + 1], "/data/sync logs");
        assert!(on.windows(2).any(|w| w[0] == "-DebugCRCFromFrame" && w[1] == "0"));
        assert!(on.iter().any(|a| a == "-LogObjectCRCs"));
        // module data would change the checksum itself, so one player with sync logs on and one with them off would see a false mismatch
        assert!(!on.iter().any(|a| a == "-CRCLogicModuleData"));
        assert!(on.windows(2).any(|w| w[0] == "-DebugCRCUntilFrame" && w[1] == "1000000"));
        assert!(on.windows(2).any(|w| w[0] == "-NetCRCInterval" && w[1] == "10"));
        assert!(on.windows(2).any(|w| w[0] == "-ReplayCRCInterval" && w[1] == "10"));
        let t = std::env::temp_dir().join(format!("gen-sync-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        let dir = t.join("sync logs");
        prepare_sync_logs(&dir).unwrap();
        assert!(dir.is_dir());
        let _ = std::fs::remove_dir_all(&t);
    }

    #[test]
    fn game_only_starts_on_a_ready_network() {
        use crate::agent::Status;
        assert_eq!(relay_for_launch(None), Ok(None));
        assert_eq!(relay_for_launch(Some(("127.0.0.1:9".into(), Status::Connected([10, 77, 0, 2])))), Ok(Some(("127.0.0.1:9".into(), "10.77.0.2".into()))));
        assert!(relay_for_launch(Some(("x".into(), Status::Connecting))).unwrap_err().contains("Still connecting"));
        assert!(relay_for_launch(Some(("x".into(), Status::Rejected(3)))).unwrap_err().contains("not connected"));
    }

    #[test]
    fn relay_sets_env() {
        let o = LaunchOptions { width: 1, height: 1, relay: Some(("127.0.0.1:5000".into(), "10.77.0.2".into())), ..Default::default() };
        let c = command(Path::new("e"), Path::new("."), &o);
        assert!(c.get_envs().any(|(k, v)| k == "GEN_RELAY" && v == Some("127.0.0.1:5000".as_ref())));
        assert!(c.get_envs().any(|(k, v)| k == "GEN_RELAY_VIP" && v == Some("10.77.0.2".as_ref())));
    }

    #[test]
    fn names_the_zero_hour_folder_and_raises_the_camera_only_when_asked() {
        let c = command(Path::new("e"), Path::new("/games/zh"), &LaunchOptions::default());
        assert!(c.get_envs().any(|(k, v)| k == "GEN_ZH_PATH" && v == Some("/games/zh".as_ref())));
        assert!(!c.get_envs().any(|(k, _)| k == "GEN_MAX_CAMERA_HEIGHT"));
        let c = command(Path::new("e"), Path::new("/games/zh"), &LaunchOptions { raised_camera: true, ..Default::default() });
        assert!(c.get_envs().any(|(k, v)| k == "GEN_MAX_CAMERA_HEIGHT" && v == Some("600".as_ref())));
        assert!(c.get_envs().any(|(k, v)| k == "GEN_CAMERA_PITCH" && v == Some("37".as_ref())));
    }

    #[test]
    fn mod_splash_is_the_mods_bitmap_and_shockwaves_name_when_that_is_what_it_ships() {
        let t = std::env::temp_dir().join(format!("gen-splash-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        std::fs::create_dir_all(&t).unwrap();
        assert!(splash_bitmap(&t).is_none(), "no picture means the engine keeps Zero Hour's");
        std::fs::write(t.join("Install_Final_shw.bmp"), b"BM").unwrap();
        assert_eq!(splash_bitmap(&t).unwrap().file_name().unwrap(), "Install_Final_shw.bmp");
        std::fs::write(t.join("Install_Final.bmp"), b"BM").unwrap();
        assert_eq!(splash_bitmap(&t).unwrap().file_name().unwrap(), "Install_Final.bmp");
        let o = LaunchOptions { mod_path: Some(t.to_string_lossy().into()), ..Default::default() };
        let c = command(Path::new("e"), Path::new("."), &o);
        let expected = t.join("Install_Final.bmp");
        assert!(c.get_envs().any(|(k, v)| k == "GEN_SPLASH" && v == Some(expected.as_os_str())));
        let plain = command(Path::new("e"), Path::new("."), &LaunchOptions::default());
        assert!(!plain.get_envs().any(|(k, _)| k == "GEN_SPLASH"));
        let _ = std::fs::remove_dir_all(&t);
    }

    #[test]
    fn game_art_prefers_the_mods_picture_and_refuses_anything_but_a_bmp() {
        let t = std::env::temp_dir().join(format!("gen-art-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        let (game, modd) = (t.join("game"), t.join("mod"));
        std::fs::create_dir_all(&game).unwrap();
        std::fs::create_dir_all(&modd).unwrap();
        assert!(game_art(&game, Some(&modd)).is_none());
        std::fs::write(game.join("Install_Final.bmp"), [b"BM".as_slice(), &[1u8; 100]].concat()).unwrap();
        assert_eq!(game_art(&game, None).unwrap()[2], 1);
        std::fs::write(modd.join("Install_Final_shw.bmp"), [b"BM".as_slice(), &[2u8; 100]].concat()).unwrap();
        assert_eq!(game_art(&game, Some(&modd)).unwrap()[2], 2, "the mod's picture wins");
        std::fs::write(modd.join("Install_Final_shw.bmp"), [b"PNG".as_slice(), &[2u8; 100]].concat()).unwrap();
        assert_eq!(game_art(&game, Some(&modd)).unwrap()[2], 1, "not a BMP: fall back to the game's");
        let _ = std::fs::remove_dir_all(t);
    }
}
