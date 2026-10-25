//! Locate a Steam install of Generals / Zero Hour by looking for their `.big` archives,
//! so we don't depend on Steam app IDs.
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct GameInstall {
    pub dir: PathBuf,
    pub zero_hour: bool,
}

/// Default Steam roots per platform.
pub fn steam_roots() -> Vec<PathBuf> {
    let home = std::env::var_os("HOME").or_else(|| std::env::var_os("USERPROFILE")).map(PathBuf::from);
    let mut v = vec![];
    if cfg!(windows) {
        for k in ["ProgramFiles(x86)", "ProgramFiles"] {
            if let Some(p) = std::env::var_os(k) {
                v.push(PathBuf::from(p).join("Steam"));
            }
        }
    }
    if let Some(h) = home {
        if cfg!(target_os = "macos") {
            v.push(h.join("Library/Application Support/Steam"));
        } else if cfg!(target_os = "linux") {
            v.push(h.join(".steam/steam"));
            v.push(h.join(".local/share/Steam"));
            v.push(h.join(".var/app/com.valvesoftware.Steam/.local/share/Steam"));
        }
    }
    v
}

/// Library folders listed in `steamapps/libraryfolders.vdf`, plus the root itself.
pub fn library_dirs(root: &Path) -> Vec<PathBuf> {
    let text = std::fs::read_to_string(root.join("steamapps/libraryfolders.vdf")).unwrap_or_default();
    let mut dirs = vec![root.to_path_buf()];
    dirs.extend(parse_library_paths(&text));
    dirs
}

/// Minimal VDF scan: every `"path"  "<value>"` pair. Values use `\\` escapes.
pub fn parse_library_paths(vdf: &str) -> Vec<PathBuf> {
    vdf.lines()
        .filter_map(|l| {
            let mut q = l.split('"').skip(1).step_by(2);
            (q.next()? == "path").then(|| q.next().map(|v| PathBuf::from(v.replace("\\\\", "\\"))))?
        })
        .collect()
}

/// Look in `steamapps/common/*` and one level below (Ultimate Collection nests the games).
pub fn find_in_library(lib: &Path) -> Vec<GameInstall> {
    let common = lib.join("steamapps/common");
    let mut out = vec![];
    for e in read_dirs(&common) {
        for d in std::iter::once(e.clone()).chain(read_dirs(&e)) {
            if d.join("INIZH.big").is_file() {
                out.push(GameInstall { dir: d, zero_hour: true });
            } else if d.join("INI.big").is_file() {
                out.push(GameInstall { dir: d, zero_hour: false });
            }
        }
    }
    out
}

fn read_dirs(p: &Path) -> Vec<PathBuf> {
    std::fs::read_dir(p)
        .map(|r| r.filter_map(|e| e.ok().map(|e| e.path())).filter(|p| p.is_dir()).collect())
        .unwrap_or_default()
}

pub fn detect() -> Vec<GameInstall> {
    steam_roots().iter().flat_map(|r| library_dirs(r)).flat_map(|l| find_in_library(&l)).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_vdf_paths() {
        let vdf = "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"\n\t\t\"label\"\t\t\"\"\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"/mnt/games/steam\"\n\t}\n}";
        assert_eq!(
            parse_library_paths(vdf),
            vec![PathBuf::from("D:\\SteamLibrary"), PathBuf::from("/mnt/games/steam")]
        );
    }

    #[test]
    fn finds_nested_zero_hour() {
        let t = std::env::temp_dir().join(format!("gen-steam-{}", std::process::id()));
        let zh = t.join("steamapps/common/Ultimate Collection/Zero Hour");
        let base = t.join("steamapps/common/Generals");
        std::fs::create_dir_all(&zh).unwrap();
        std::fs::create_dir_all(&base).unwrap();
        std::fs::write(zh.join("INIZH.big"), "").unwrap();
        std::fs::write(base.join("INI.big"), "").unwrap();
        let mut found = find_in_library(&t);
        found.sort_by_key(|g| g.zero_hour);
        assert_eq!(found, vec![GameInstall { dir: base, zero_hour: false }, GameInstall { dir: zh, zero_hour: true }]);
        std::fs::remove_dir_all(t).unwrap();
    }
}
