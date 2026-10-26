//! Unpacking downloads (zip, 7z, rar, tar, and installer .exe files) with the tools already on the machine, so the
//! launcher needs no archive library. Entries are listed and checked for path traversal before anything is written,
//! and symlinks in the result are rejected.
use std::path::{Component, Path, PathBuf};
use crate::proc::quiet;
use std::process::Command;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Zip,
    SevenZip,
    Rar,
    Tar,
    /// A Windows installer. Inno Setup and NSIS installers can often be unpacked without running them.
    Exe,
}

pub fn kind_of(path: &Path) -> Option<Kind> {
    let n = path.file_name()?.to_string_lossy().to_ascii_lowercase();
    let ends = |s: &[&str]| s.iter().any(|e| n.ends_with(e));
    if ends(&[".zip"]) {
        Some(Kind::Zip)
    } else if ends(&[".7z"]) {
        Some(Kind::SevenZip)
    } else if ends(&[".rar"]) {
        Some(Kind::Rar)
    } else if ends(&[".tar", ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2"]) {
        Some(Kind::Tar)
    } else if ends(&[".exe"]) {
        Some(Kind::Exe)
    } else {
        None
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum Tool {
    /// `tar` that can also read zip/7z/rar (macOS and Windows ship bsdtar; Linux usually has GNU tar).
    BsdTar,
    /// Plain `tar` for .tar.* archives.
    Tar,
    Unzip,
    SevenZip(PathBuf),
    Innoextract,
    Unrar,
}

fn find_in_path(name: &str) -> Option<PathBuf> {
    let exe = if cfg!(windows) { format!("{name}.exe") } else { name.to_string() };
    std::env::var_os("PATH").into_iter().flat_map(|p| std::env::split_paths(&p).collect::<Vec<_>>()).map(|d| d.join(&exe)).find(|p| p.is_file())
}

fn seven_zip() -> Option<PathBuf> {
    ["7zz", "7z", "7za", "7zr"]
        .iter()
        .find_map(|n| find_in_path(n))
        .or_else(|| ["C:\\Program Files\\7-Zip\\7z.exe", "C:\\Program Files (x86)\\7-Zip\\7z.exe"].iter().map(PathBuf::from).find(|p| p.is_file()))
}

fn is_bsdtar() -> bool {
    quiet(&mut Command::new("tar")).arg("--version").output().map(|o| String::from_utf8_lossy(&o.stdout).to_ascii_lowercase().contains("bsdtar")).unwrap_or(false)
}

/// Tools that can handle `kind`, best first, only those present.
fn candidates(kind: Kind) -> Vec<Tool> {
    let mut v = vec![];
    match kind {
        Kind::Zip => {
            if is_bsdtar() {
                v.push(Tool::BsdTar);
            }
            if find_in_path("unzip").is_some() {
                v.push(Tool::Unzip);
            }
            if let Some(p) = seven_zip() {
                v.push(Tool::SevenZip(p));
            }
        }
        Kind::SevenZip => {
            if let Some(p) = seven_zip() {
                v.push(Tool::SevenZip(p));
            }
            if is_bsdtar() {
                v.push(Tool::BsdTar);
            }
        }
        Kind::Rar => {
            if find_in_path("unrar").is_some() {
                v.push(Tool::Unrar);
            }
            if let Some(p) = seven_zip() {
                v.push(Tool::SevenZip(p));
            }
            if is_bsdtar() {
                v.push(Tool::BsdTar);
            }
        }
        Kind::Tar => {
            if find_in_path("tar").is_some() {
                v.push(Tool::Tar);
            }
        }
        Kind::Exe => {
            if find_in_path("innoextract").is_some() {
                v.push(Tool::Innoextract);
            }
            if let Some(p) = seven_zip() {
                v.push(Tool::SevenZip(p));
            }
        }
    }
    v
}

fn install_hint(kind: Kind) -> &'static str {
    let (mac, linux, win) = match kind {
        Kind::Exe => ("brew install innoextract sevenzip", "sudo apt install innoextract p7zip-full", "install 7-Zip from 7-zip.org"),
        Kind::Rar => ("brew install sevenzip", "sudo apt install p7zip-full", "install 7-Zip from 7-zip.org"),
        _ => ("brew install sevenzip", "sudo apt install unzip p7zip-full", "install 7-Zip from 7-zip.org"),
    };
    if cfg!(target_os = "macos") { mac } else if cfg!(windows) { win } else { linux }
}

/// `true` for names that could write outside the destination.
pub fn unsafe_entry(name: &str) -> bool {
    let n = name.replace('\\', "/");
    n.starts_with('/') || n.split('/').any(|c| c == "..") || n.as_bytes().get(1) == Some(&b':') || Path::new(&n).components().any(|c| matches!(c, Component::Prefix(_) | Component::RootDir))
}

fn out(cmd: &mut Command) -> Result<String, String> {
    let o = quiet(cmd).output().map_err(|e| format!("couldn't run {:?}: {e}", cmd.get_program()))?;
    if o.status.success() {
        Ok(String::from_utf8_lossy(&o.stdout).into_owned())
    } else {
        Err(format!("{:?} failed: {}", cmd.get_program(), String::from_utf8_lossy(&o.stderr).lines().last().unwrap_or("").trim()))
    }
}

fn list(tool: &Tool, archive: &Path) -> Result<Option<Vec<String>>, String> {
    let lines = |s: String| s.lines().map(str::trim).filter(|l| !l.is_empty()).map(String::from).collect::<Vec<_>>();
    Ok(match tool {
        Tool::BsdTar | Tool::Tar => Some(lines(out(Command::new("tar").arg("-tf").arg(archive))?)),
        Tool::Unzip => Some(lines(out(Command::new("unzip").arg("-Z1").arg(archive))?)),
        Tool::Unrar => Some(lines(out(Command::new("unrar").arg("lb").arg(archive))?)),
        Tool::SevenZip(p) => {
            let text = out(Command::new(p).args(["l", "-slt", "-ba"]).arg(archive))?;
            Some(text.lines().filter_map(|l| l.strip_prefix("Path = ")).map(String::from).collect())
        }
        Tool::Innoextract => None, // sanitises paths itself; the result is still checked afterwards
    })
}

/// The first entry of a verbose listing that is a symbolic or hard link, if any. Extracting a link and then a file
/// "inside" it writes outside the destination before any check of the finished tree could notice, so links are
/// refused up front. `kind` says which tool's listing format `text` is in.
fn find_link(tool: &Tool, text: &str) -> Option<String> {
    let unix_mode = |t: &str| t.len() >= 10 && t.is_char_boundary(10) && t[1..10].chars().all(|c| "rwxsStT-".contains(c));
    match tool {
        // `tar -tv`: `lrwxr-xr-x ... name -> target` for symlinks, `hrw-r--r-- ... link to ...` for hard links.
        Tool::BsdTar | Tool::Tar => text.lines().find(|l| (l.starts_with('l') || l.starts_with('h')) && unix_mode(l)).map(|l| l.trim().to_string()),
        // `unzip -Z`: zipinfo's long format starts with the Unix mode.
        Tool::Unzip => text.lines().find(|l| l.starts_with('l') && unix_mode(l)).map(|l| l.trim().to_string()),
        // `7z l -slt`: blocks of `Key = value`; links carry a Symbolic Link / Hard Link field or a Unix mode starting with `l`.
        Tool::SevenZip(_) => {
            let mut path = String::new();
            for l in text.lines() {
                if let Some(p) = l.strip_prefix("Path = ") {
                    path = p.to_string();
                } else if l.strip_prefix("Symbolic Link = ").or_else(|| l.strip_prefix("Hard Link = ")).is_some_and(|v| !v.trim().is_empty()) {
                    return Some(path);
                } else if let Some(a) = l.strip_prefix("Attributes = ") {
                    if a.split_whitespace().any(|t| t.starts_with('l') && unix_mode(t)) {
                        return Some(path);
                    }
                }
            }
            None
        }
        // `unrar lt`: `Type: Unix symbolic link` (and the like).
        Tool::Unrar => text.lines().find(|l| l.trim_start().to_ascii_lowercase().starts_with("type:") && l.to_ascii_lowercase().contains("link")).map(|l| l.trim().to_string()),
        Tool::Innoextract => None,
    }
}

fn links_in(tool: &Tool, archive: &Path) -> Result<Option<String>, String> {
    let text = match tool {
        Tool::BsdTar | Tool::Tar => out(Command::new("tar").arg("-tvf").arg(archive))?,
        Tool::Unzip => out(Command::new("unzip").arg("-Z").arg(archive))?,
        Tool::SevenZip(p) => out(Command::new(p).args(["l", "-slt", "-ba"]).arg(archive))?,
        Tool::Unrar => out(Command::new("unrar").arg("lt").arg(archive))?,
        Tool::Innoextract => return Ok(None),
    };
    Ok(find_link(tool, &text))
}

fn run_extract(tool: &Tool, archive: &Path, dest: &Path) -> Result<(), String> {
    let mut cmd = match tool {
        Tool::BsdTar | Tool::Tar => {
            let mut c = Command::new("tar");
            c.arg("-xf").arg(archive).arg("-C").arg(dest);
            c
        }
        Tool::Unzip => {
            let mut c = Command::new("unzip");
            c.args(["-o", "-q"]).arg(archive).arg("-d").arg(dest);
            c
        }
        Tool::Unrar => {
            let mut c = Command::new("unrar");
            c.args(["x", "-y"]).arg(archive).arg(format!("{}/", dest.display()));
            c
        }
        Tool::SevenZip(p) => {
            let mut c = Command::new(p);
            c.args(["x", "-y"]).arg(format!("-o{}", dest.display())).arg(archive);
            c
        }
        Tool::Innoextract => {
            let mut c = Command::new("innoextract");
            c.arg("--silent").arg("-d").arg(dest).arg(archive);
            c
        }
    };
    out(&mut cmd).map(|_| ())
}

/// Archives made on other systems can carry read-only folders and files; left that way they cannot be moved or removed
/// (and a copy keeps the bits). The owner gets read and write on everything that was unpacked.
#[cfg(unix)]
fn make_writable(root: &Path) {
    use std::os::unix::fs::PermissionsExt;
    let Ok(rd) = std::fs::read_dir(root) else { return };
    let _ = std::fs::set_permissions(root, std::fs::Permissions::from_mode(0o755));
    for e in rd.flatten() {
        let Ok(ty) = e.file_type() else { continue };
        if ty.is_dir() {
            make_writable(&e.path());
        } else if ty.is_file() {
            let mode = e.metadata().map(|m| m.permissions().mode()).unwrap_or(0o644);
            let _ = std::fs::set_permissions(e.path(), std::fs::Permissions::from_mode(mode | 0o600));
        }
    }
}
#[cfg(not(unix))]
fn make_writable(_root: &Path) {}

/// Reject symlinks and anything that resolves outside `root`.
fn check_tree(root: &Path) -> Result<(), String> {
    let root = root.canonicalize().map_err(|e| e.to_string())?;
    let mut stack = vec![root.clone()];
    while let Some(d) = stack.pop() {
        for e in std::fs::read_dir(&d).map_err(|e| e.to_string())? {
            let e = e.map_err(|e| e.to_string())?;
            let ty = e.file_type().map_err(|e| e.to_string())?;
            if ty.is_symlink() {
                return Err("the archive contains a symbolic link, which isn't allowed".into());
            }
            let p = e.path().canonicalize().map_err(|e| e.to_string())?;
            if !p.starts_with(&root) {
                return Err("the archive tried to write outside its folder".into());
            }
            if ty.is_dir() {
                stack.push(p);
            }
        }
    }
    Ok(())
}

/// Unpack `archive` into `dest` (which must exist and be empty-ish). Safe against path traversal.
pub fn extract(archive: &Path, dest: &Path) -> Result<(), String> {
    let kind = kind_of(archive).ok_or("that file type isn't supported (use .zip, .7z, .rar, .tar.gz or an installer .exe)")?;
    let tools = candidates(kind);
    if tools.is_empty() {
        return Err(format!("no tool to unpack this file was found; try: {}", install_hint(kind)));
    }
    let mut last = String::new();
    for tool in tools {
        let attempt = (|| {
            if let Some(entries) = list(&tool, archive)? {
                if let Some(bad) = entries.iter().find(|e| unsafe_entry(e)) {
                    return Err(format!("the archive contains an unsafe path ({bad:?}); refusing to unpack it"));
                }
            }
            if let Some(link) = links_in(&tool, archive)? {
                return Err(format!("the archive contains a symbolic link ({link}), which isn't allowed; refusing to unpack it"));
            }
            run_extract(&tool, archive, dest)?;
            make_writable(dest);
            check_tree(dest)
        })();
        match attempt {
            Ok(()) => return Ok(()),
            // an unsafe archive is final; a tool that merely failed lets the next candidate try
            Err(e) if e.contains("unsafe path") || e.contains("symbolic link") || e.contains("outside") => return Err(e),
            Err(e) => last = e,
        }
    }
    Err(if kind == Kind::Exe { format!("couldn't unpack this installer ({last}). See the README section on mods that come as an .exe") } else { last })
}

/// SHA-256 of a file, for pinned downloads.
pub fn sha256_file(path: &Path) -> std::io::Result<[u8; 32]> {
    Ok(generals_proto::sha256(&[&std::fs::read(path)?]))
}

pub fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    /// Folders and files that arrive read-only (zips made on other systems) are made writable, so they can be moved and removed.
    #[cfg(unix)]
    #[test]
    fn unpacked_read_only_trees_become_removable() {
        use std::os::unix::fs::PermissionsExt;
        let t = std::env::temp_dir().join(format!("gen-ro-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        std::fs::create_dir_all(t.join("a/b")).unwrap();
        std::fs::write(t.join("a/b/f.txt"), "x").unwrap();
        std::fs::set_permissions(t.join("a/b/f.txt"), std::fs::Permissions::from_mode(0o444)).unwrap();
        std::fs::set_permissions(t.join("a/b"), std::fs::Permissions::from_mode(0o555)).unwrap();
        std::fs::set_permissions(t.join("a"), std::fs::Permissions::from_mode(0o555)).unwrap();
        make_writable(&t);
        std::fs::remove_dir_all(&t).unwrap();
    }

    #[test]
    fn spots_links_in_each_tools_listing() {
        let tar = "-rw-r--r--  0 u g  5 Jan  1 00:00 a.txt\nlrwxr-xr-x  0 u g  0 Jan  1 00:00 link -> /Users/me\n";
        assert!(find_link(&Tool::BsdTar, tar).unwrap().contains("link -> /Users/me"));
        assert!(find_link(&Tool::Tar, "hrw-r--r-- 0 u/g 0 2026-01-01 00:00 b link to a\n").is_some());
        assert!(find_link(&Tool::Tar, "-rw-r--r--  0 u g  5 Jan  1 00:00 a.txt\ndrwxr-xr-x  0 u g  0 Jan  1 00:00 dir/\n").is_none());
        assert!(find_link(&Tool::Unzip, "Archive:  x.zip\n-rw-r--r--  3.0 unx  5 tx stor 26-Jan-01 00:00 a\nlrwxr-xr-x  3.0 unx  8 bx stor 26-Jan-01 00:00 link\n").is_some());
        assert!(find_link(&Tool::Unzip, "Archive:  x.zip\n-rw-r--r--  3.0 unx  5 tx stor 26-Jan-01 00:00 a\n").is_none());
        let sz = std::path::PathBuf::from("7z");
        assert_eq!(find_link(&Tool::SevenZip(sz.clone()), "Path = a\nAttributes = A_ -rw-r--r--\n\nPath = link\nAttributes = _ lrwxrwxrwx\n").as_deref(), Some("link"));
        assert_eq!(find_link(&Tool::SevenZip(sz.clone()), "Path = x\nSymbolic Link = /etc\n").as_deref(), Some("x"));
        assert!(find_link(&Tool::SevenZip(sz), "Path = a\nAttributes = A_ -rw-r--r--\nSymbolic Link = \n").is_none());
        assert!(find_link(&Tool::Unrar, "Name: l\n Type: Unix symbolic link\n").is_some());
        assert!(find_link(&Tool::Unrar, "Name: f\n Type: File\n").is_none());
    }

    /// A tar holding a symlink is refused before anything is written (the old order extracted it first).
    #[cfg(unix)]
    #[test]
    fn a_symlink_is_refused_before_anything_is_extracted() {
        if find_in_path("tar").is_none() {
            return;
        }
        let t = std::env::temp_dir().join(format!("gen-link-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        std::fs::create_dir_all(t.join("src")).unwrap();
        std::fs::create_dir_all(t.join("dest")).unwrap();
        std::os::unix::fs::symlink("/", t.join("src/escape")).unwrap();
        std::fs::write(t.join("src/real.txt"), "x").unwrap();
        let tar = t.join("evil.tar");
        assert!(Command::new("tar").arg("-cf").arg(&tar).arg("-C").arg(t.join("src")).args(["escape", "real.txt"]).status().unwrap().success());
        let err = extract(&tar, &t.join("dest")).unwrap_err();
        assert!(err.contains("symbolic link"), "{err}");
        assert_eq!(std::fs::read_dir(t.join("dest")).unwrap().count(), 0, "nothing was unpacked");
        let _ = std::fs::remove_dir_all(&t);
    }

    fn crc32(data: &[u8]) -> u32 {
        let mut crc = !0u32;
        for &b in data {
            crc ^= b as u32;
            for _ in 0..8 {
                crc = if crc & 1 != 0 { (crc >> 1) ^ 0xEDB8_8320 } else { crc >> 1 };
            }
        }
        !crc
    }

    /// A minimal stored (uncompressed) zip, built by hand so tests need no zip tool or library.
    pub(crate) fn make_zip(entries: &[(&str, &[u8])]) -> Vec<u8> {
        let (mut out, mut central) = (vec![], vec![]);
        for (name, data) in entries {
            let off = out.len() as u32;
            let (crc, n) = (crc32(data), name.as_bytes());
            out.extend([0x50, 0x4b, 3, 4, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
            out.extend(crc.to_le_bytes());
            out.extend((data.len() as u32).to_le_bytes());
            out.extend((data.len() as u32).to_le_bytes());
            out.extend((n.len() as u16).to_le_bytes());
            out.extend([0, 0]);
            out.extend(n);
            out.extend(*data);
            central.extend([0x50, 0x4b, 1, 2, 20, 0, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
            central.extend(crc.to_le_bytes());
            central.extend((data.len() as u32).to_le_bytes());
            central.extend((data.len() as u32).to_le_bytes());
            central.extend((n.len() as u16).to_le_bytes());
            central.extend([0u8; 12]);
            central.extend(off.to_le_bytes());
            central.extend(n);
        }
        let (cd_off, cd_len) = (out.len() as u32, central.len() as u32);
        out.extend(central);
        out.extend([0x50, 0x4b, 5, 6, 0, 0, 0, 0]);
        out.extend((entries.len() as u16).to_le_bytes());
        out.extend((entries.len() as u16).to_le_bytes());
        out.extend(cd_len.to_le_bytes());
        out.extend(cd_off.to_le_bytes());
        out.extend([0, 0]);
        out
    }

    fn temp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("gen-extract-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn recognises_archive_kinds() {
        for (n, k) in [("a.ZIP", Kind::Zip), ("a.7z", Kind::SevenZip), ("a.rar", Kind::Rar), ("a.tar.gz", Kind::Tar), ("Setup.EXE", Kind::Exe)] {
            assert_eq!(kind_of(Path::new(n)), Some(k), "{n}");
        }
        assert_eq!(kind_of(Path::new("a.txt")), None);
    }

    #[test]
    fn spots_unsafe_entry_names() {
        for bad in ["../x", "a/../../x", "/etc/passwd", "C:\\x", "a\\..\\b", "C:/x"] {
            assert!(unsafe_entry(bad), "{bad}");
        }
        for ok in ["a/b.big", "Mod/Data/x.ini", "a..b/c", "..hidden"] {
            assert!(!unsafe_entry(ok), "{ok}");
        }
    }

    #[test]
    fn extracts_a_zip() {
        let d = temp("ok");
        let zip = d.join("m.zip");
        std::fs::write(&zip, make_zip(&[("Mod/a.big", b"BIGF"), ("Mod/Data/x.ini", b"x")])).unwrap();
        let out = d.join("out");
        std::fs::create_dir(&out).unwrap();
        if candidates(Kind::Zip).is_empty() {
            return; // no unzip-capable tool on this machine
        }
        extract(&zip, &out).unwrap();
        assert_eq!(std::fs::read(out.join("Mod/a.big")).unwrap(), b"BIGF");
        assert!(out.join("Mod/Data/x.ini").is_file());
        std::fs::remove_dir_all(d).unwrap();
    }

    #[test]
    fn refuses_a_zip_that_escapes_its_folder() {
        let d = temp("slip");
        let zip = d.join("evil.zip");
        std::fs::write(&zip, make_zip(&[("../evil.txt", b"pwned"), ("ok.big", b"x")])).unwrap();
        let out = d.join("out");
        std::fs::create_dir(&out).unwrap();
        if candidates(Kind::Zip).is_empty() {
            return;
        }
        let e = extract(&zip, &out).unwrap_err();
        assert!(e.contains("unsafe path") || e.contains("failed"), "{e}");
        assert!(!d.join("evil.txt").exists(), "nothing may be written outside the destination");
        std::fs::remove_dir_all(d).unwrap();
    }

    #[test]
    fn rejects_unknown_types_and_hashes_files() {
        assert!(extract(Path::new("x.txt"), Path::new(".")).unwrap_err().contains("isn't supported"));
        let d = temp("hash");
        std::fs::write(d.join("f"), "abc").unwrap();
        assert_eq!(hex(&sha256_file(&d.join("f")).unwrap()), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        std::fs::remove_dir_all(d).unwrap();
    }
}
