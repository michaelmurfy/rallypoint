//! Red Alert 2 / Yuri's Revenge: find the player's own copy. Rallypoint's RA2 engine reads its data from there, and it
//! needs Yuri's Revenge (`ra2.mix`, `ra2md.mix`, `language.mix`, `multimd.mix`); nothing of EA's is shipped.
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

pub const STEAM_APP: u32 = crate::assets::RA2_APP;

/// The archives the engine loads. `ra2md.mix` and `multimd.mix` are Yuri's Revenge's.
pub const DATA_FILES: [&str; 4] = ["ra2.mix", "ra2md.mix", "language.mix", "multimd.mix"];

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Ra2Install {
    pub dir: PathBuf,
}

/// A file in `dir` whatever the case of its name (Steam on Linux keeps `RA2MD.MIX` as it was shipped).
fn has_file(dir: &Path, name: &str) -> bool {
    resolve_file(dir, name).is_some()
}

/// The path of `name` in `dir` whatever the case of its spelling, or `None` if it is missing.
pub fn resolve_file(dir: &Path, name: &str) -> Option<PathBuf> {
    std::fs::read_dir(dir).ok()?.flatten().map(|e| e.path()).find(|p| p.is_file() && p.file_name().is_some_and(|n| n.to_string_lossy().eq_ignore_ascii_case(name)))
}

/// `<app data>/GeneralsZH/ra2/maps-cache`: plain INI maps written by the engine's `--extract-maps`.
pub fn maps_cache_dir(app_data: &Path) -> PathBuf {
    app_data.join("ra2").join("maps-cache")
}

/// A folder with all of `DATA_FILES`.
pub fn check_dir(path: &Path) -> Result<Ra2Install, String> {
    let missing: Vec<&str> = DATA_FILES.iter().copied().filter(|f| !has_file(path, f)).collect();
    match missing.as_slice() {
        [] => Ok(Ra2Install { dir: path.to_path_buf() }),
        m if m.len() < DATA_FILES.len() && has_file(path, "ra2.mix") => Err(format!("That Red Alert 2 has no Yuri's Revenge ({} missing)", m.join(", "))),
        _ => Err("That folder has no Red Alert 2 and Yuri's Revenge (ra2md.mix)".into()),
    }
}

fn subdirs(p: &Path) -> Vec<PathBuf> {
    let mut v: Vec<PathBuf> = std::fs::read_dir(p).map(|r| r.flatten().map(|e| e.path()).filter(|p| p.is_dir()).collect()).unwrap_or_default();
    v.sort();
    v
}

/// `dir` itself or the first folder one level below it that is an install.
pub fn find_under(dir: &Path) -> Option<Ra2Install> {
    std::iter::once(dir.to_path_buf()).chain(subdirs(dir)).find_map(|d| check_dir(&d).ok())
}

/// Installs in Steam libraries and the usual EA app / Origin folders (also inside the default Wine prefix).
pub fn detect() -> Vec<Ra2Install> {
    let mut roots: Vec<PathBuf> = vec![];
    for s in crate::steam::steam_roots() {
        for lib in crate::steam::library_dirs(&s) {
            roots.extend(subdirs(&lib.join("steamapps/common")));
        }
    }
    let mut program_files: Vec<PathBuf> = ["ProgramFiles", "ProgramFiles(x86)"].iter().filter_map(std::env::var_os).map(PathBuf::from).collect();
    if let Some(h) = std::env::var_os("HOME").filter(|_| !cfg!(windows)) {
        let c = PathBuf::from(h).join(".wine/drive_c");
        program_files.extend([c.join("Program Files"), c.join("Program Files (x86)")]);
    }
    for pf in program_files {
        for vendor in ["EA Games", "Origin Games"] {
            roots.push(pf.join(vendor).join("Command and Conquer Red Alert II"));
            roots.push(pf.join(vendor).join("Command & Conquer The Ultimate Collection").join("Command and Conquer Red Alert II"));
        }
    }
    let mut out: Vec<Ra2Install> = vec![];
    for i in roots.iter().filter_map(|r| find_under(r)) {
        if !out.contains(&i) {
            out.push(i);
        }
    }
    out
}

/// The player's Red Alert 2 settings, kept in the launcher config.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Ra2Config {
    /// Empty: whatever `detect` (or the Steam download) finds.
    pub game_dir: String,
    /// 0: the screen's own size.
    pub width: u32,
    pub height: u32,
    pub windowed: bool,
    /// Country 0..9 (America .. Russia, then Yuri: `ra2launch::SIDES`), -1 random.
    pub side: i32,
    /// Colour 0..7, -1 any free one.
    pub color: i32,
    /// The Red Alert 2 first-run wizard has been completed (or skipped). Separate from Generals' `setup_done`.
    #[serde(default)]
    pub setup_done: bool,
}

impl Default for Ra2Config {
    fn default() -> Self {
        Ra2Config { game_dir: String::new(), width: 0, height: 0, windowed: false, side: -1, color: -1, setup_done: false }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn touch(p: &Path) {
        std::fs::create_dir_all(p.parent().unwrap()).unwrap();
        std::fs::write(p, "x").unwrap();
    }

    #[test]
    fn needs_yuris_revenge_data_whatever_the_case_of_the_names() {
        let t = std::env::temp_dir().join(format!("gen-ra2-detect-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        std::fs::create_dir_all(&t).unwrap();
        assert!(check_dir(&t).unwrap_err().contains("no Red Alert 2"));
        touch(&t.join("RA2/RA2.MIX"));
        touch(&t.join("RA2/Language.mix"));
        assert!(check_dir(&t.join("RA2")).unwrap_err().contains("no Yuri's Revenge"), "Red Alert 2 alone is not enough");
        assert_eq!(find_under(&t), None);
        for f in ["RA2.MIX", "RA2MD.MIX", "LANGUAGE.MIX", "multimd.mix"] {
            touch(&t.join("YR").join(f));
        }
        assert_eq!(check_dir(&t.join("YR")).unwrap(), Ra2Install { dir: t.join("YR") });
        assert_eq!(find_under(&t), Some(Ra2Install { dir: t.join("YR") }), "found one level down");
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn settings_fill_in_what_an_older_file_lacks() {
        let c: Ra2Config = serde_json::from_str(r#"{"windowed":true,"renderer":"gdi"}"#).unwrap();
        assert_eq!(c, Ra2Config { windowed: true, ..Default::default() });
        assert_eq!((c.side, c.color, c.setup_done), (-1, -1, false));
    }
}
