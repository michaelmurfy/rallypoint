//! Mods. A mod is a folder of `.big` archives under `<data>/Mods/<folder>`, loaded with the engine's `-mod <folder>`.
//! Installing means unpacking an archive, folder or installer into that layout and checking it really has game files.
//! Mod sites (ModDB) don't allow automated downloads, so the launcher opens the official page and then picks the
//! file up from the user's Downloads folder (`find_download`) or from a drag and drop.
use crate::extract::{self, Kind};
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

#[derive(Debug, Clone, Deserialize, Serialize, PartialEq)]
pub struct Mod {
    pub id: String,
    pub name: String,
    #[serde(default)]
    pub description: String,
    pub homepage: String,
    /// Official page where the user downloads it (opened in their browser).
    #[serde(default)]
    pub download_page: String,
    /// Folder inside the mods dir.
    pub path: String,
    /// Lower-case fragments of the download's file name, used to spot it in the Downloads folder.
    #[serde(default)]
    pub detect: Vec<String>,
    #[serde(default)]
    pub notes: String,
    #[serde(default)]
    pub args: Vec<String>,
    /// Id of the mod this one is an add-on for. The engine loads one mod folder, so at launch the two are combined.
    #[serde(default)]
    pub addon_for: Option<String>,
}

pub fn load_manifest(json: &str) -> Result<Vec<Mod>, serde_json::Error> {
    serde_json::from_str(json)
}

impl Mod {
    pub fn full_path(&self, mods_dir: &Path) -> PathBuf {
        mods_dir.join(&self.path)
    }
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Installed {
    pub folder: String,
    pub bigs: usize,
    pub mb: u64,
}

fn is_big(p: &Path) -> bool {
    p.extension().map(|e| e.eq_ignore_ascii_case("big") || e.eq_ignore_ascii_case("gib")).unwrap_or(false)
}

/// (number of .big files, total bytes) under `dir`.
fn scan(dir: &Path) -> (usize, u64) {
    let (mut bigs, mut bytes, mut stack) = (0, 0, vec![dir.to_path_buf()]);
    while let Some(d) = stack.pop() {
        for e in std::fs::read_dir(&d).into_iter().flatten().flatten() {
            let Ok(ty) = e.file_type() else { continue };
            if ty.is_dir() {
                stack.push(e.path());
            } else if ty.is_file() {
                bytes += e.metadata().map(|m| m.len()).unwrap_or(0);
                bigs += is_big(&e.path()) as usize;
            }
        }
    }
    (bigs, bytes)
}

/// Every folder in `mods_dir` that holds game archives, whether or not it is in the catalog.
pub fn list_installed(mods_dir: &Path) -> Vec<Installed> {
    let mut v: Vec<Installed> = std::fs::read_dir(mods_dir)
        .into_iter()
        .flatten()
        .flatten()
        .filter(|e| e.file_type().map(|t| t.is_dir()).unwrap_or(false))
        .filter_map(|e| {
            let folder = e.file_name().to_string_lossy().into_owned();
            let (bigs, bytes) = scan(&e.path());
            (!folder.starts_with('.') && bigs > 0).then_some(Installed { folder, bigs, mb: bytes / 1_048_576 })
        })
        .collect();
    v.sort_by(|a, b| a.folder.to_lowercase().cmp(&b.folder.to_lowercase()));
    v
}

/// A folder name that can't escape the mods dir or hide itself.
pub fn sanitize_folder(name: &str) -> Option<String> {
    let s: String = name.trim().chars().filter(|c| c.is_alphanumeric() || " ._-&".contains(*c)).take(64).collect();
    let s = s.trim().trim_matches('.').to_string();
    (!s.is_empty()).then_some(s)
}

/// Folder name for a downloaded archive: its name without the extension and version-ish noise.
pub fn folder_for_archive(path: &Path) -> Option<String> {
    let n = path.file_name()?.to_string_lossy();
    let stem = n.rsplit_once('.').map(|(s, _)| s).unwrap_or(&n);
    sanitize_folder(stem.trim_end_matches(".tar"))
}

fn copy_dir(src: &Path, dst: &Path) -> Result<(), String> {
    std::fs::create_dir_all(dst).map_err(|e| e.to_string())?;
    for e in std::fs::read_dir(src).map_err(|e| e.to_string())? {
        let e = e.map_err(|e| e.to_string())?;
        let ty = e.file_type().map_err(|e| e.to_string())?;
        if ty.is_symlink() {
            return Err("that folder contains a symbolic link, which isn't allowed".into());
        }
        let to = dst.join(e.file_name());
        if ty.is_dir() {
            copy_dir(&e.path(), &to)?;
        } else {
            std::fs::copy(e.path(), to).map_err(|e| e.to_string())?;
        }
    }
    Ok(())
}

/// Descend through wrapper folders (`Mod_v1.2/Mod/...`) to where the files actually are.
fn flatten(mut cur: PathBuf) -> PathBuf {
    loop {
        let entries: Vec<_> = std::fs::read_dir(&cur).into_iter().flatten().flatten().collect();
        match entries.as_slice() {
            [only] if only.file_type().map(|t| t.is_dir()).unwrap_or(false) => cur = only.path(),
            _ => return cur,
        }
    }
}

/// Does `dir` hold an `.exe` somewhere (an installer inside a zip, say)?
fn has_installer(dir: &Path) -> bool {
    let mut stack = vec![dir.to_path_buf()];
    while let Some(d) = stack.pop() {
        for e in std::fs::read_dir(&d).into_iter().flatten().flatten() {
            let p = e.path();
            match e.file_type() {
                Ok(t) if t.is_dir() => stack.push(p),
                Ok(t) if t.is_file() && p.extension().map(|x| x.eq_ignore_ascii_case("exe")).unwrap_or(false) => return true,
                _ => {}
            }
        }
    }
    false
}

fn unique_suffix() -> String {
    format!("{}-{}", std::process::id(), SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0))
}

/// Install `src` (an archive, an installer, or a folder) as `mods_dir/folder`, replacing any previous version.
pub fn install(src: &Path, mods_dir: &Path, folder: &str) -> Result<Installed, String> {
    let folder = sanitize_folder(folder).ok_or("that isn't a valid mod name")?;
    std::fs::create_dir_all(mods_dir).map_err(|e| e.to_string())?;
    let tmp = mods_dir.join(format!(".incoming-{}", unique_suffix()));
    std::fs::create_dir_all(&tmp).map_err(|e| e.to_string())?;
    let result = (|| {
        if src.is_dir() {
            copy_dir(src, &tmp)?;
        } else if crate::clickteam::looks_like(src) {
            // Shockwave's download is a Clickteam installer. Unpack the archives; do not run its launcher.
            crate::clickteam::extract_into(src, &tmp)?;
        } else {
            extract::extract(src, &tmp)?;
        }
        let mut root = flatten(tmp.clone());
        let mut scanned = scan(&root);
        if scanned.0 == 0 {
            if let Some(unpacked) = crate::clickteam::unpack_tree(&root)? {
                root = flatten(unpacked);
                scanned = scan(&root);
            }
        }
        let (bigs, bytes) = scanned;
        if bigs == 0 {
            return Err(if extract::kind_of(src) == Some(Kind::Exe) || has_installer(&tmp) {
                "That is a Windows installer, and its files can't be unpacked without running it. Run it on Windows (or in Wine or CrossOver), \
                 pick a new empty folder as the install location, then choose that folder here instead."
                    .to_string()
            } else {
                "No .big game files were found in that. Mods that use loose files or an .exe patcher can't be installed automatically; see the README".to_string()
            });
        }
        let dest = mods_dir.join(&folder);
        let old = mods_dir.join(format!(".old-{}", unique_suffix()));
        if dest.exists() {
            std::fs::rename(&dest, &old).map_err(|e| e.to_string())?;
        }
        std::fs::rename(&root, &dest).map_err(|e| {
            let _ = std::fs::rename(&old, &dest); // put the previous version back
            e.to_string()
        })?;
        let _ = std::fs::remove_dir_all(&old);
        Ok(Installed { folder: folder.clone(), bigs, mb: bytes / 1_048_576 })
    })();
    let _ = std::fs::remove_dir_all(&tmp);
    result
}

pub fn remove(mods_dir: &Path, folder: &str) -> Result<(), String> {
    let name = sanitize_folder(folder).filter(|n| n == folder).ok_or("that isn't a valid mod name")?;
    let p = mods_dir.join(name);
    let meta = std::fs::symlink_metadata(&p).map_err(|_| "that mod isn't installed".to_string())?;
    if !meta.is_dir() {
        return Err("that isn't a mod folder".into());
    }
    std::fs::remove_dir_all(p).map_err(|e| e.to_string())
}

fn link_tree(src: &Path, dst: &Path) -> Result<(), String> {
    std::fs::create_dir_all(dst).map_err(|e| e.to_string())?;
    for e in std::fs::read_dir(src).map_err(|e| e.to_string())? {
        let e = e.map_err(|e| e.to_string())?;
        let ty = e.file_type().map_err(|e| e.to_string())?;
        let to = dst.join(e.file_name());
        if ty.is_dir() {
            link_tree(&e.path(), &to)?;
        } else if ty.is_file() {
            let _ = std::fs::remove_file(&to); // a later layer replaces an earlier file of the same name
            // hard links cost no space or time; fall back to a copy across drives or on odd filesystems
            std::fs::hard_link(e.path(), &to).or_else(|_| std::fs::copy(e.path(), &to).map(|_| ())).map_err(|e| e.to_string())?;
        }
    }
    Ok(())
}

/// Build the folder to launch for an add-on: the base mod's files with the add-on's laid over them.
/// Rebuilt every launch (it is only links) and kept out of the installed list by its leading dot.
/// Put each selected mod's required base immediately before it. Later folders win where files overlap.
pub fn with_bases(catalog: &[Mod], selected: &[String]) -> Vec<String> {
    let mut out = vec![];
    for folder in selected {
        if let Some(m) = catalog.iter().find(|m| &m.path == folder) {
            if let Some(parent) = m.addon_for.as_ref().and_then(|id| catalog.iter().find(|p| &p.id == id)) {
                if !out.iter().any(|f| f == &parent.path) {
                    out.push(parent.path.clone());
                }
            }
        }
        if !out.iter().any(|f| f == folder) {
            out.push(folder.clone());
        }
    }
    out
}

/// The engine loads one `-mod` folder. One selection is used as-is; several are hard-linked into `Mods/.run`
/// (later folders replace earlier files of the same name). The dot keeps it out of the installed list.
pub fn stack(mods_dir: &Path, folders: &[String]) -> Result<Option<PathBuf>, String> {
    let mut dirs = vec![];
    for f in folders {
        let name = sanitize_folder(f).filter(|n| n == f).ok_or("invalid mod")?;
        let dir = mods_dir.join(&name);
        if !dir.is_dir() {
            return Err(format!("{name} isn't installed"));
        }
        dirs.push(dir);
    }
    match dirs.as_slice() {
        [] => Ok(None),
        [one] => Ok(Some(one.clone())),
        _ => {
            let out = mods_dir.join(".run");
            let _ = std::fs::remove_dir_all(&out);
            for d in &dirs {
                link_tree(d, &out)?;
            }
            Ok(Some(out))
        }
    }
}

pub fn compose(mods_dir: &Path, base: &str, addon: &str) -> Result<PathBuf, String> {
    let (b, a) = (sanitize_folder(base).filter(|n| n == base), sanitize_folder(addon).filter(|n| n == addon));
    let (b, a) = (b.ok_or("invalid mod")?, a.ok_or("invalid mod")?);
    let (base_dir, addon_dir) = (mods_dir.join(&b), mods_dir.join(&a));
    if !base_dir.is_dir() {
        return Err(format!("{b} needs to be installed first"));
    }
    if !addon_dir.is_dir() {
        return Err(format!("{a} isn't installed"));
    }
    let out = mods_dir.join(format!(".run-{a}"));
    let _ = std::fs::remove_dir_all(&out);
    link_tree(&base_dir, &out)?;
    link_tree(&addon_dir, &out)?;
    Ok(out)
}

pub fn downloads_dir() -> Option<PathBuf> {
    std::env::var_os("HOME").or_else(|| std::env::var_os("USERPROFILE")).map(|h| PathBuf::from(h).join("Downloads"))
}

const PARTIAL: [&str; 6] = [".crdownload", ".part", ".download", ".tmp", ".opdownload", ".partial"];

/// A finished download in `dir` that appeared after `since` and looks like this mod's archive (or installer).
/// A file counts as finished once it hasn't changed for a few seconds.
pub fn find_download(dir: &Path, since: SystemTime, detect: &[String]) -> Option<PathBuf> {
    let now = SystemTime::now();
    std::fs::read_dir(dir)
        .ok()?
        .flatten()
        .filter(|e| e.file_type().map(|t| t.is_file()).unwrap_or(false))
        .filter_map(|e| {
            let p = e.path();
            let name = p.file_name()?.to_string_lossy().to_ascii_lowercase();
            if PARTIAL.iter().any(|s| name.ends_with(s)) || extract::kind_of(&p).is_none() {
                return None;
            }
            if !detect.is_empty() && !detect.iter().any(|d| name.contains(d.as_str())) {
                return None;
            }
            let modified = e.metadata().ok()?.modified().ok()?;
            let settled = now.duration_since(modified).map(|d| d >= Duration::from_secs(3)).unwrap_or(false);
            (modified >= since.checked_sub(Duration::from_secs(2)).unwrap_or(since) && settled).then_some((modified, p))
        })
        .max_by_key(|(m, _)| *m)
        .map(|(_, p)| p)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::extract::tests::make_zip;

    fn temp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("gen-mods-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn bundled_catalog_parses_and_points_at_https_pages() {
        let m = load_manifest(include_str!("../../../mods/mods.json")).unwrap();
        assert!(m.iter().any(|m| m.id == "shockwave"));
        for m in &m {
            assert!(m.homepage.starts_with("https://") && m.download_page.starts_with("https://"), "{}", m.id);
            assert_eq!(sanitize_folder(&m.path).as_deref(), Some(m.path.as_str()));
            assert!(!m.detect.is_empty());
        }
    }

    #[test]
    fn bundled_addon_points_at_a_catalog_mod() {
        let m = load_manifest(include_str!("../../../mods/mods.json")).unwrap();
        let chaos = m.iter().find(|m| m.id == "shockwave-chaos").unwrap();
        assert_eq!(chaos.addon_for.as_deref(), Some("shockwave"));
        assert!(m.iter().any(|b| Some(&b.id) == chaos.addon_for.as_ref()), "base mod is in the catalog");
        assert_ne!(chaos.path, m.iter().find(|b| b.id == "shockwave").unwrap().path);
    }

    #[test]
    fn composes_an_addon_over_its_base_without_touching_either() {
        let t = temp("compose");
        let mods = t.join("Mods");
        for (dir, files) in [("Base", vec![("a.big", "base-a"), ("same.big", "base"), ("Sub/x.big", "x")]), ("Addon", vec![("same.big", "addon"), ("b.big", "addon-b")])] {
            for (f, c) in files {
                let p = mods.join(dir).join(f);
                std::fs::create_dir_all(p.parent().unwrap()).unwrap();
                std::fs::write(p, c).unwrap();
            }
        }
        let out = compose(&mods, "Base", "Addon").unwrap();
        let read = |p: &str| std::fs::read_to_string(out.join(p)).unwrap();
        assert_eq!((read("a.big").as_str(), read("b.big").as_str(), read("Sub/x.big").as_str()), ("base-a", "addon-b", "x"));
        assert_eq!(read("same.big"), "addon", "the add-on's file wins");
        assert_eq!(std::fs::read_to_string(mods.join("Base/same.big")).unwrap(), "base", "the base is untouched");
        // rebuilding works, and the composed folder never shows up as an installed mod
        compose(&mods, "Base", "Addon").unwrap();
        assert_eq!(list_installed(&mods).iter().map(|i| i.folder.as_str()).collect::<Vec<_>>(), ["Addon", "Base"]);
        assert!(compose(&mods, "Missing", "Addon").unwrap_err().contains("installed first"));
        assert!(compose(&mods, "../x", "Addon").is_err());
        let cat = vec![
            Mod { id: "base".into(), name: "Base".into(), description: String::new(), homepage: "https://example.com".into(), download_page: String::new(), path: "Base".into(), detect: vec![], notes: String::new(), args: vec![], addon_for: None },
            Mod { id: "add".into(), name: "Add".into(), description: String::new(), homepage: "https://example.com".into(), download_page: String::new(), path: "Addon".into(), detect: vec![], notes: String::new(), args: vec![], addon_for: Some("base".into()) },
        ];
        assert_eq!(with_bases(&cat, &["Addon".into()]), ["Base".to_string(), "Addon".to_string()]);
        let stacked = stack(&mods, &with_bases(&cat, &["Addon".into()])).unwrap().unwrap();
        assert_eq!(std::fs::read_to_string(stacked.join("same.big")).unwrap(), "addon");
        assert_eq!(std::fs::read_to_string(stacked.join("a.big")).unwrap(), "base-a");
        assert!(stack(&mods, &[]).unwrap().is_none());
        assert_eq!(stack(&mods, &["Base".into()]).unwrap().unwrap(), mods.join("Base"));
        assert!(list_installed(&mods).iter().all(|i| i.folder != ".run"));
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn folder_names_cannot_escape() {
        assert_eq!(sanitize_folder("../../etc"), Some("etc".into()));
        assert_eq!(sanitize_folder("Shock wave_1.2"), Some("Shock wave_1.2".into()));
        assert_eq!(sanitize_folder("..."), None);
        assert_eq!(sanitize_folder("/"), None);
        assert_eq!(sanitize_folder(""), None);
        assert_eq!(folder_for_archive(Path::new("/d/ShockWave 1.201.zip")).as_deref(), Some("ShockWave 1.201"));
        assert_eq!(folder_for_archive(Path::new("mod.tar.gz")).as_deref(), Some("mod"));
    }

    #[test]
    fn installs_a_folder_flattening_wrappers() {
        let t = temp("folder");
        let src = t.join("download/Mod_v1/Mod");
        std::fs::create_dir_all(src.join("Art")).unwrap();
        std::fs::write(src.join("!Mod.big"), vec![0u8; 2_000_000]).unwrap();
        std::fs::write(src.join("Art/more.BIG"), "x").unwrap();
        let mods = t.join("Mods");
        let got = install(&t.join("download"), &mods, "My Mod").unwrap();
        assert_eq!(got, Installed { folder: "My Mod".into(), bigs: 2, mb: 1 });
        assert!(mods.join("My Mod/!Mod.big").is_file() && mods.join("My Mod/Art/more.BIG").is_file());
        assert_eq!(list_installed(&mods), vec![got]);
        assert_eq!(std::fs::read_dir(&mods).unwrap().count(), 1, "no temp folders left behind");
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn installs_a_zip_and_replaces_an_older_version() {
        let t = temp("zip");
        let mods = t.join("Mods");
        let zip = t.join("m.zip");
        std::fs::write(&zip, make_zip(&[("Wrapper/a.big", b"BIGF1")])).unwrap();
        if extract::kind_of(&zip).is_none() {
            return;
        }
        match install(&zip, &mods, "M") {
            Err(e) if e.contains("no tool") => return, // no unzip-capable tool on this machine
            r => assert_eq!(r.unwrap().bigs, 1),
        }
        std::fs::write(&zip, make_zip(&[("a2.big", b"BIGF2"), ("b2.big", b"x")])).unwrap();
        assert_eq!(install(&zip, &mods, "M").unwrap().bigs, 2);
        assert!(!mods.join("M/a.big").exists(), "old files are gone");
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn refuses_things_without_game_files_and_cleans_up() {
        let t = temp("nobig");
        let src = t.join("src");
        std::fs::create_dir_all(&src).unwrap();
        std::fs::write(src.join("readme.txt"), "hi").unwrap();
        let mods = t.join("Mods");
        let e = install(&src, &mods, "X").unwrap_err();
        assert!(e.contains("No .big"), "{e}");
        assert!(list_installed(&mods).is_empty());
        assert_eq!(std::fs::read_dir(&mods).unwrap().count(), 0);
        assert!(install(&src, &mods, "...").is_err());
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn a_zip_holding_only_an_installer_gets_clear_guidance() {
        let t = temp("exezip");
        let src = t.join("src");
        std::fs::create_dir_all(&src).unwrap();
        std::fs::write(src.join("Setup.exe"), b"MZ").unwrap();
        std::fs::write(src.join("readme.txt"), b"hi").unwrap();
        let err = install(&src, &t.join("mods"), "Shockwave").unwrap_err();
        assert!(err.contains("Windows installer") && err.contains("empty folder"), "{err}");
        assert!(!t.join("mods").join("Shockwave").exists());
        assert!(std::fs::read_dir(t.join("mods")).unwrap().next().is_none(), "no temp folders left behind");
        let click = t.join("ShockWave.exe");
        std::fs::write(&click, crate::clickteam::sample_installer("Data\\m.big", b"BIGF-shock", "Shockwave.exe", b"MZ-nope")).unwrap();
        let got = install(&click, &t.join("mods"), "Shockwave").unwrap();
        assert_eq!(got.bigs, 1);
        assert_eq!(std::fs::read(t.join("mods/Shockwave/m.big")).unwrap(), b"BIGF-shock");
        assert!(!t.join("mods/Shockwave/Shockwave.exe").exists(), "the mod launcher is not installed");
        std::fs::remove_dir_all(t).unwrap();
    }

    #[cfg(unix)]
    #[test]
    fn refuses_a_folder_with_symlinks() {
        let t = temp("link");
        let src = t.join("src");
        std::fs::create_dir_all(&src).unwrap();
        std::fs::write(src.join("a.big"), "x").unwrap();
        std::os::unix::fs::symlink("/etc/passwd", src.join("evil")).unwrap();
        assert!(install(&src, &t.join("Mods"), "X").unwrap_err().contains("symbolic link"));
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn remove_only_touches_direct_children() {
        let t = temp("rm");
        let mods = t.join("Mods");
        std::fs::create_dir_all(mods.join("Keep")).unwrap();
        std::fs::write(mods.join("Keep/a.big"), "x").unwrap();
        std::fs::write(t.join("precious.txt"), "x").unwrap();
        for bad in ["../", "..", "../precious.txt", "Keep/../..", ""] {
            assert!(remove(&mods, bad).is_err(), "{bad:?}");
        }
        assert!(t.join("precious.txt").exists() && mods.join("Keep").exists());
        remove(&mods, "Keep").unwrap();
        assert!(!mods.join("Keep").exists());
        assert!(remove(&mods, "Keep").is_err());
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn spots_the_finished_download_in_the_downloads_folder() {
        let t = temp("dl");
        let before = SystemTime::now();
        let old = t.join("ShockWave_old.zip");
        std::fs::write(&old, "x").unwrap();
        let f = std::fs::File::options().write(true).open(&old).unwrap();
        f.set_modified(before - Duration::from_secs(3600)).unwrap();
        let detect = vec!["shockwave".to_string()];
        assert_eq!(find_download(&t, before, &detect), None, "older than the watch start");
        let new = t.join("ShockWave_1.201.zip");
        std::fs::write(&new, "x").unwrap();
        std::fs::write(t.join("ShockWave_1.201.zip.crdownload"), "x").unwrap();
        std::fs::write(t.join("unrelated.zip"), "x").unwrap();
        std::fs::write(t.join("ShockWave notes.txt"), "x").unwrap();
        // just written: not settled yet
        assert_eq!(find_download(&t, before, &detect), None);
        let f = std::fs::File::options().write(true).open(&new).unwrap();
        f.set_modified(SystemTime::now() - Duration::from_secs(10)).unwrap();
        assert_eq!(find_download(&t, before - Duration::from_secs(60), &detect), Some(new));
        std::fs::remove_dir_all(t).unwrap();
    }
}
