//! The map organiser. The game lists every folder in its `Maps` folder, so a pack with hundreds of maps buries the
//! few you want. Maps the player has switched off are moved to a library folder beside Rallypoint's settings and moved
//! back when switched on again: nothing is deleted and nothing is held twice. `library.json` remembers which pack each
//! map came from.
use crate::maps::{find_maps, is_map_file};
use serde::Serialize;
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

/// Packs shown for maps nothing is known about (added by hand, or before the organiser).
pub const OTHER: &str = "Other maps";

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct MapEntry {
    pub name: String,
    pub pack: String,
    pub enabled: bool,
    pub size_kb: u64,
}

fn disabled_dir(lib: &Path) -> PathBuf {
    lib.join("disabled")
}

fn meta_path(lib: &Path) -> PathBuf {
    lib.join("library.json")
}

fn load_meta(lib: &Path) -> BTreeMap<String, String> {
    std::fs::read_to_string(meta_path(lib)).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
}

fn save_meta(lib: &Path, meta: &BTreeMap<String, String>) -> Result<(), String> {
    std::fs::create_dir_all(lib).map_err(|e| e.to_string())?;
    std::fs::write(meta_path(lib), serde_json::to_vec_pretty(meta).map_err(|e| e.to_string())?).map_err(|e| e.to_string())
}

fn dir_size(p: &Path) -> u64 {
    std::fs::read_dir(p).into_iter().flatten().flatten().map(|e| e.metadata().map(|m| if m.is_dir() { dir_size(&e.path()) } else { m.len() }).unwrap_or(0)).sum()
}

fn is_map_dir(d: &Path) -> bool {
    std::fs::read_dir(d).into_iter().flatten().flatten().any(|e| is_map_file(&e.path()))
}

fn map_dirs(root: &Path) -> Vec<(String, PathBuf)> {
    let mut v: Vec<(String, PathBuf)> = std::fs::read_dir(root)
        .into_iter()
        .flatten()
        .flatten()
        .filter(|e| e.file_type().map(|t| t.is_dir()).unwrap_or(false) && is_map_dir(&e.path()))
        .map(|e| (e.file_name().to_string_lossy().into_owned(), e.path()))
        .collect();
    v.sort_by_key(|(n, _)| n.to_lowercase());
    v
}

/// Every map the player has: the ones the game sees (`maps_dir`) and the ones switched off (in the library).
pub fn list(maps_dir: &Path, lib: &Path) -> Vec<MapEntry> {
    let meta = load_meta(lib);
    let pack = |n: &str| meta.get(n).cloned().unwrap_or_else(|| OTHER.to_string());
    let mut out: Vec<MapEntry> = map_dirs(maps_dir).into_iter().map(|(n, p)| MapEntry { pack: pack(&n), size_kb: dir_size(&p) / 1024, name: n, enabled: true }).collect();
    for (n, p) in map_dirs(&disabled_dir(lib)) {
        if !out.iter().any(|e| e.name == n) {
            out.push(MapEntry { pack: pack(&n), size_kb: dir_size(&p) / 1024, name: n, enabled: false });
        }
    }
    out.sort_by_key(|e| e.name.to_lowercase());
    out
}

fn copy_tree(src: &Path, dst: &Path) -> std::io::Result<()> {
    std::fs::create_dir_all(dst)?;
    for e in std::fs::read_dir(src)? {
        let e = e?;
        let ty = e.file_type()?;
        let to = dst.join(e.file_name());
        if ty.is_dir() {
            copy_tree(&e.path(), &to)?;
        } else if ty.is_file() {
            std::fs::copy(e.path(), to)?;
        }
    }
    Ok(())
}

/// Move a folder; across disks (where a rename fails) it is copied and then removed.
fn move_dir(from: &Path, to: &Path) -> Result<(), String> {
    if let Some(parent) = to.parent() {
        std::fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    let _ = std::fs::remove_dir_all(to);
    if std::fs::rename(from, to).is_ok() {
        return Ok(());
    }
    copy_tree(from, to).map_err(|e| format!("couldn't move {}: {e}", from.display()))?;
    std::fs::remove_dir_all(from).map_err(|e| e.to_string())
}

/// A folder name that is one plain path component on every system. Maps are named by their authors (`! 1v1 ! no money`,
/// `Black Hell (v3)`), so unlike a mod folder these may start with punctuation and contain spaces and brackets; what is
/// refused is anything that could leave the folder or that Windows cannot hold.
pub fn is_safe_name(name: &str) -> bool {
    !name.is_empty()
        && name.len() <= 120
        && name != "."
        && name != ".."
        && !name.ends_with(['.', ' '])
        && !name.chars().any(|c| c.is_control() || "/\\:*?\"<>|".contains(c))
}

fn safe(name: &str) -> Result<&str, String> {
    if is_safe_name(name) { Ok(name) } else { Err(format!("{name:?} isn't a map folder name")) }
}

/// Switch maps on (move into the game's folder) or off (move into the library). Returns how many were moved.
pub fn set_enabled(maps_dir: &Path, lib: &Path, names: &[String], enabled: bool) -> Result<usize, String> {
    let (from, to) = if enabled { (disabled_dir(lib), maps_dir.to_path_buf()) } else { (maps_dir.to_path_buf(), disabled_dir(lib)) };
    let mut moved = 0;
    for n in names {
        let n = safe(n)?;
        let src = from.join(n);
        if src.is_dir() && is_map_dir(&src) {
            move_dir(&src, &to.join(n))?;
            moved += 1;
        }
    }
    Ok(moved)
}

/// Remove maps for good, switched on or off.
pub fn delete(maps_dir: &Path, lib: &Path, names: &[String]) -> Result<usize, String> {
    let mut meta = load_meta(lib);
    let mut gone = 0;
    for n in names {
        let n = safe(n)?;
        for root in [maps_dir.to_path_buf(), disabled_dir(lib)] {
            let d = root.join(n);
            if d.is_dir() && is_map_dir(&d) {
                std::fs::remove_dir_all(&d).map_err(|e| e.to_string())?;
                gone += 1;
            }
        }
        meta.remove(n);
    }
    save_meta(lib, &meta)?;
    Ok(gone)
}

/// Put freshly unpacked maps in place and note their pack. A map the player already has is replaced where it is (on or
/// off); a new one starts switched on or off as `start_enabled` says. Returns (added, skipped for an unusable name).
pub fn add(found: &[(String, PathBuf)], maps_dir: &Path, lib: &Path, pack: &str, start_enabled: bool) -> Result<(usize, usize), String> {
    let mut meta = load_meta(lib);
    let (mut done, mut skipped) = (0, 0);
    for (name, dir) in found {
        if !is_safe_name(name) {
            skipped += 1;
            continue;
        }
        let n = name.clone();
        let on_now = maps_dir.join(&n).is_dir();
        let off_now = disabled_dir(lib).join(&n).is_dir();
        let enabled = if on_now || off_now { on_now } else { start_enabled };
        let dest = if enabled { maps_dir.join(&n) } else { disabled_dir(lib).join(&n) };
        for stale in [maps_dir.join(&n), disabled_dir(lib).join(&n)] {
            let _ = std::fs::remove_dir_all(stale);
        }
        std::fs::create_dir_all(dest.parent().unwrap()).map_err(|e| e.to_string())?;
        copy_tree(dir, &dest).map_err(|e| format!("couldn't copy {n}: {e}"))?;
        meta.insert(n, pack.to_string());
        done += 1;
    }
    save_meta(lib, &meta)?;
    Ok((done, skipped))
}

/// Map folders somewhere under `root`, for callers that already unpacked an archive.
pub fn found_in(root: &Path) -> Vec<(String, PathBuf)> {
    find_maps(root, 6)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("gen-maporg-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    fn map(root: &Path, name: &str) {
        let d = root.join(name);
        std::fs::create_dir_all(&d).unwrap();
        std::fs::write(d.join(format!("{name}.map")), "m").unwrap();
        std::fs::write(d.join(format!("{name}.tga")), "t").unwrap();
    }

    #[test]
    fn switching_a_map_off_and_on_moves_the_folder_and_keeps_everything() {
        let t = temp("toggle");
        let (maps, lib) = (t.join("Maps"), t.join("lib"));
        map(&maps, "Alpha");
        map(&maps, "Beta");
        assert_eq!(list(&maps, &lib).iter().map(|e| (e.name.as_str(), e.enabled, e.pack.as_str())).collect::<Vec<_>>(), [("Alpha", true, OTHER), ("Beta", true, OTHER)]);
        assert_eq!(set_enabled(&maps, &lib, &["Alpha".into(), "Missing".into()], false).unwrap(), 1);
        assert!(!maps.join("Alpha").exists() && lib.join("disabled/Alpha/Alpha.tga").is_file());
        let l = list(&maps, &lib);
        assert_eq!((l[0].name.as_str(), l[0].enabled, l[1].enabled), ("Alpha", false, true));
        assert_eq!(set_enabled(&maps, &lib, &["Alpha".into()], true).unwrap(), 1);
        assert!(maps.join("Alpha/Alpha.map").is_file() && !lib.join("disabled/Alpha").exists());
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn a_pack_can_start_switched_off_and_remembers_where_it_came_from() {
        let t = temp("add");
        let (maps, lib, src) = (t.join("Maps"), t.join("lib"), t.join("src"));
        map(&src, "One");
        map(&src, "Two");
        map(&maps, "Two"); // already playing this one: it stays on
        let found = vec![("One".to_string(), src.join("One")), ("Two".to_string(), src.join("Two")), ("../evil".to_string(), src.join("One"))];
        assert_eq!(add(&found, &maps, &lib, "Showdown", false).unwrap(), (2, 1));
        let l = list(&maps, &lib);
        assert_eq!(l.iter().map(|e| (e.name.as_str(), e.enabled, e.pack.as_str())).collect::<Vec<_>>(), [("One", false, "Showdown"), ("Two", true, "Showdown")]);
        assert!(!t.join("evil").exists());
        // a pack that starts on
        add(&[("One".to_string(), src.join("One"))], &maps, &lib, "Showdown", true).unwrap();
        assert!(!maps.join("One").exists(), "an existing map keeps its state");
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn map_names_may_have_punctuation_but_never_leave_the_folder() {
        for ok in ["! 1v1 ! super no money map v2", "!!!survival 6 players (3 min)pfp edit by e-3v7", "Black Hell (v3)", "A & B", "map.v2"] {
            assert!(is_safe_name(ok), "{ok}");
        }
        for bad in ["", ".", "..", "../x", "a/b", "a\\b", "C:x", "trailing.", "trailing ", "star*", "q?", "pipe|", "tab\there"] {
            assert!(!is_safe_name(bad), "{bad:?}");
        }
    }

    #[test]
    fn refuses_names_that_could_leave_the_folder_and_deletes_in_both_places() {
        let t = temp("delete");
        let (maps, lib) = (t.join("Maps"), t.join("lib"));
        map(&maps, "Keep");
        map(&maps, "Drop");
        set_enabled(&maps, &lib, &["Drop".into()], false).unwrap();
        assert!(set_enabled(&maps, &lib, &["../Keep".into()], false).is_err());
        assert!(delete(&maps, &lib, &["..".into()]).is_err());
        assert_eq!(delete(&maps, &lib, &["Drop".into()]).unwrap(), 1);
        assert_eq!(list(&maps, &lib).len(), 1);
        assert!(maps.join("Keep").is_dir());
        std::fs::remove_dir_all(t).unwrap();
    }
}
