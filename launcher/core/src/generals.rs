//! The generals a game offers: read from the game's own data, so a mod that adds or renames generals shows its own.
//!
//! The data lives in `.big` archives (EA's simple archive format): `Data\INI\PlayerTemplate.ini` lists the factions, and
//! `Data\English\generals.csf` holds their names. A mod's archives take priority over the base game's, as in the game.
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

const MAX_ENTRIES: u32 = 400_000;
const MAX_INI: u32 = 8 << 20;
const MAX_CSF: u32 = 64 << 20;

/// One entry of a `.big` archive, found by lower-cased path with backslashes.
#[derive(Debug, Clone, PartialEq)]
pub struct Entry {
    pub name: String,
    pub offset: u32,
    pub size: u32,
}

/// The entries of a `.big`/`.gib` archive. Header: `BIGF`, archive size (little endian), then big-endian entry count and
/// header size, then per entry a big-endian offset and size and a zero-terminated name.
pub fn list(path: &Path) -> std::io::Result<Vec<Entry>> {
    let mut f = std::fs::File::open(path)?;
    let mut head = [0u8; 16];
    f.read_exact(&mut head)?;
    if &head[..4] != b"BIGF" {
        return Err(std::io::Error::other("not a BIG archive"));
    }
    let count = u32::from_be_bytes(head[8..12].try_into().unwrap());
    let header_size = u32::from_be_bytes(head[12..16].try_into().unwrap());
    if count > MAX_ENTRIES || header_size < 16 || header_size > 64 << 20 {
        return Err(std::io::Error::other("unreasonable BIG header"));
    }
    let mut table = vec![0u8; (header_size - 16) as usize];
    f.read_exact(&mut table)?;
    let (mut pos, mut out) = (0usize, Vec::with_capacity(count as usize));
    for _ in 0..count {
        if pos + 9 > table.len() {
            break;
        }
        let offset = u32::from_be_bytes(table[pos..pos + 4].try_into().unwrap());
        let size = u32::from_be_bytes(table[pos + 4..pos + 8].try_into().unwrap());
        pos += 8;
        let Some(end) = table[pos..].iter().position(|&b| b == 0) else { break };
        out.push(Entry { name: String::from_utf8_lossy(&table[pos..pos + end]).replace('/', "\\").to_ascii_lowercase(), offset, size });
        pos += end + 1;
    }
    Ok(out)
}

pub fn read(path: &Path, e: &Entry, max: u32) -> std::io::Result<Vec<u8>> {
    if e.size > max {
        return Err(std::io::Error::other("entry too large"));
    }
    let mut f = std::fs::File::open(path)?;
    f.seek(SeekFrom::Start(e.offset as u64))?;
    let mut buf = vec![0u8; e.size as usize];
    f.read_exact(&mut buf)?;
    Ok(buf)
}

fn archives(dir: &Path) -> Vec<PathBuf> {
    let mut v: Vec<PathBuf> = std::fs::read_dir(dir)
        .into_iter()
        .flatten()
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|x| x.eq_ignore_ascii_case("big") || x.eq_ignore_ascii_case("gib")))
        .collect();
    v.sort();
    v
}

/// The file `name` as the game would find it: the last layer (highest priority) that has it, the base game last.
fn find(name: &str, base: &Path, layers: &[PathBuf], max: u32) -> Option<Vec<u8>> {
    for dir in layers.iter().rev().chain(std::iter::once(&base.to_path_buf())) {
        for a in archives(dir) {
            if let Ok(entries) = list(&a) {
                if let Some(e) = entries.iter().find(|e| e.name == name) {
                    if let Ok(b) = read(&a, e, max) {
                        return Some(b);
                    }
                }
            }
        }
    }
    None
}

/// Labels of a `.csf` string table (written as ` FSC`, then version, label and string counts and two more words, then ` LBL`
/// blocks of ` RTS`/`WRTS` strings stored as UTF-16 with every bit inverted: the four-letter tags are stored reversed). Only the first string of each label is kept.
pub fn parse_csf(b: &[u8]) -> std::collections::HashMap<String, String> {
    let mut out = std::collections::HashMap::new();
    let u32at = |p: usize| b.get(p..p + 4).map(|x| u32::from_le_bytes(x.try_into().unwrap()));
    if b.get(..4) != Some(b" FSC") {
        return out;
    }
    let Some(labels) = u32at(8) else { return out };
    let mut p = 24;
    for _ in 0..labels {
        if b.get(p..p + 4) != Some(b" LBL") {
            break;
        }
        let (Some(n), Some(len)) = (u32at(p + 4), u32at(p + 8)) else { break };
        p += 12;
        let Some(name) = b.get(p..p + len as usize) else { break };
        let name = String::from_utf8_lossy(name).into_owned();
        p += len as usize;
        let mut first: Option<String> = None;
        for _ in 0..n {
            let kind = b.get(p..p + 4).map(|k| k.to_vec());
            let Some(chars) = u32at(p + 4) else { return out };
            p += 8;
            let Some(raw) = b.get(p..p + chars as usize * 2) else { return out };
            p += chars as usize * 2;
            if first.is_none() {
                let units: Vec<u16> = raw.chunks(2).map(|c| !u16::from_le_bytes([c[0], c[1]])).collect();
                first = Some(String::from_utf16_lossy(&units));
            }
            if kind.as_deref() == Some(b"WRTS") {
                let Some(extra) = u32at(p) else { return out };
                p += 4 + extra as usize;
            }
        }
        if let Some(s) = first {
            out.insert(name, s);
        }
    }
    out
}

#[derive(Debug, Clone, PartialEq, serde::Serialize)]
pub struct General {
    /// The game's internal name (`FactionAmericaLaserGeneral`), what the game is told.
    pub id: String,
    /// What the game calls it on screen (`Laser General Alexis Alexander`).
    pub name: String,
}

/// `PlayerTemplate` blocks that can be chosen in a skirmish (`PlayableSide = Yes`, and not the Boss General, which the
/// game keeps out of its own lists), with the label of their display name.
pub fn parse_templates(ini: &str) -> Vec<(String, Option<String>)> {
    let mut out = vec![];
    let mut current: Option<(String, bool, Option<String>, bool)> = None; // name, playable, label, is the Boss
    for line in ini.lines() {
        let l = line.split(';').next().unwrap_or("").trim();
        if let Some(rest) = l.strip_prefix("PlayerTemplate") {
            if rest.starts_with(char::is_whitespace) {
                current = Some((rest.trim().to_string(), false, None, false));
                continue;
            }
        }
        if l.eq_ignore_ascii_case("End") {
            if let Some((name, playable, label, boss)) = current.take() {
                if playable && !boss {
                    out.push((name, label));
                }
            }
            continue;
        }
        if let (Some(c), Some((k, v))) = (current.as_mut(), l.split_once('=')) {
            let (k, v) = (k.trim(), v.trim());
            if k.eq_ignore_ascii_case("PlayableSide") {
                c.1 = v.eq_ignore_ascii_case("yes");
            } else if k.eq_ignore_ascii_case("Side") {
                c.3 = v.eq_ignore_ascii_case("boss");
            } else if k.eq_ignore_ascii_case("DisplayName") {
                c.2 = Some(v.to_string());
            }
        }
    }
    out
}

/// `FactionAmericaLaserGeneral` -> `America Laser General`, for a general whose name the game's text does not have.
fn plain_name(id: &str) -> String {
    let s = id.strip_prefix("Faction").unwrap_or(id);
    let mut out = String::new();
    for (i, c) in s.chars().enumerate() {
        if i > 0 && c.is_uppercase() && !s.chars().nth(i - 1).is_some_and(|p| p.is_uppercase()) {
            out.push(' ');
        }
        out.push(c);
    }
    out
}

/// The generals of the game in `base` with the mod folders `layers` laid over it (later folders win).
pub fn generals(base: &Path, layers: &[PathBuf]) -> Vec<General> {
    let Some(ini) = find("data\\ini\\playertemplate.ini", base, layers, MAX_INI) else { return vec![] };
    let strings = find("data\\english\\generals.csf", base, layers, MAX_CSF).map(|b| parse_csf(&b)).unwrap_or_default();
    parse_templates(&String::from_utf8_lossy(&ini))
        .into_iter()
        .map(|(id, label)| {
            let name = label.and_then(|l| strings.get(&l).cloned()).filter(|s| !s.trim().is_empty()).unwrap_or_else(|| plain_name(&id));
            General { id, name }
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn big(files: &[(&str, &[u8])]) -> Vec<u8> {
        let table: usize = files.iter().map(|(n, _)| 8 + n.len() + 1).sum();
        let header = 16 + table;
        let mut out = b"BIGF".to_vec();
        out.extend(0u32.to_le_bytes());
        out.extend((files.len() as u32).to_be_bytes());
        out.extend((header as u32).to_be_bytes());
        let mut offset = header as u32;
        for (n, d) in files {
            out.extend(offset.to_be_bytes());
            out.extend((d.len() as u32).to_be_bytes());
            out.extend(n.as_bytes());
            out.push(0);
            offset += d.len() as u32;
        }
        for (_, d) in files {
            out.extend(*d);
        }
        out
    }

    fn csf(labels: &[(&str, &str)]) -> Vec<u8> {
        let mut out = b" FSC".to_vec();
        for w in [3u32, labels.len() as u32, labels.len() as u32, 0, 0] {
            out.extend(w.to_le_bytes());
        }
        for (l, s) in labels {
            out.extend(b" LBL");
            out.extend(1u32.to_le_bytes());
            out.extend((l.len() as u32).to_le_bytes());
            out.extend(l.as_bytes());
            out.extend(b" RTS");
            let units: Vec<u16> = s.encode_utf16().collect();
            out.extend((units.len() as u32).to_le_bytes());
            for u in units {
                out.extend((!u).to_le_bytes());
            }
        }
        out
    }

    const INI: &str = "PlayerTemplate FactionCivilian\n  Side = Civilian\n  PlayableSide = No\n  DisplayName = INI:FactionCivilian\nEnd\n\nPlayerTemplate FactionAmerica ; the USA\n  PlayableSide = Yes\n  DisplayName = INI:FactionAmerica\nEnd\nPlayerTemplate FactionChinaTankGeneral\n  PlayableSide = Yes\n  DisplayName = INI:FactionChinaTankGeneral\nEnd\nPlayerTemplate FactionBossGeneral\n  Side = Boss\n  PlayableSide = Yes\n  DisplayName = INI:FactionBossGeneral\nEnd\n";

    #[test]
    fn reads_templates_and_names_from_archives_with_a_mod_on_top() {
        let t = std::env::temp_dir().join(format!("gen-generals-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        let (base, modd) = (t.join("base"), t.join("mod"));
        std::fs::create_dir_all(&base).unwrap();
        std::fs::create_dir_all(&modd).unwrap();
        std::fs::write(base.join("INIZH.big"), big(&[("Data\\INI\\PlayerTemplate.ini", INI.as_bytes()), ("Data\\INI\\Other.ini", b"x")])).unwrap();
        std::fs::write(base.join("EnglishZH.big"), big(&[("Data\\English\\generals.csf", &csf(&[("INI:FactionAmerica", "USA"), ("INI:FactionChinaTankGeneral", "Tank General Kwai")]))])).unwrap();
        let g = generals(&base, &[]);
        assert_eq!(g.iter().map(|g| (g.id.as_str(), g.name.as_str())).collect::<Vec<_>>(), [("FactionAmerica", "USA"), ("FactionChinaTankGeneral", "Tank General Kwai")]);
        // a mod with its own list (and no text for one of them) replaces the base game's
        let mod_ini = "PlayerTemplate FactionAmerica\n PlayableSide = Yes\n DisplayName = INI:FactionAmerica\nEnd\nPlayerTemplate FactionBlueTeam\n PlayableSide = Yes\n DisplayName = INI:FactionBlueTeam\nEnd\n";
        std::fs::write(modd.join("Mod.big"), big(&[("Data/INI/PlayerTemplate.ini", mod_ini.as_bytes())])).unwrap();
        let g = generals(&base, std::slice::from_ref(&modd));
        assert_eq!(g.iter().map(|g| (g.id.as_str(), g.name.as_str())).collect::<Vec<_>>(), [("FactionAmerica", "USA"), ("FactionBlueTeam", "Blue Team")]);
        let _ = std::fs::remove_dir_all(t);
    }

    #[test]
    fn plain_names_and_bad_files_do_not_break_it() {
        assert_eq!(plain_name("FactionAmericaLaserGeneral"), "America Laser General");
        assert_eq!(plain_name("FactionGLA"), "GLA");
        assert!(parse_csf(b"nope").is_empty());
        assert!(parse_csf(b" FSC\x03\0\0\0\xff\xff\xff\xff").is_empty());
        let t = std::env::temp_dir().join(format!("gen-generals-bad-{}", std::process::id()));
        std::fs::create_dir_all(&t).unwrap();
        std::fs::write(t.join("A.big"), b"BIGF not really").unwrap();
        assert!(generals(&t, &[]).is_empty());
        let _ = std::fs::remove_dir_all(t);
    }

    /// The real game on this machine, if it is there: twelve choices, with names.
    #[test]
    fn reads_the_real_game_if_present() {
        let Some(home) = std::env::var_os("HOME") else { return };
        let dir = PathBuf::from(home).join("Library/Application Support/GeneralsZH/game/ZeroHour");
        if !dir.join("INIZH.big").is_file() {
            return;
        }
        let g = generals(&dir, &[]);
        assert!(g.len() >= 12, "{g:?}");
        assert!(g.iter().any(|g| g.id == "FactionAmerica"));
        assert!(g.iter().all(|g| !g.name.is_empty() && !g.name.starts_with("INI:")), "{g:?}");
    }
}
