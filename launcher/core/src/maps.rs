//! Map packs. Zero Hour finds maps as `<user data>/Maps/<Map Name>/<Map Name>.map` (plus preview and text files in
//! the same folder). A pack is downloaded from GitHub (which, unlike mod sites, allows it), unpacked safely, and only
//! its map folders are copied into the maps directory. Any other archive or folder of maps can be added the same way.
use crate::extract;
use crate::mods::sanitize_folder;
use crate::proc::quiet;
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

/// The folder the game keeps its user data in, under Documents.
pub const USER_DATA_LEAF: &str = "Command and Conquer Generals Zero Hour Data";

#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct MapPack {
    pub id: String,
    pub name: String,
    pub description: String,
    pub homepage: String,
    #[serde(default)]
    pub license: String,
    pub zip_url: String,
    /// Folders inside the zip whose subfolders are maps. Empty means: look for map folders anywhere in the zip.
    #[serde(default)]
    pub subdirs: Vec<String>,
    pub size_mb: u64,
}

pub fn load_packs(json: &str) -> Result<Vec<MapPack>, serde_json::Error> {
    serde_json::from_str(json)
}

fn is_map_file(p: &Path) -> bool {
    p.extension().map(|e| e.eq_ignore_ascii_case("map")).unwrap_or(false)
}

fn dirs_in(p: &Path) -> Vec<PathBuf> {
    let mut v: Vec<PathBuf> = std::fs::read_dir(p).into_iter().flatten().flatten().filter(|e| e.file_type().map(|t| t.is_dir()).unwrap_or(false)).map(|e| e.path()).collect();
    v.sort();
    v
}

/// How many maps are installed (folders that hold a `.map` file).
pub fn count_maps(maps_dir: &Path) -> usize {
    dirs_in(maps_dir).iter().filter(|d| std::fs::read_dir(d).into_iter().flatten().flatten().any(|e| is_map_file(&e.path()))).count()
}

/// Map folders somewhere under `root` (a directory with a `.map` file in it), looking up to `depth` levels down.
/// Loose map files with no folder are grouped into a folder named after the map.
pub fn find_maps(root: &Path, depth: usize) -> Vec<(String, PathBuf)> {
    let mut out = vec![];
    let entries: Vec<_> = std::fs::read_dir(root).into_iter().flatten().flatten().collect();
    let has_map = entries.iter().any(|e| is_map_file(&e.path()));
    if has_map {
        let name = root.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
        out.push((name, root.to_path_buf()));
    } else if depth > 0 {
        for d in dirs_in(root) {
            out.extend(find_maps(&d, depth - 1));
        }
    }
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
        } // symlinks and anything else are skipped
    }
    Ok(())
}

/// Copy each map folder into `maps_dir` (replacing an older copy). Returns (installed, skipped for an unusable name).
pub fn install_maps(found: &[(String, PathBuf)], maps_dir: &Path) -> Result<(usize, usize), String> {
    std::fs::create_dir_all(maps_dir).map_err(|e| e.to_string())?;
    let (mut done, mut skipped) = (0, 0);
    for (name, dir) in found {
        let Some(safe) = sanitize_folder(name).filter(|s| s == name) else {
            skipped += 1;
            continue;
        };
        let dest = maps_dir.join(&safe);
        let _ = std::fs::remove_dir_all(&dest);
        copy_tree(dir, &dest).map_err(|e| format!("couldn't copy {safe}: {e}"))?;
        done += 1;
    }
    Ok((done, skipped))
}

/// Wrap loose files at the top of an unpacked archive (`Map.map`, `Map.tga`, `Map.ini`...) in a folder per map,
/// since the game wants every map in its own folder.
fn gather_loose(root: &Path) -> Result<(), String> {
    let files: Vec<PathBuf> = std::fs::read_dir(root).map_err(|e| e.to_string())?.flatten().filter(|e| e.file_type().map(|t| t.is_file()).unwrap_or(false)).map(|e| e.path()).collect();
    for map in files.iter().filter(|p| is_map_file(p)) {
        let stem = map.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
        let folder = root.join(&stem);
        std::fs::create_dir_all(&folder).map_err(|e| e.to_string())?;
        for f in &files {
            let fname = f.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
            if fname.to_ascii_lowercase().starts_with(&stem.to_ascii_lowercase()) && f.exists() {
                std::fs::rename(f, folder.join(&fname)).map_err(|e| e.to_string())?;
            }
        }
    }
    Ok(())
}

fn temp_dir(work: &Path, tag: &str) -> PathBuf {
    work.join(format!(".maps-{tag}-{}-{}", std::process::id(), std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0)))
}

/// Install maps from an archive or a folder the user chose. Returns how many maps were added.
pub fn install_from_path(src: &Path, maps_dir: &Path, work: &Path) -> Result<usize, String> {
    let tmp = temp_dir(work, "in");
    std::fs::create_dir_all(&tmp).map_err(|e| e.to_string())?;
    let result = (|| {
        let root = if src.is_dir() {
            src.to_path_buf()
        } else {
            extract::extract(src, &tmp)?;
            gather_loose(&tmp)?;
            tmp.clone()
        };
        let mut found = find_maps(&root, 5);
        if src.is_dir() && found.is_empty() {
            gather_loose_copy_check(&root)?;
        }
        if found.is_empty() {
            return Err("No maps were found in that. A map is a folder with a .map file inside".to_string());
        }
        found.dedup_by(|a, b| a.1 == b.1);
        let (n, skipped) = install_maps(&found, maps_dir)?;
        if n == 0 {
            return Err(format!("None of those {skipped} map folders have names the game can use"));
        }
        Ok(n)
    })();
    let _ = std::fs::remove_dir_all(&tmp);
    result
}

// A chosen folder is never modified; loose files in it just aren't supported.
fn gather_loose_copy_check(_root: &Path) -> Result<(), String> {
    Ok(())
}

// ---- downloading a pack ---------------------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Default)]
pub enum Phase {
    #[default]
    Idle,
    Downloading,
    Installing,
    Done,
    Failed,
}

#[derive(Debug, Clone, Serialize, Default)]
pub struct MapState {
    pub phase: Phase,
    pub percent: f32,
    pub message: String,
    pub installed: usize,
}

pub struct MapJob {
    state: Arc<Mutex<MapState>>,
    cancelled: Arc<AtomicBool>,
}

fn set(state: &Arc<Mutex<MapState>>, phase: Phase, percent: f32, message: &str, installed: usize) {
    *state.lock().unwrap() = MapState { phase, percent, message: message.into(), installed };
}

/// Download `pack.zip_url`, unpack it, and copy its maps in. `proto` is curl's protocol restriction ("=https").
fn run_pack(pack: &MapPack, maps_dir: &Path, work: &Path, proto: &str, state: &Arc<Mutex<MapState>>, cancelled: &Arc<AtomicBool>) -> Result<usize, String> {
    std::fs::create_dir_all(work).map_err(|e| e.to_string())?;
    let tmp = temp_dir(work, &pack.id);
    std::fs::create_dir_all(&tmp).map_err(|e| e.to_string())?;
    let zip = tmp.join("pack.zip");
    let result = (|| {
        let mut child = quiet(&mut Command::new("curl"))
            .args(["-fsSL", "--proto", proto, "--proto-redir", proto, "--max-time", "1800", "-o"])
            .arg(&zip)
            .arg(&pack.zip_url)
            .spawn()
            .map_err(|e| format!("curl isn't available ({e})"))?;
        let expected = (pack.size_mb.max(1) * 1_048_576) as f32;
        let status = loop {
            if let Some(s) = child.try_wait().map_err(|e| e.to_string())? {
                break s;
            }
            if cancelled.load(Ordering::Relaxed) {
                let _ = child.kill();
                let _ = child.wait();
                return Err("cancelled".into());
            }
            let got = std::fs::metadata(&zip).map(|m| m.len() as f32).unwrap_or(0.0);
            set(state, Phase::Downloading, (got / expected * 90.0).min(90.0), &format!("Downloading {} ({} MB so far)", pack.name, (got / 1_048_576.0) as u64), 0);
            std::thread::sleep(std::time::Duration::from_millis(300));
        };
        if !status.success() {
            return Err("Couldn't download the maps. Check your internet connection".into());
        }
        set(state, Phase::Installing, 92.0, "Unpacking…", 0);
        let unpack = tmp.join("unpack");
        std::fs::create_dir_all(&unpack).map_err(|e| e.to_string())?;
        extract::extract(&zip, &unpack)?;
        let _ = std::fs::remove_file(&zip);
        // GitHub wraps the repository in one top-level folder.
        let root = {
            let top = dirs_in(&unpack);
            if top.len() == 1 { top[0].clone() } else { unpack.clone() }
        };
        set(state, Phase::Installing, 96.0, "Copying maps…", 0);
        let mut found = if pack.subdirs.is_empty() { find_maps(&root, 6) } else { vec![] };
        for sub in &pack.subdirs {
            if sub.split('/').any(|c| c == ".." || c.is_empty()) {
                return Err("the pack lists an unsafe folder".into());
            }
            for d in dirs_in(&root.join(sub)) {
                if !find_maps(&d, 0).is_empty() {
                    found.push((d.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default(), d));
                }
            }
        }
        if found.is_empty() {
            return Err("The download didn't contain the expected maps".into());
        }
        install_maps(&found, maps_dir).map(|(n, _)| n)
    })();
    let _ = std::fs::remove_dir_all(&tmp);
    result
}

impl MapJob {
    pub fn start(pack: MapPack, maps_dir: PathBuf, work: PathBuf) -> MapJob {
        Self::start_with(pack, maps_dir, work, "=https")
    }

    fn start_with(pack: MapPack, maps_dir: PathBuf, work: PathBuf, proto: &'static str) -> MapJob {
        let state: Arc<Mutex<MapState>> = Arc::default();
        let cancelled = Arc::new(AtomicBool::new(false));
        set(&state, Phase::Downloading, 0.0, "Starting…", 0);
        let (s, c) = (state.clone(), cancelled.clone());
        std::thread::spawn(move || match run_pack(&pack, &maps_dir, &work, proto, &s, &c) {
            Ok(n) => set(&s, Phase::Done, 100.0, &format!("{n} maps installed"), n),
            Err(m) if m == "cancelled" => set(&s, Phase::Idle, 0.0, "", 0),
            Err(m) => set(&s, Phase::Failed, 0.0, &m, 0),
        });
        MapJob { state, cancelled }
    }

    pub fn state(&self) -> MapState {
        self.state.lock().unwrap().clone()
    }

    pub fn cancel(&self) {
        self.cancelled.store(true, Ordering::Relaxed);
    }
}

impl Drop for MapJob {
    fn drop(&mut self) {
        self.cancel();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::extract::tests::make_zip;

    fn temp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("gen-maps-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    fn write(p: &Path, c: &str) {
        std::fs::create_dir_all(p.parent().unwrap()).unwrap();
        std::fs::write(p, c).unwrap();
    }

    #[test]
    fn bundled_catalog_is_https_from_known_hosts_and_safe() {
        let packs = load_packs(include_str!("../../../mods/mappacks.json")).unwrap();
        assert!(packs.len() >= 4 && packs.iter().any(|p| p.id == "gentool-all"));
        for p in &packs {
            assert!(p.zip_url.starts_with("https://www.gentool.net/download/") || p.zip_url.starts_with("https://github.com/"), "{}", p.id);
            assert!(p.homepage.starts_with("https://"), "{}", p.id);
            assert!(p.subdirs.iter().all(|s| !s.contains("..") && !s.starts_with('/')), "{}", p.id);
            assert!(p.size_mb > 0, "{}", p.id);
        }
        let ids: std::collections::HashSet<_> = packs.iter().map(|p| &p.id).collect();
        assert_eq!(ids.len(), packs.len(), "ids are unique");
    }

    #[test]
    fn finds_map_folders_at_any_depth() {
        let t = temp("find");
        write(&t.join("a/Map One ZH v1/Map One ZH v1.map"), "m");
        write(&t.join("a/Map One ZH v1/Map One ZH v1.tga"), "t");
        write(&t.join("deep/er/Map Two/Map Two.MAP"), "m");
        write(&t.join("docs/readme.txt"), "x");
        let found: Vec<String> = find_maps(&t, 5).into_iter().map(|(n, _)| n).collect();
        assert_eq!(found, ["Map One ZH v1", "Map Two"]);
        assert_eq!(find_maps(&t, 2).len(), 1, "depth is limited: Map Two is three levels down");
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn installs_a_folder_of_maps_and_counts_them() {
        let t = temp("folder");
        write(&t.join("src/Alpha ZH v1/Alpha ZH v1.map"), "1");
        write(&t.join("src/Alpha ZH v1/Alpha ZH v1.str"), "2");
        write(&t.join("src/Beta/Beta.map"), "3");
        let maps = t.join("Maps");
        write(&maps.join("Alpha ZH v1/old.txt"), "stale");
        assert_eq!(install_from_path(&t.join("src"), &maps, &t.join("work")).unwrap(), 2);
        assert_eq!(count_maps(&maps), 2);
        assert!(!maps.join("Alpha ZH v1/old.txt").exists(), "an older copy is replaced");
        assert!(maps.join("Alpha ZH v1/Alpha ZH v1.str").is_file());
        assert!(install_from_path(&t.join("work"), &maps, &t.join("work")).unwrap_err().contains("No maps"));
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn refuses_unusable_folder_names() {
        let t = temp("names");
        let bad = vec![("../evil".to_string(), t.join("x")), ("...".to_string(), t.join("y"))];
        write(&t.join("x/a.map"), "1");
        write(&t.join("y/a.map"), "1");
        assert_eq!(install_maps(&bad, &t.join("Maps")).unwrap(), (0, 2));
        assert!(!t.join("evil").exists());
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn installs_a_zip_including_loose_map_files() {
        let t = temp("zip");
        let zip = t.join("one.zip");
        std::fs::write(&zip, make_zip(&[("Lone Map.map", b"m"), ("Lone Map.tga", b"t"), ("Lone Map.ini", b"i"), ("readme.txt", b"r")])).unwrap();
        let maps = t.join("Maps");
        match install_from_path(&zip, &maps, &t.join("work")) {
            Err(e) if e.contains("no tool") => return, // no unzip-capable tool here
            r => assert_eq!(r.unwrap(), 1),
        }
        assert!(maps.join("Lone Map/Lone Map.map").is_file() && maps.join("Lone Map/Lone Map.tga").is_file());
        assert!(!maps.join("Lone Map/readme.txt").exists());
        std::fs::remove_dir_all(t).unwrap();
    }

    /// A pack that lists no sub-folders: any map folder in the zip is installed, wherever it sits.
    #[cfg(unix)]
    #[test]
    fn a_pack_without_subdirs_installs_every_map_folder_it_finds() {
        let t = temp("flat");
        let zip = t.join("m.zip");
        std::fs::write(&zip, make_zip(&[("Maps/Alpha ZH v1/Alpha ZH v1.map", b"m"), ("Maps/Alpha ZH v1/Alpha ZH v1.tga", b"t"), ("Beta ZH v2/Beta ZH v2.map", b"m"), ("notes.txt", b"n")])).unwrap();
        let pack = MapPack { id: "f".into(), name: "Flat".into(), description: String::new(), homepage: String::new(), license: String::new(), zip_url: format!("file://{}", zip.display()), subdirs: vec![], size_mb: 1 };
        let job = MapJob::start_with(pack, t.join("Maps"), t.join("work"), "=file");
        let mut s = job.state();
        for _ in 0..200 {
            s = job.state();
            if s.phase == Phase::Done || s.phase == Phase::Failed {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(50));
        }
        if s.phase == Phase::Failed && s.message.contains("no tool") {
            return;
        }
        assert_eq!((s.phase, s.installed), (Phase::Done, 2), "{s:?}");
        assert!(t.join("Maps/Alpha ZH v1/Alpha ZH v1.tga").is_file() && t.join("Maps/Beta ZH v2/Beta ZH v2.map").is_file());
        std::fs::remove_dir_all(t).unwrap();
    }

    /// A whole pack, end to end, from a local zip (curl can read file:// URLs).
    #[cfg(unix)]
    #[test]
    fn downloads_unpacks_and_installs_a_pack_keeping_only_its_map_folders() {
        let t = temp("pack");
        let zip = t.join("repo.zip");
        std::fs::write(&zip, make_zip(&[
            ("Repo-main/release/maps_1v1_zh/Duel ZH v1/Duel ZH v1.map", b"m"),
            ("Repo-main/release/maps_1v1_zh/Duel ZH v1/Duel ZH v1.tga", b"t"),
            ("Repo-main/release/maps_fun_zh/Fun ZH v2/Fun ZH v2.map", b"m"),
            ("Repo-main/candidate/Unfinished/Unfinished.map", b"m"),
            ("Repo-main/README.md", b"r"),
        ])).unwrap();
        let pack = MapPack { id: "t".into(), name: "Test".into(), description: String::new(), homepage: String::new(), license: "MIT".into(), zip_url: format!("file://{}", zip.display()), subdirs: vec!["release/maps_1v1_zh".into(), "release/maps_fun_zh".into()], size_mb: 1 };
        let maps = t.join("Maps");
        let job = MapJob::start_with(pack, maps.clone(), t.join("work"), "=file");
        let mut s = job.state();
        for _ in 0..200 {
            s = job.state();
            if s.phase == Phase::Done || s.phase == Phase::Failed {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(50));
        }
        if s.phase == Phase::Failed && s.message.contains("no tool") {
            return;
        }
        assert_eq!(s.phase, Phase::Done, "{s:?}");
        assert_eq!(s.installed, 2);
        assert!(maps.join("Duel ZH v1/Duel ZH v1.map").is_file() && maps.join("Fun ZH v2/Fun ZH v2.map").is_file());
        assert!(!maps.join("Unfinished").exists(), "only the listed folders are installed");
        // nothing left behind in the work folder
        assert_eq!(std::fs::read_dir(t.join("work")).unwrap().count(), 0);
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn a_failed_download_reports_a_clear_message() {
        let t = temp("fail");
        let pack = MapPack { id: "t".into(), name: "Test".into(), description: String::new(), homepage: String::new(), license: "MIT".into(), zip_url: "file:///nonexistent/none.zip".into(), subdirs: vec!["x".into()], size_mb: 1 };
        let job = MapJob::start_with(pack, t.join("Maps"), t.join("work"), "=file");
        let mut s = job.state();
        for _ in 0..200 {
            s = job.state();
            if s.phase == Phase::Failed || s.phase == Phase::Done {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(50));
        }
        assert_eq!(s.phase, Phase::Failed);
        assert!(s.message.contains("Couldn't download"), "{s:?}");
        std::fs::remove_dir_all(t).unwrap();
    }
}
