//! The debug bundle: one zip with everything needed to work out why a game crashed or went out of sync. The player
//! sends it; nothing leaves the machine by itself. Passwords, serials, tokens, server keys and the Steam account name are
//! never put in it, the user's home folder is replaced by `<home>` in every text file, and the last replay (it can hold
//! in-game chat) is only added when the player ticks it.
use crate::proc::quiet;
use std::path::{Path, PathBuf};
use std::process::Command;

pub struct Sources {
    /// `<app data>/GeneralsZH`: engine.log, sync-logs, config.json, Mods.
    pub base: PathBuf,
    /// `<Documents>/Command and Conquer Generals Zero Hour Data`: Options.ini, crash report, replays.
    pub game_data: PathBuf,
    /// The game folder with the `.big` files.
    pub game_dir: Option<PathBuf>,
    pub version: String,
    /// Extra sections for system.txt (title, text), e.g. the relay connection state.
    pub extra: Vec<(String, String)>,
    /// Add `LastReplay.rep`. Off unless the player asks: a replay can contain the chat of the game.
    pub include_replay: bool,
    /// The user's home folder, which is removed from the text files (it has their user name in it).
    pub home: Option<PathBuf>,
    /// The folder of the game engine's program: on Windows the engine writes its own debug log (`DebugLogFile.txt`, with the
    /// pathfinding, network and AI lines the macOS build prints to `engine.log`) next to itself.
    pub engine_dir: Option<PathBuf>,
}

const MAX_COPY: u64 = 200 * 1024 * 1024;

/// The config without anything secret: every top-level entry whose name mentions a password, serial, token or key.
pub fn scrub_config(text: &str) -> String {
    let Ok(serde_json::Value::Object(mut m)) = serde_json::from_str::<serde_json::Value>(text) else { return "(config could not be read)".into() };
    // secrets, and who the player is on Steam and in the lobby
    m.retain(|k, _| !["password", "serial", "token", "key", "steam", "player_name"].iter().any(|w| k.to_lowercase().contains(w)));
    serde_json::to_string_pretty(&serde_json::Value::Object(m)).unwrap_or_default()
}

/// `text` with the user's home folder replaced by `<home>`, in the spellings it turns up in (either slash, doubled
/// backslashes in JSON).
pub fn redact_home(text: &str, home: &Path) -> String {
    let h = home.to_string_lossy();
    let h = h.trim_end_matches(['/', '\\']);
    if h.len() < 4 {
        return text.to_string();
    }
    let forward = h.replace('\\', "/");
    let back = h.replace('/', "\\");
    let doubled = back.replace('\\', "\\\\");
    let mut out = text.to_string();
    for v in [doubled, back, forward, h.to_string()] {
        out = out.replace(&v, "<home>");
    }
    out
}

/// Replace the home folder in a text file that was just copied.
fn redact_file(path: &Path, home: &Path) {
    if let Ok(t) = std::fs::read(path) {
        let text = String::from_utf8_lossy(&t);
        let r = redact_home(&text, home);
        if r != text {
            let _ = std::fs::write(path, r);
        }
    }
}

fn copy_file(from: &Path, to: &Path) {
    if from.metadata().map(|m| m.len() <= MAX_COPY).unwrap_or(false) {
        if let Some(d) = to.parent() {
            let _ = std::fs::create_dir_all(d);
        }
        let _ = std::fs::copy(from, to);
    }
}

fn copy_tree(from: &Path, to: &Path) {
    for e in std::fs::read_dir(from).into_iter().flatten().flatten() {
        let p = e.path();
        match e.file_type() {
            Ok(t) if t.is_dir() => copy_tree(&p, &to.join(e.file_name())),
            Ok(t) if t.is_file() => copy_file(&p, &to.join(e.file_name())),
            _ => {}
        }
    }
}

/// Newest files of a folder whose name starts with `prefix` (case-insensitive), at most `n`.
fn newest(dir: &Path, prefix: &str, n: usize) -> Vec<PathBuf> {
    let mut v: Vec<_> = std::fs::read_dir(dir)
        .into_iter()
        .flatten()
        .flatten()
        .filter(|e| e.file_name().to_string_lossy().to_lowercase().starts_with(prefix))
        .filter_map(|e| Some((e.metadata().ok()?.modified().ok()?, e.path())))
        .collect();
    v.sort_by(|a, b| b.0.cmp(&a.0));
    v.into_iter().take(n).map(|(_, p)| p).collect()
}

fn listing(dir: &Path, depth: usize) -> String {
    let mut out = String::new();
    let mut entries: Vec<_> = std::fs::read_dir(dir).into_iter().flatten().flatten().collect();
    entries.sort_by_key(|e| e.file_name());
    for e in entries {
        let name = e.file_name().to_string_lossy().into_owned();
        match e.file_type() {
            Ok(t) if t.is_dir() && depth > 0 => out.push_str(&format!("{name}/\n{}", listing(&e.path(), depth - 1).lines().map(|l| format!("  {l}\n")).collect::<String>())),
            Ok(t) if t.is_dir() => out.push_str(&format!("{name}/\n")),
            _ => out.push_str(&format!("{name}  {} bytes\n", e.metadata().map(|m| m.len()).unwrap_or(0))),
        }
    }
    out
}

/// Seconds since 1970 as `YYYY-MM-DD-HHMMSS` (UTC), for the file name.
pub fn stamp(secs: u64) -> String {
    let (days, rem) = ((secs / 86400) as i64, secs % 86400);
    let z = days + 719468;
    let era = z.div_euclid(146097);
    let doe = z.rem_euclid(146097);
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = yoe + era * 400 + if m <= 2 { 1 } else { 0 };
    format!("{y:04}-{m:02}-{d:02}-{:02}{:02}{:02}", rem / 3600, rem % 3600 / 60, rem % 60)
}

/// Copy everything into `stage` (emptied first).
pub fn stage(s: &Sources, stage: &Path, now: u64) -> std::io::Result<()> {
    let _ = std::fs::remove_dir_all(stage);
    std::fs::create_dir_all(stage)?;
    for f in ["engine.log", "engine.log.1"] {
        copy_file(&s.base.join(f), &stage.join(f));
    }
    copy_tree(&s.base.join("sync-logs"), &stage.join("sync-logs"));
    if let Ok(t) = std::fs::read_to_string(s.base.join("config.json")) {
        std::fs::write(stage.join("config.json"), scrub_config(&t))?;
    }
    // The game's own files: settings, crash report, and the last replay (which can be replayed to see where it went wrong).
    copy_file(&s.game_data.join("Options.ini"), &stage.join("game").join("Options.ini"));
    for p in newest(&s.game_data, "releasecrashinfo", 3) {
        copy_file(&p, &stage.join("game").join(p.file_name().unwrap()));
    }
    if s.include_replay {
        copy_file(&s.game_data.join("Replays").join("LastReplay.rep"), &stage.join("game").join("LastReplay.rep"));
    }
    if let Some(dir) = &s.engine_dir {
        for f in ["DebugLogFile.txt", "DebugLogFilePrev.txt"] {
            copy_file(&dir.join(f), &stage.join("engine-debug").join(f));
        }
    }
    // macOS leaves a crash report of its own for each crash of the engine.
    if let Some(home) = std::env::var_os("HOME") {
        for p in newest(&PathBuf::from(home).join("Library/Logs/DiagnosticReports"), "generalszh", 3) {
            copy_file(&p, &stage.join("crash-reports").join(p.file_name().unwrap()));
        }
    }
    let mut sys = format!("Rallypoint {}\nOS {} {}\nbundle made at unix time {now} ({} UTC)\n", s.version, std::env::consts::OS, std::env::consts::ARCH, stamp(now));
    if let Some(g) = &s.game_dir {
        sys.push_str(&format!("\n== game folder {} ==\n{}", g.display(), listing(g, 0).lines().filter(|l| l.contains(".big") || l.contains(".gib") || l.ends_with('/')).map(|l| format!("{l}\n")).collect::<String>()));
    }
    sys.push_str(&format!("\n== mods ==\n{}", listing(&s.base.join("Mods"), 1)));
    sys.push_str(&format!("\n== maps ==\n{}", listing(&s.game_data.join("Maps"), 0).lines().count()));
    for (title, text) in &s.extra {
        sys.push_str(&format!("\n== {title} ==\n{text}\n"));
    }
    std::fs::write(stage.join("system.txt"), sys)?;
    // the text files that can hold paths (a crash report names the folders the game ran from)
    if let Some(home) = &s.home {
        for f in ["engine.log", "engine.log.1", "config.json", "system.txt"] {
            redact_file(&stage.join(f), home);
        }
        for dir in ["game", "crash-reports", "engine-debug"] {
            for e in std::fs::read_dir(stage.join(dir)).into_iter().flatten().flatten() {
                if !matches!(e.path().extension(), Some(x) if x == "rep") {
                    redact_file(&e.path(), home);
                }
            }
        }
    }
    Ok(())
}

/// Zip `stage` into `out`. macOS and Windows ship a `tar` that writes zips; elsewhere `zip` is used.
fn archive(stage: &Path, out: &Path) -> Result<(), String> {
    let _ = std::fs::remove_file(out);
    let tar = quiet(Command::new("tar").args(["-a", "-c", "-f"]).arg(out).arg("-C").arg(stage).arg(".")).output();
    if tar.as_ref().map(|o| o.status.success()).unwrap_or(false) && out.is_file() {
        return Ok(());
    }
    let _ = std::fs::remove_file(out);
    let zip = quiet(Command::new("zip").args(["-r", "-q"]).arg(out).arg(".").current_dir(stage)).output().map_err(|e| format!("no tool to make a zip: {e}"))?;
    if zip.status.success() { Ok(()) } else { Err(String::from_utf8_lossy(&zip.stderr).trim().to_string()) }
}

/// Make the bundle in `out_dir` and return its path.
pub fn create(s: &Sources, out_dir: &Path) -> Result<PathBuf, String> {
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
    let work = s.base.join("tmp").join("debug-bundle");
    stage(s, &work, now).map_err(|e| e.to_string())?;
    std::fs::create_dir_all(out_dir).map_err(|e| e.to_string())?;
    let out = out_dir.join(format!("rallypoint-debug-{}.zip", stamp(now)));
    let r = archive(&work, &out);
    let _ = std::fs::remove_dir_all(&work);
    r.map(|_| out)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("gen-bundle-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn config_keeps_settings_and_drops_secrets() {
        let t = scrub_config(r#"{"server":"s:1","camera_height":510,"room_passwords":{"a|b":"hunter2"},"serials":{"s":"X"},"server_keys":{"s":"K"},"steam_user":"me"}"#);
        assert!(t.contains("camera_height") && t.contains("server"));
        assert!(!t.contains("hunter2") && !t.contains("serials") && !t.contains("server_keys"));
        assert!(scrub_config("not json").contains("could not"));
        // who the player is on Steam and in the lobby stays out too
        let t = scrub_config(r#"{"server":"s:1","steam_user":"ann_steam","player_name":"Ann S"}"#);
        assert!(!t.contains("ann_steam") && !t.contains("Ann S") && t.contains("server"));
    }

    #[test]
    fn the_home_folder_is_removed_in_every_spelling() {
        let home = Path::new("C:\\Users\\Ann Smith");
        let t = redact_home("-mod C:\\Users\\Ann Smith\\AppData\\x, C:/Users/Ann Smith/Documents, \"C:\\\\Users\\\\Ann Smith\\\\g\"", home);
        assert!(!t.contains("Ann Smith"), "{t}");
        assert_eq!(t.matches("<home>").count(), 3);
        assert_eq!(redact_home("/Users/ann/x", Path::new("/Users/ann")), "<home>/x");
        // nothing sensible to replace: leave the text alone
        assert_eq!(redact_home("keep /x", Path::new("/")), "keep /x");
    }

    #[test]
    fn file_name_stamp_is_a_utc_date() {
        assert_eq!(stamp(0), "1970-01-01-000000");
        assert_eq!(stamp(1_788_000_000), "2026-08-29-104000");
        assert_eq!(stamp(951_868_799), "2000-02-29-235959");
    }

    #[test]
    fn everything_useful_is_staged_and_nothing_secret() {
        let t = temp("stage");
        let (base, data) = (t.join("app"), t.join("docs"));
        std::fs::create_dir_all(base.join("sync-logs")).unwrap();
        std::fs::create_dir_all(data.join("Replays")).unwrap();
        std::fs::create_dir_all(base.join("Mods/Shockwave")).unwrap();
        std::fs::write(base.join("engine.log"), "[net] relay on\n-mod /Users/ann smith/Library/GeneralsZH/Mods/X").unwrap();
        std::fs::write(base.join("sync-logs/DebugFrame_000000.txt"), "0:00000 line").unwrap();
        std::fs::write(base.join("config.json"), r#"{"server":"s","room_passwords":{"x":"secret"}}"#).unwrap();
        std::fs::write(data.join("Options.ini"), "Resolution = 1280 720").unwrap();
        std::fs::write(data.join("ReleaseCrashInfo.txt"), "crash").unwrap();
        std::fs::write(data.join("Replays/LastReplay.rep"), "rep").unwrap();
        std::fs::create_dir_all(t.join("engine")).unwrap();
        std::fs::write(t.join("engine/DebugLogFile.txt"), "path failed in /Users/ann smith/x").unwrap();
        let s = Sources { base: base.clone(), game_data: data, game_dir: None, version: "9.9.9".into(), extra: vec![("relay".into(), "connected".into())], include_replay: false, home: Some(PathBuf::from("/Users/ann smith")), engine_dir: Some(t.join("engine")) };
        let out = t.join("stage");
        stage(&s, &out, 0).unwrap();
        for f in ["engine.log", "sync-logs/DebugFrame_000000.txt", "config.json", "game/Options.ini", "game/ReleaseCrashInfo.txt", "system.txt"] {
            assert!(out.join(f).is_file(), "{f}");
        }
        let sys = std::fs::read_to_string(out.join("system.txt")).unwrap();
        assert!(sys.contains("Rallypoint 9.9.9") && sys.contains("Shockwave/") && sys.contains("== relay ==\nconnected"));
        assert!(!std::fs::read_to_string(out.join("config.json")).unwrap().contains("secret"));
        let log = std::fs::read_to_string(out.join("engine.log")).unwrap();
        assert!(log.contains("-mod <home>/Library") && !log.contains("ann smith"), "{log}");
        assert!(!out.join("game/LastReplay.rep").exists(), "the replay is only added when asked for");
        let dbg = std::fs::read_to_string(out.join("engine-debug/DebugLogFile.txt")).unwrap();
        assert!(dbg.contains("<home>/x") && !dbg.contains("ann smith"), "{dbg}");
        let with = Sources { include_replay: true, ..Sources { base: base.clone(), game_data: s.game_data.clone(), game_dir: None, version: "9.9.9".into(), extra: vec![], include_replay: true, home: None, engine_dir: None } };
        let out2 = t.join("stage2");
        stage(&with, &out2, 0).unwrap();
        assert!(out2.join("game/LastReplay.rep").is_file());
        // The zip itself needs the system's tar or zip, which a bare build machine may lack.
        if let Ok(z) = create(&s, &t.join("out")) {
            assert!(z.metadata().unwrap().len() > 100 && z.to_string_lossy().ends_with(".zip"));
            assert!(!base.join("tmp/debug-bundle").exists());
        }
        let _ = std::fs::remove_dir_all(t);
    }
}
