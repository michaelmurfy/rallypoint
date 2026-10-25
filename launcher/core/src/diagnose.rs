//! "Why does the game show 'You have encountered a serious error'?" The engine shows that box for many
//! reasons; the common ones we can check without running it: missing game files, a missing base game
//! (Zero Hour reads Generals' data too) and the crash report the engine leaves behind.
use crate::maps::USER_DATA_LEAF;
use std::path::Path;

#[derive(Debug, Clone, serde::Serialize)]
pub struct Check {
    pub name: String,
    pub ok: bool,
    pub detail: String,
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct Report {
    pub checks: Vec<Check>,
    /// Last lines of `ReleaseCrashInfo.txt`, if the engine wrote one.
    pub crash: Option<String>,
    /// Folder holding the crash report and logs.
    pub data_dir: String,
}

const ZH_FILES: [&str; 5] = ["INIZH.big", "W3DZH.big", "TexturesZH.big", "TerrainZH.big", "AudioZH.big"];
const BASE_FILES: [&str; 5] = ["INI.big", "W3D.big", "Textures.big", "Terrain.big", "Audio.big"];

fn missing(dir: &Path, files: &[&str]) -> Vec<String> {
    files.iter().filter(|f| !dir.join(f).is_file()).map(|f| f.to_string()).collect()
}

fn check(name: &str, miss: Vec<String>, ok_text: &str, fix: &str) -> Check {
    if miss.is_empty() {
        Check { name: name.into(), ok: true, detail: ok_text.into() }
    } else {
        Check { name: name.into(), ok: false, detail: format!("Missing {}. {fix}", miss.join(", ")) }
    }
}

/// `documents` is the user's Documents folder, where the engine keeps its crash report.
pub fn run(game_dir: &Path, base: Option<&Path>, engine: Option<&Path>, documents: &Path) -> Report {
    let mut checks = vec![match engine {
        Some(p) => Check { name: "Game engine".into(), ok: true, detail: p.display().to_string() },
        None => Check { name: "Game engine".into(), ok: false, detail: "Not found. Reinstall Rallypoint.".into() },
    }];
    checks.push(check("Zero Hour files", missing(game_dir, &ZH_FILES), &game_dir.display().to_string(), "Verify the game files in Steam, or download them again from Settings."));
    checks.push(match base {
        Some(b) => check("Generals (base game)", missing(b, &BASE_FILES), &b.display().to_string(), "Verify Generals in Steam, or download it again from Settings."),
        None => Check {
            name: "Generals (base game)".into(),
            ok: false,
            detail: "Not found. Zero Hour also needs the original Generals installed (Steam: Command & Conquer Generals) or downloaded from Settings.".into(),
        },
    });
    let data = documents.join(USER_DATA_LEAF);
    let crash = std::fs::read_to_string(data.join("ReleaseCrashInfo.txt")).ok().map(|t| {
        let lines: Vec<&str> = t.lines().collect();
        lines[lines.len().saturating_sub(40)..].join("\n")
    });
    Report { checks, crash, data_dir: data.display().to_string() }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn touch(dir: &Path, names: &[&str]) {
        std::fs::create_dir_all(dir).unwrap();
        for n in names {
            std::fs::write(dir.join(n), b"BIGF").unwrap();
        }
    }

    #[test]
    fn reports_missing_base_game_and_files() {
        let t = std::env::temp_dir().join(format!("diag-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        touch(&t.join("zh"), &["INIZH.big", "W3DZH.big"]);
        let r = run(&t.join("zh"), None, Some(Path::new("engine.exe")), &t.join("docs"));
        assert!(r.checks[0].ok);
        assert!(!r.checks[1].ok && r.checks[1].detail.contains("TexturesZH.big") && !r.checks[1].detail.contains("INIZH.big"));
        assert!(!r.checks[2].ok && r.checks[2].detail.contains("original Generals"));
        assert!(r.crash.is_none());
        let _ = std::fs::remove_dir_all(&t);
    }

    #[test]
    fn all_present_passes_and_crash_report_is_tailed() {
        let t = std::env::temp_dir().join(format!("diag2-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        touch(&t.join("zh"), &ZH_FILES);
        touch(&t.join("base"), &BASE_FILES);
        let data = t.join("docs").join(USER_DATA_LEAF);
        std::fs::create_dir_all(&data).unwrap();
        let log: String = (0..100).map(|i| format!("line {i}\n")).collect();
        std::fs::write(data.join("ReleaseCrashInfo.txt"), log).unwrap();
        let r = run(&t.join("zh"), Some(&t.join("base")), Some(Path::new("e")), &t.join("docs"));
        assert!(r.checks.iter().all(|c| c.ok));
        let c = r.crash.unwrap();
        assert!(c.starts_with("line 60") && c.ends_with("line 99"));
        let _ = std::fs::remove_dir_all(&t);
    }
}
