//! Clickteam Install Creator archives (overlay signature `wwgT)H`).
//!
//! Shockwave ships as one of these. The installer also contains Shockwave's own Windows launcher.
//! That program is not written out: Rallypoint starts the mod's `.big` / `.gib` archives with its engine.

use bzip2::read::BzDecoder;
use flate2::read::DeflateDecoder;
use std::io::Read;
use std::path::{Path, PathBuf};

const SIGNATURE: &[u8] = b"wwgT)H";
const FILE_LIST: u16 = 0x143A;
const FILE_DATA: u16 = 0x7F7F;

struct Rd<'a> {
    d: &'a [u8],
    p: usize,
}

impl<'a> Rd<'a> {
    fn rest(&self) -> usize {
        self.d.len().saturating_sub(self.p)
    }
    fn u8(&mut self) -> Option<u8> {
        let b = *self.d.get(self.p)?;
        self.p += 1;
        Some(b)
    }
    fn u16(&mut self) -> Option<u16> {
        let b = self.d.get(self.p..self.p + 2)?;
        self.p += 2;
        Some(u16::from_le_bytes([b[0], b[1]]))
    }
    fn u32(&mut self) -> Option<u32> {
        let b = self.d.get(self.p..self.p + 4)?;
        self.p += 4;
        Some(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
    }
    fn skip(&mut self, n: usize) -> Option<()> {
        if self.rest() < n {
            None
        } else {
            self.p += n;
            Some(())
        }
    }
}

struct Entry {
    offset: u32,
    compressed: u32,
    uncompressed: u32,
    path: String,
}

fn bunzip(payload: &[u8], expected: usize) -> Result<Vec<u8>, String> {
    let mut out = vec![0u8; expected];
    let mut got = 0;
    let mut dec = BzDecoder::new(payload);
    while got < expected {
        match dec.read(&mut out[got..]) {
            Ok(0) => break,
            Ok(n) => got += n,
            Err(e) => return Err(format!("couldn't decompress the installer ({e})")),
        }
    }
    if got != expected {
        return Err(format!("installer block decompressed to {got} bytes, expected {expected}"));
    }
    Ok(out)
}

fn inflate(payload: &[u8], expected: usize) -> Result<Vec<u8>, String> {
    let raw = if payload.len() >= 2 && payload[0] == 0x78 { &payload[2..] } else { payload };
    let mut out = Vec::new();
    DeflateDecoder::new(raw).read_to_end(&mut out).map_err(|e| format!("couldn't decompress the installer ({e})"))?;
    if expected > 0 && out.len() > expected {
        out.truncate(expected);
    }
    if expected > 0 && out.len() != expected {
        return Err(format!("installer block decompressed to {} bytes, expected {expected}", out.len()));
    }
    Ok(out)
}

fn decompress_block(block: &[u8]) -> Result<Vec<u8>, String> {
    let mut r = Rd { d: block, p: 0 };
    let expected = r.u32().ok_or("truncated installer block")? as usize;
    let method = r.u8().ok_or("truncated installer block")?;
    let payload = &block[r.p..];
    match method {
        0 => {
            if payload.len() < expected {
                return Err("uncompressed installer block is short".into());
            }
            Ok(payload[..expected].to_vec())
        }
        1 => inflate(payload, expected),
        2 => bunzip(payload, expected),
        _ => Err("this installer uses a compression Rallypoint can't read".into()),
    }
}

/// A path inside the archive that stays under the destination. Installer launchers are dropped.
fn rel_path(raw: &str) -> Option<PathBuf> {
    let raw = raw.trim().trim_matches('\0');
    let raw = raw.strip_prefix('\\').or_else(|| raw.strip_prefix('/')).unwrap_or(raw);
    let raw = if raw.len() >= 2 && raw.as_bytes()[1] == b':' { raw[2..].trim_start_matches(['\\', '/']) } else { raw };
    if raw.is_empty() {
        return None;
    }
    let mut out = PathBuf::new();
    for c in raw.split(['\\', '/']) {
        if c.is_empty() || c == "." {
            continue;
        }
        if c == ".." || c.contains(':') {
            return None;
        }
        out.push(c);
    }
    let name = out.file_name()?.to_string_lossy();
    let ext = name.rsplit('.').next().unwrap_or("").to_ascii_lowercase();
    // Shockwave's launcher (and any other program in the installer) is a Windows executable.
    if matches!(ext.as_str(), "exe" | "dll" | "bat" | "cmd" | "com" | "scr" | "lnk" | "pif" | "msi") {
        return None;
    }
    (!out.as_os_str().is_empty()).then_some(out)
}

fn parse_entry(r: &mut Rd, wide_size: bool) -> Option<Option<Entry>> {
    let start = r.p;
    let node_size = if wide_size { r.u32()? as usize } else { r.u16()? as usize };
    let typ = r.u16()?;
    let end = start.checked_add(node_size)?;
    if end > r.d.len() || end < r.p {
        return None;
    }
    if typ != 0 {
        r.p = end;
        return Some(None);
    }
    r.skip(3)?;
    let flag = r.u8()?;
    let (offset, compressed, uncompressed) = if flag == 0xE2 {
        r.skip(30)?;
        (0, 0, 0)
    } else if wide_size {
        r.skip(14)?;
        let uncompressed = r.u32()?;
        let offset = r.u32()?;
        let compressed = r.u32()?;
        r.skip(4)?;
        r.skip(24)?;
        (offset, compressed, uncompressed)
    } else {
        return None;
    };
    if r.p > end {
        return None;
    }
    let bytes = &r.d[r.p..end];
    r.p = end;
    let n = bytes.iter().position(|b| *b == 0).unwrap_or(bytes.len());
    let path = String::from_utf8_lossy(&bytes[..n]).into_owned();
    Some(Some(Entry { offset, compressed, uncompressed, path }))
}

fn parse_list_v24(list: &[u8]) -> Option<Vec<Entry>> {
    let mut r = Rd { d: list, p: 0 };
    let count = r.u16()? as usize;
    r.skip(2)?;
    let mut out = vec![];
    for _ in 0..count {
        let start = r.p;
        let node_size = r.u16()? as usize;
        let typ = r.u16()?;
        let end = start.checked_add(node_size)?;
        if end > list.len() {
            return None;
        }
        if typ != 0 {
            r.p = end;
            continue;
        }
        r.skip(2)?;
        let offset = r.u32()?;
        let compressed = r.u32()?;
        let _unknown = r.u32()?;
        let uncompressed = r.u32()?;
        r.skip(16 + 24)?;
        if r.p > end {
            return None;
        }
        let bytes = &list[r.p..end];
        r.p = end;
        let n = bytes.iter().position(|b| *b == 0).unwrap_or(bytes.len());
        out.push(Entry { offset, compressed, uncompressed, path: String::from_utf8_lossy(&bytes[..n]).into_owned() });
    }
    Some(out)
}

fn parse_list(list: &[u8], wide: bool) -> Option<Vec<Entry>> {
    let mut r = Rd { d: list, p: 0 };
    let count = r.u16()? as usize;
    r.skip(2)?;
    let mut out = vec![];
    for _ in 0..count {
        match parse_entry(&mut r, wide)? {
            Some(e) => out.push(e),
            None => {}
        }
    }
    Some(out)
}

fn score(entries: &[Entry], data_len: u64) -> usize {
    let ok = !entries.is_empty()
        && entries.iter().all(|e| {
            e.offset as u64 <= data_len
                && e.compressed < 2_000_000_000
                && !(e.uncompressed > 10 && e.compressed > 10 && e.compressed > e.uncompressed.saturating_mul(5))
                && !e.path.chars().any(|c| c.is_control() || matches!(c, '<' | '>' | '"' | '|' | '?' | '*'))
        });
    if !ok {
        return 0;
    }
    let bigs = entries.iter().filter(|e| {
        let p = e.path.to_ascii_lowercase();
        p.ends_with(".big") || p.ends_with(".gib")
    }).count();
    if bigs == 0 {
        return 0;
    }
    bigs.saturating_mul(1000).saturating_add(entries.len())
}

/// v35: u32 node size. The size fields are present even for the short `0xE2` nodes.
fn parse_list_v35(list: &[u8]) -> Option<Vec<Entry>> {
    let mut r = Rd { d: list, p: 0 };
    let count = r.u16()? as usize;
    r.skip(2)?;
    let mut out = vec![];
    for _ in 0..count {
        let start = r.p;
        let node_size = r.u32()? as usize;
        let typ = r.u16()?;
        let end = start.checked_add(node_size)?;
        if end > list.len() {
            return None;
        }
        if typ != 0 {
            r.p = end;
            continue;
        }
        r.skip(3)?;
        let flag = r.u8()?;
        r.skip(if flag == 0xE2 { 30 } else { 14 })?;
        let uncompressed = r.u32()?;
        let offset = r.u32()?;
        let compressed = r.u32()?;
        r.skip(4 + 4 + 2 + 24)?;
        if r.p > end {
            return None;
        }
        out.push(Entry { offset, compressed, uncompressed, path: path_at(&list[r.p..end]) });
        r.p = end;
    }
    Some(out)
}

/// v30: u16 node size, an index field, then the path.
fn parse_list_v30(list: &[u8]) -> Option<Vec<Entry>> {
    let mut r = Rd { d: list, p: 0 };
    let count = r.u16()? as usize;
    r.skip(2)?;
    let mut out = vec![];
    for _ in 0..count {
        let start = r.p;
        let node_size = r.u16()? as usize;
        let typ = r.u16()?;
        let end = start.checked_add(node_size)?;
        if end > list.len() {
            return None;
        }
        if typ != 0 {
            r.p = end;
            continue;
        }
        r.skip(2)?;
        let offset = r.u32()?;
        let compressed = r.u32()?;
        r.skip(4)?;
        let uncompressed = r.u32()?;
        r.skip(18 + 4 + 24)?;
        if r.p > end {
            return None;
        }
        out.push(Entry { offset, compressed, uncompressed, path: path_at(&list[r.p..end]) });
        r.p = end;
    }
    Some(out)
}

/// v20: u32 node size, then sizes and three timestamps.
fn parse_list_v20(list: &[u8]) -> Option<Vec<Entry>> {
    let mut r = Rd { d: list, p: 0 };
    let count = r.u16()? as usize;
    r.skip(2)?;
    let mut out = vec![];
    for _ in 0..count {
        let start = r.p;
        let node_size = r.u32()? as usize;
        let typ = r.u16()?;
        let end = start.checked_add(node_size)?;
        if end > list.len() {
            return None;
        }
        if typ != 0 {
            r.p = end;
            continue;
        }
        r.skip(14)?;
        let uncompressed = r.u32()?;
        let offset = r.u32()?;
        let compressed = r.u32()?;
        r.skip(24)?;
        if r.p > end {
            return None;
        }
        out.push(Entry { offset, compressed, uncompressed, path: path_at(&list[r.p..end]) });
        r.p = end;
    }
    Some(out)
}

fn path_at(bytes: &[u8]) -> String {
    let n = bytes.iter().position(|b| *b == 0).unwrap_or(bytes.len());
    String::from_utf8_lossy(&bytes[..n]).into_owned()
}

fn file_list(data: &[u8], at: usize) -> Result<Vec<Entry>, String> {
    let mut best: Option<Vec<Entry>> = None;
    let mut r = Rd { d: data, p: at };
    let file_len = data.len() as u64;
    while r.rest() >= 8 {
        let id = r.u16().unwrap();
        let _unknown = r.u16().unwrap();
        let size = r.u32().unwrap() as usize;
        if r.rest() < size {
            break;
        }
        let content = &data[r.p..r.p + size];
        if id == FILE_DATA {
            break;
        }
        if id == FILE_LIST {
            let list = decompress_block(content)?;
            for parsed in [parse_list(&list, true), parse_list_v35(&list), parse_list_v30(&list), parse_list_v24(&list), parse_list_v20(&list)] {
                if let Some(entries) = parsed {
                    let s = score(&entries, file_len);
                    if s > 0 && best.as_ref().map(|b| s > score(b, file_len)).unwrap_or(true) {
                        best = Some(entries);
                    }
                }
            }
        }
        r.p += size;
    }
    best.ok_or_else(|| "this Clickteam installer has no file list Rallypoint could read".into())
}

fn data_at(data: &[u8], at: usize) -> Option<usize> {
    let mut r = Rd { d: data, p: at };
    while r.rest() >= 8 {
        let id = r.u16()?;
        let _unknown = r.u16()?;
        let size = r.u32()? as usize;
        if id == FILE_DATA {
            return Some(r.p);
        }
        if r.rest() < size {
            return None;
        }
        r.p += size;
    }
    None
}

fn write_entry(data: &[u8], data_at: usize, e: &Entry, dest: &Path) -> Result<(), String> {
    let Some(rel) = rel_path(&e.path) else { return Ok(()) };
    let to = dest.join(&rel);
    if let Some(parent) = to.parent() {
        std::fs::create_dir_all(parent).map_err(|err| err.to_string())?;
    }
    if e.uncompressed == 0 {
        return Ok(());
    }
    let pos = data_at + e.offset as usize + 4;
    let bytes = data.get(pos..).ok_or_else(|| format!("{} points past the end of the installer", e.path))?;
    let mut r = Rd { d: bytes, p: 0 };
    let method = r.u8().ok_or_else(|| format!("{} is truncated", e.path))?;
    let payload = bytes.get(r.p..).unwrap_or(&[]);
    let plain = match method {
        0 => payload.get(..e.uncompressed as usize).ok_or_else(|| format!("{} is truncated", e.path))?.to_vec(),
        1 => inflate(payload, e.uncompressed as usize)?,
        2 => bunzip(payload, e.uncompressed as usize)?,
        _ => return Err(format!("{} uses a compression Rallypoint can't read", e.path)),
    };
    std::fs::write(&to, plain).map_err(|err| err.to_string())?;
    Ok(())
}

pub fn looks_like(path: &Path) -> bool {
    let Ok(f) = std::fs::File::open(path) else { return false };
    let mut r = std::io::BufReader::new(f);
    let mut window = vec![0u8; 64 * 1024];
    let mut prev = Vec::new();
    loop {
        let n = match std::io::Read::read(&mut r, &mut window) {
            Ok(0) => break,
            Ok(n) => n,
            Err(_) => return false,
        };
        let mut hay = prev.clone();
        hay.extend_from_slice(&window[..n]);
        if hay.windows(SIGNATURE.len()).any(|w| w == SIGNATURE) {
            return true;
        }
        prev = hay[hay.len().saturating_sub(SIGNATURE.len())..].to_vec();
    }
    false
}

/// Unpack game data from a Clickteam installer. Programs inside it (the mod launcher) are not written.
pub fn extract_into(exe: &Path, dest: &Path) -> Result<(), String> {
    let data = std::fs::read(exe).map_err(|e| e.to_string())?;
    let at = data.windows(SIGNATURE.len()).position(|w| w == SIGNATURE).ok_or("not a clickteam installer")? + SIGNATURE.len();
    let entries = file_list(&data, at)?;
    let data_at = data_at(&data, at).ok_or("the installer has a file list but no file data")?;
    std::fs::create_dir_all(dest).map_err(|e| e.to_string())?;
    let mut wrote = 0usize;
    for e in &entries {
        let before = rel_path(&e.path).is_some();
        write_entry(&data, data_at, e, dest)?;
        wrote += before as usize;
    }
    if wrote == 0 {
        return Err("the installer only contained a Windows launcher, which Rallypoint does not run".into());
    }
    Ok(())
}

/// If `dir` holds a Clickteam installer, unpack it beside those files and return that folder.
pub fn unpack_tree(dir: &Path) -> Result<Option<PathBuf>, String> {
    let mut exes = vec![];
    let mut stack = vec![dir.to_path_buf()];
    while let Some(d) = stack.pop() {
        for e in std::fs::read_dir(&d).into_iter().flatten().flatten() {
            let p = e.path();
            match e.file_type() {
                Ok(t) if t.is_dir() => stack.push(p),
                Ok(t) if t.is_file() && p.extension().map(|x| x.eq_ignore_ascii_case("exe")).unwrap_or(false) && looks_like(&p) => exes.push(p),
                _ => {}
            }
        }
    }
    if exes.is_empty() {
        return Ok(None);
    }
    let out = dir.join("unpacked");
    let _ = std::fs::remove_dir_all(&out);
    std::fs::create_dir_all(&out).map_err(|e| e.to_string())?;
    for exe in &exes {
        extract_into(exe, &out)?;
    }
    Ok(Some(out))
}

#[cfg(test)]
pub(crate) fn sample_installer(big_name: &str, big: &[u8], launcher_name: &str, launcher: &[u8]) -> Vec<u8> {
    fn node(path: &str, offset: u32, compressed: u32, uncompressed: u32) -> Vec<u8> {
        let mut path_bytes = path.as_bytes().to_vec();
        path_bytes.extend_from_slice(&[0u8; 9]);
        let node_size = (64 + path_bytes.len()) as u32;
        let mut n = vec![];
        n.extend(node_size.to_le_bytes());
        n.extend(0u16.to_le_bytes());
        n.extend([0u8; 3]);
        n.push(0);
        n.extend([0u8; 14]);
        n.extend(uncompressed.to_le_bytes());
        n.extend(offset.to_le_bytes());
        n.extend(compressed.to_le_bytes());
        n.extend(0u32.to_le_bytes());
        n.extend([0u8; 24]);
        n.extend(path_bytes);
        n
    }
    fn block(id: u16, body: &[u8]) -> Vec<u8> {
        let mut b = vec![];
        b.extend(id.to_le_bytes());
        b.extend(0u16.to_le_bytes());
        let mut content = vec![];
        content.extend((body.len() as u32).to_le_bytes());
        content.push(0);
        content.extend(body);
        b.extend((content.len() as u32).to_le_bytes());
        b.extend(content);
        b
    }
    let mut list = vec![];
    list.extend(2u16.to_le_bytes());
    list.extend(0u16.to_le_bytes());
    list.extend(node(big_name, 0, big.len() as u32 + 12, big.len() as u32));
    list.extend(node(launcher_name, 64, launcher.len() as u32 + 12, launcher.len() as u32));
    let mut data = vec![0, 0, 0, 0, 0];
    data.extend(big);
    while data.len() < 4 + 64 {
        data.push(0);
    }
    data.push(0);
    data.extend(launcher);
    let mut file = b"MZ junk".to_vec();
    file.extend(SIGNATURE);
    file.extend(block(FILE_LIST, &list));
    file.extend(FILE_DATA.to_le_bytes());
    file.extend(0u16.to_le_bytes());
    file.extend(0u32.to_le_bytes());
    file.extend(&data);
    file
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unpacks_archives_and_drops_the_launcher() {
        let big = b"BIGF-game";
        let file = sample_installer("Mod\\a.big", big, "ShockwaveLauncher.exe", b"MZ-launcher");
        let t = std::env::temp_dir().join(format!("gen-click-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        std::fs::create_dir_all(&t).unwrap();
        let exe_path = t.join("setup.exe");
        std::fs::write(&exe_path, &file).unwrap();
        assert!(looks_like(&exe_path));
        let dest = t.join("out");
        extract_into(&exe_path, &dest).unwrap();
        assert_eq!(std::fs::read(dest.join("Mod/a.big")).unwrap(), big);
        assert!(!dest.join("ShockwaveLauncher.exe").exists());
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn real_shockwave_installer_if_present() {
        let Some(path) = std::env::var_os("RALLYPOINT_SHOCKWAVE") else { return };
        let t = std::env::temp_dir().join(format!("gen-shock-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        extract_into(Path::new(&path), &t).unwrap();
        let mut bigs = 0usize;
        let mut exes = 0usize;
        let mut stack = vec![t.clone()];
        while let Some(d) = stack.pop() {
            for e in std::fs::read_dir(&d).unwrap().flatten() {
                let p = e.path();
                if p.is_dir() {
                    stack.push(p);
                } else if p.extension().map(|x| x.eq_ignore_ascii_case("big") || x.eq_ignore_ascii_case("gib")).unwrap_or(false) {
                    bigs += 1;
                } else if p.extension().map(|x| x.eq_ignore_ascii_case("exe")).unwrap_or(false) {
                    exes += 1;
                }
            }
        }
        assert!(bigs > 0, "expected game archives");
        assert_eq!(exes, 0, "the Windows launcher must not be extracted");
        std::fs::remove_dir_all(t).unwrap();
    }
}
