//! Sending a map to the other players of a relay room, for Generals / Zero Hour (a map folder) and Red Alert 2 (one map
//! file). Threat model and wire description: `docs/send-map-threat-model.md`.
//!
//! Three layers, each testable alone:
//! - the **bundle** (`pack`, `unpack`, `validate`, `DirLibrary::install`): what travels. Every byte that arrives is hostile
//!   until it has passed all of them, and nothing touches the maps folder before that.
//! - the **transfer** (`MapShare`): who is in the room, offers, windows, acks, timeouts. Pure state, driven by `receive`
//!   and `tick`, which return datagrams through `drain`; the Tauri shell only moves them to and from the agent.
//! - the **view** (`ShareView`): what the page shows. Names that came from other players are cleaned and length-limited here.
use crate::{ra2launch, ra2maps};
use flate2::read::ZlibDecoder;
use flate2::write::ZlibEncoder;
use flate2::Compression;
use generals_proto::{map_chunk_len, map_chunks, map_end, sha256, MapMsg, Vip, MAP_CHUNK, MAP_GENERALS, MAP_RA2, MAX_MAP_BYTES, MAX_MAP_NAME, MAX_PEER_NAME};
use std::collections::{BTreeMap, HashSet};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::sync::Arc;

// ---------------------------------------------------------------------------------------------- the bundle

/// Files in one bundle, bytes of one name, and what a bundle may unpack to in all. Real maps are a handful of files and a
/// few MB; these are far above that and far below what would hurt.
pub const MAX_FILES: usize = 16;
pub const MAX_FILE_NAME: usize = 100;
pub const MAX_UNPACKED: usize = 32 << 20;
const MAX_MAP_FILE: usize = 16 << 20;
const MAX_PICTURE: usize = 4 << 20;
const MAX_TEXT: usize = 1 << 20;
const MAGIC: &[u8; 4] = b"RPM1";
const GENERALS_EXT: [&str; 5] = ["map", "tga", "ini", "str", "txt"];

/// A map as it travels: which game, its name (the folder for Generals, the file for Red Alert 2) and its files.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Bundle {
    pub game: u8,
    pub name: String,
    pub files: Vec<(String, Vec<u8>)>,
}

fn ext_of(name: &str) -> String {
    Path::new(name).extension().map(|e| e.to_string_lossy().to_lowercase()).unwrap_or_default()
}

/// One plain file name that is the same thing on every system: no folders, drives, streams or wildcards, no control
/// characters, nothing Windows reserves (`CON`, `NUL.txt`, `COM1`...), and no trailing dot or space.
pub fn flat_name_ok(name: &str) -> bool {
    if name.is_empty() || name.len() > MAX_FILE_NAME || name == "." || name == ".." || name.starts_with(['.', ' ']) || name.ends_with(['.', ' ']) {
        return false;
    }
    if name.chars().any(|c| c.is_control() || "/\\:*?\"<>|".contains(c)) {
        return false;
    }
    let stem = name.split('.').next().unwrap_or("").trim_end().to_ascii_uppercase();
    let reserved = ["CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"].contains(&stem.as_str()) || (stem.len() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem.as_bytes()[3].is_ascii_digit());
    !reserved
}

fn allowed_ext(game: u8) -> &'static [&'static str] {
    if game == MAP_RA2 { &ra2launch::MAP_EXTENSIONS } else { &GENERALS_EXT }
}

/// What identifies a map's content: the game, its name (any case) and every file's name (any case) and bytes, in name order.
/// The same map read from two computers hashes the same, whichever of them packed it.
pub fn content_hash(game: u8, name: &str, files: &[(String, Vec<u8>)]) -> [u8; 32] {
    let mut sorted: Vec<&(String, Vec<u8>)> = files.iter().collect();
    sorted.sort_by_key(|f| f.0.to_lowercase());
    let head = [b"rallypoint map v1\0".to_vec(), vec![game], name.to_lowercase().into_bytes(), vec![0]].concat();
    let heads: Vec<Vec<u8>> = sorted.iter().map(|(n, d)| [n.to_lowercase().as_bytes(), &[0], &(d.len() as u32).to_be_bytes()].concat()).collect();
    let mut parts: Vec<&[u8]> = vec![&head];
    for (h, f) in heads.iter().zip(&sorted) {
        parts.push(h);
        parts.push(&f.1);
    }
    sha256(&parts)
}

/// The bytes before compression: magic, game, name, a table of (name, size), then the files end to end.
fn encode_raw(b: &Bundle) -> Vec<u8> {
    let mut out = MAGIC.to_vec();
    out.push(b.game);
    out.push(b.name.len().min(255) as u8);
    out.extend_from_slice(b.name.as_bytes());
    out.push(b.files.len().min(255) as u8);
    for (n, d) in &b.files {
        out.push(n.len().min(255) as u8);
        out.extend_from_slice(n.as_bytes());
        out.extend_from_slice(&(d.len() as u32).to_be_bytes());
    }
    for (_, d) in &b.files {
        out.extend_from_slice(d);
    }
    out
}

fn compress(raw: &[u8]) -> Result<Vec<u8>, String> {
    let mut e = ZlibEncoder::new(Vec::new(), Compression::new(6));
    e.write_all(raw).map_err(|e| e.to_string())?;
    e.finish().map_err(|e| e.to_string())
}

/// The packed form that is sent. Refuses what the other side would refuse (so the player hears it now) and anything over
/// the transfer cap.
pub fn pack(b: &Bundle) -> Result<Vec<u8>, String> {
    check_shape(b)?;
    let packed = compress(&encode_raw(b))?;
    if packed.len() > MAX_MAP_BYTES as usize {
        return Err(format!("This map is {:.1} MB packed, over the {} MB a transfer may carry", packed.len() as f64 / 1048576.0, MAX_MAP_BYTES >> 20));
    }
    Ok(packed)
}

/// Bytes -> bundle. Everything is bounded by what has really arrived: the inflater stops at `MAX_UNPACKED`, a size in the
/// header is only compared with what is left, never used to allocate.
pub fn unpack(packed: &[u8]) -> Result<Bundle, String> {
    let bad = || "the map file is damaged".to_string();
    if packed.len() > MAX_MAP_BYTES as usize {
        return Err("the map file is bigger than a transfer may be".into());
    }
    let mut raw = Vec::new();
    ZlibDecoder::new(packed).take(MAX_UNPACKED as u64 + 1).read_to_end(&mut raw).map_err(|_| bad())?;
    if raw.len() > MAX_UNPACKED {
        return Err("the map unpacks to far more than any map should".into());
    }
    let mut r = raw.as_slice();
    let mut take = |n: usize| -> Result<&[u8], String> {
        if r.len() < n {
            return Err(bad());
        }
        let (a, b) = r.split_at(n);
        r = b;
        Ok(a)
    };
    if take(4)? != MAGIC {
        return Err(bad());
    }
    let game = take(1)?[0];
    let n = take(1)?[0] as usize;
    let name = String::from_utf8(take(n)?.to_vec()).map_err(|_| bad())?;
    let count = take(1)?[0] as usize;
    if count > MAX_FILES {
        return Err("the map has too many files".into());
    }
    let mut table = vec![];
    for _ in 0..count {
        let n = take(1)?[0] as usize;
        let fname = String::from_utf8(take(n)?.to_vec()).map_err(|_| bad())?;
        let size = u32::from_be_bytes(take(4)?.try_into().unwrap()) as usize;
        table.push((fname, size));
    }
    let mut files = vec![];
    for (fname, size) in table {
        files.push((fname, take(size)?.to_vec()));
    }
    if !r.is_empty() {
        return Err(bad()); // bytes nobody asked for
    }
    Ok(Bundle { game, name, files })
}

/// Everything about a bundle that does not need the files' contents: names, extensions, counts and sizes.
fn check_shape(b: &Bundle) -> Result<(), String> {
    if b.game != MAP_GENERALS && b.game != MAP_RA2 {
        return Err("not a map for a game Rallypoint knows".into());
    }
    if b.files.is_empty() || b.files.len() > MAX_FILES {
        return Err("a map has between one and sixteen files".into());
    }
    let mut seen = HashSet::new();
    let mut total = 0usize;
    for (n, d) in &b.files {
        if !flat_name_ok(n) {
            return Err(format!("{n:?} is not a file name a map may have"));
        }
        if !allowed_ext(b.game).contains(&ext_of(n).as_str()) {
            return Err(format!("{n:?} is not a kind of file a map has"));
        }
        if !seen.insert(n.to_lowercase()) {
            return Err(format!("{n:?} is in the map twice"));
        }
        let cap = match ext_of(n).as_str() {
            "ini" | "str" | "txt" => MAX_TEXT,
            "tga" => MAX_PICTURE,
            _ => MAX_MAP_FILE,
        };
        if d.len() > cap {
            return Err(format!("{n:?} is bigger than a map's file should be"));
        }
        total += d.len();
    }
    if total > MAX_UNPACKED {
        return Err("the map is bigger than any map should be".into());
    }
    if b.name.is_empty() || b.name.len() > MAX_MAP_NAME {
        return Err("the map has no usable name".into());
    }
    Ok(())
}

/// A bundle is acceptable if its shape is and the map is readable by the parsers the launcher itself uses to show it.
/// Returns the number of start positions.
pub fn validate(b: &Bundle) -> Result<u8, String> {
    check_shape(b)?;
    if b.game == MAP_GENERALS {
        if !crate::maporg::is_safe_name(&b.name) {
            return Err(format!("{:?} is not a map folder name", b.name));
        }
        let own = format!("{}.map", b.name).to_lowercase();
        let map = b.files.iter().find(|f| f.0.to_lowercase() == own).ok_or("the map has no map file of its own name")?;
        let info = crate::mapview::parse_map(&map.1)?;
        for (n, d) in &b.files {
            if ext_of(n) == "tga" {
                crate::mapview::decode_tga(d)?;
            }
            if matches!(ext_of(n).as_str(), "ini" | "str" | "txt") && d.contains(&0) {
                return Err(format!("{n:?} is not a text file"));
            }
        }
        Ok(info.starts.len().min(255) as u8)
    } else {
        let [(file, bytes)] = b.files.as_slice() else { return Err("a Red Alert 2 map is one file".into()) };
        if file != &b.name || !ra2launch::safe_map_name(file) {
            return Err(format!("{file:?} is not a map file name"));
        }
        let m = ra2maps::parse(bytes, file)?;
        Ok(m.info.starts.len().min(255) as u8)
    }
}

/// A map ready to send: packed once, whoever it goes to.
#[derive(Debug, Clone)]
pub struct Prepared {
    pub game: u8,
    pub name: String,
    pub players: u8,
    pub packed: Arc<Vec<u8>>,
    pub hash: [u8; 32],
}

fn prepare(b: Bundle) -> Result<Prepared, String> {
    let players = validate(&b).map_err(|e| format!("This map can't be shared: {e}"))?;
    let hash = content_hash(b.game, &b.name, &b.files);
    Ok(Prepared { game: b.game, name: b.name.clone(), players, packed: Arc::new(pack(&b)?), hash })
}

/// The files of a Generals map folder that travel with it (the others, and sub folders, stay behind).
fn read_folder(dir: &Path) -> Result<Vec<(String, Vec<u8>)>, String> {
    let mut files = vec![];
    for e in std::fs::read_dir(dir).map_err(|e| e.to_string())?.flatten() {
        let name = e.file_name().to_string_lossy().into_owned();
        // `file_type` does not follow links: a link in a map folder is never read
        if !e.file_type().is_ok_and(|t| t.is_file()) || !flat_name_ok(&name) || !GENERALS_EXT.contains(&ext_of(&name).as_str()) {
            continue;
        }
        files.push((name, read_capped(&e.path())?));
        if files.len() > MAX_FILES {
            return Err("the folder has too many files to send".into());
        }
    }
    files.sort_by_key(|f| f.0.to_lowercase());
    Ok(files)
}

fn read_capped(p: &Path) -> Result<Vec<u8>, String> {
    let len = std::fs::metadata(p).map_err(|e| e.to_string())?.len();
    if len > MAX_MAP_FILE as u64 {
        return Err(format!("{} is too big to send", p.file_name().unwrap_or_default().to_string_lossy()));
    }
    std::fs::read(p).map_err(|e| e.to_string())
}

/// A Generals map folder, read for sending.
pub fn prepare_generals(dir: &Path) -> Result<Prepared, String> {
    let name = dir.file_name().ok_or("not a map folder")?.to_string_lossy().into_owned();
    prepare(Bundle { game: MAP_GENERALS, name, files: read_folder(dir)? })
}

/// A Red Alert 2 map file, read for sending.
pub fn prepare_ra2(file: &Path) -> Result<Prepared, String> {
    let name = file.file_name().ok_or("not a map file")?.to_string_lossy().into_owned();
    prepare(Bundle { game: MAP_RA2, name: name.clone(), files: vec![(name, read_capped(file)?)] })
}

// ---------------------------------------------------------------------------------------------- the library

/// A map that was installed.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Installed {
    pub name: String,
    /// Another map of the same name was there and the player chose to keep it, so this one went in under a new name.
    pub renamed: bool,
    /// The player chose to replace their map of that name; the old one was kept in the backup folder.
    pub replaced: bool,
}

/// What the receiving side needs from the player's maps: whether a map is already there, and a way in.
pub trait Library {
    /// This exact map (same name, same content) is already installed (switched on or off).
    fn has(&self, game: u8, name: &str, hash: &[u8; 32]) -> bool;
    /// A map of this name is installed, whatever is in it.
    fn exists(&self, game: u8, name: &str) -> bool;
    /// Put a checked map in. With `replace` the map of the same name is swapped for it (and kept in a backup folder);
    /// without, a taken name gets a new one and nothing is overwritten.
    fn install(&self, b: &Bundle, replace: bool) -> Result<Installed, String>;
}

/// The player's real folders.
pub struct DirLibrary {
    /// Generals' maps folder, and the folder switched-off maps wait in.
    pub maps_dir: PathBuf,
    pub disabled_dir: PathBuf,
    /// Red Alert 2's own map folder.
    pub ra2_dir: PathBuf,
}

/// Where a map is put together before it moves, and where a replaced map is kept (`replaced/`): beside the folder it goes
/// to, so on the same disk (a move is then one rename), and never inside a folder the game reads.
pub fn staging_root(dest_dir: &Path) -> PathBuf {
    dest_dir.parent().unwrap_or(dest_dir).join(".rallypoint-incoming")
}

fn find_ci(dir: &Path, name: &str) -> Option<PathBuf> {
    std::fs::read_dir(dir).ok()?.flatten().find(|e| e.file_name().to_string_lossy().eq_ignore_ascii_case(name)).map(|e| e.path())
}

fn unique_suffix() -> String {
    static N: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(0);
    let t = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_nanos());
    format!("{}-{t:x}-{}", std::process::id(), N.fetch_add(1, std::sync::atomic::Ordering::Relaxed))
}

impl DirLibrary {
    fn existing(&self, game: u8, name: &str) -> Vec<PathBuf> {
        if game == MAP_RA2 { find_ci(&self.ra2_dir, name).into_iter().collect() } else { [&self.maps_dir, &self.disabled_dir].iter().filter_map(|d| find_ci(d, name)).collect() }
    }
}

impl Library for DirLibrary {
    fn has(&self, game: u8, name: &str, hash: &[u8; 32]) -> bool {
        self.existing(game, name).iter().any(|p| {
            let files = if game == MAP_RA2 {
                p.file_name().map(|n| n.to_string_lossy().into_owned()).and_then(|n| read_capped(p).ok().map(|d| vec![(n, d)]))
            } else {
                read_folder(p).ok()
            };
            files.is_some_and(|f| content_hash(game, &p.file_name().unwrap_or_default().to_string_lossy(), &f) == *hash)
        })
    }

    fn exists(&self, game: u8, name: &str) -> bool {
        !self.existing(game, name).is_empty()
    }

    /// Writes the files into a staging folder (never overwriting anything), then moves the folder or file into place in one
    /// step. A map whose name is taken by a different map goes in as `Name (2)`, `Name (3)`..., or, if the player said so,
    /// takes the old one's place and the old one moves to `staging/replaced/`.
    fn install(&self, b: &Bundle, replace: bool) -> Result<Installed, String> {
        let ra2 = b.game == MAP_RA2;
        let old = if replace { self.existing(b.game, &b.name).into_iter().next() } else { None };
        // where the map goes: next to the one it replaces, else the game's folder
        let dest_dir = old.as_ref().and_then(|p| p.parent().map(Path::to_path_buf)).unwrap_or_else(|| if ra2 { self.ra2_dir.clone() } else { self.maps_dir.clone() });
        let taken = |n: &str| self.exists(b.game, n);
        let final_name = if let Some(p) = &old {
            p.file_name().unwrap_or_default().to_string_lossy().into_owned()
        } else if !taken(&b.name) {
            b.name.clone()
        } else {
            (2..1000)
                .map(|n| {
                    if ra2 {
                        let p = Path::new(&b.name);
                        format!("{} ({n}).{}", p.file_stem().unwrap_or_default().to_string_lossy(), p.extension().unwrap_or_default().to_string_lossy())
                    } else {
                        format!("{} ({n})", b.name)
                    }
                })
                .find(|n| !taken(n))
                .ok_or("too many maps of that name")?
        };
        let name_ok = if ra2 { ra2launch::safe_map_name(&final_name) } else { crate::maporg::is_safe_name(&final_name) };
        if !name_ok || final_name.len() > MAX_MAP_NAME {
            return Err("the map's name is too long to give it a new one".into());
        }
        let staging = staging_root(&dest_dir);
        let stage = staging.join(format!("in-{}", unique_suffix()));
        std::fs::create_dir_all(&stage).map_err(|e| e.to_string())?;
        let result = (|| {
            std::fs::create_dir_all(&dest_dir).map_err(|e| e.to_string())?;
            let old_stem = Path::new(&b.name).file_stem().unwrap_or_default().to_string_lossy().to_lowercase();
            let write = |dir: &Path, name: &str, data: &[u8]| -> Result<(), String> {
                // create_new: a name that is already there fails instead of being overwritten
                let mut f = std::fs::OpenOptions::new().write(true).create_new(true).open(dir.join(name)).map_err(|e| e.to_string())?;
                f.write_all(data).map_err(|e| e.to_string())
            };
            let new = stage.join(&final_name);
            if ra2 {
                write(&stage, &final_name, &b.files[0].1)?;
            } else {
                std::fs::create_dir(&new).map_err(|e| e.to_string())?;
                for (n, d) in &b.files {
                    // the files named after the folder follow it to its new name
                    let own = Path::new(n).file_stem().unwrap_or_default().to_string_lossy().to_lowercase() == old_stem;
                    let n = if own && final_name != b.name { format!("{final_name}.{}", ext_of(n)) } else { n.clone() };
                    write(&new, &n, d)?;
                }
            }
            // swapping: the old one moves out of the way first, and comes back if the new one cannot take its place
            let backup = match &old {
                Some(p) => {
                    let keep = staging.join("replaced");
                    std::fs::create_dir_all(&keep).map_err(|e| e.to_string())?;
                    let to = keep.join(format!("{final_name}-{}", unique_suffix()));
                    std::fs::rename(p, &to).map_err(|e| e.to_string())?;
                    Some((p.clone(), to))
                }
                None => None,
            };
            if let Err(e) = std::fs::rename(&new, dest_dir.join(&final_name)) {
                if let Some((from, to)) = backup {
                    let _ = std::fs::rename(to, from);
                }
                return Err(e.to_string());
            }
            Ok(())
        })();
        let _ = std::fs::remove_dir_all(&stage);
        result?;
        Ok(Installed { renamed: !replace && final_name != b.name, replaced: old.is_some(), name: final_name })
    }
}

// ---------------------------------------------------------------------------------------------- the transfer

const HI_EVERY: u64 = 5_000;
const HI_MIN_GAP: u64 = 2_500;
const PEER_STALE: u64 = 20_000;
/// No answer to our `Hi` in this long: the relay does not share maps.
const CAPS_WAIT: u64 = 9_000;
const OFFER_REPEAT: u64 = 4_000;
const OFFER_WAIT: u64 = 150_000;
const PROMPT_WAIT: u64 = 120_000;
const WINDOW: u32 = 24;
const RESEND_AFTER: u64 = 1_500;
const STALL: u64 = 20_000;
const END_WAIT: u64 = 30_000;
const RECV_IDLE: u64 = 20_000;
const ACK_EVERY: u32 = 8;
const ACK_GAP: u64 = 100;
/// One chunk per this many milliseconds, shared by all transfers: about 300 kB a second, under the relay's cap per session.
const MS_PER_CHUNK: u64 = 3;
const KEEP_DONE: u64 = 600_000;
const MAX_OPEN_OUT: usize = 4;
const MAX_OPEN_IN: usize = 4;

/// Text from another player, made safe to show: no control or direction-changing characters, one line, trimmed, and not
/// too long.
pub fn display(s: &str, max_chars: usize) -> String {
    let kept: String = s.chars().filter(|c| !c.is_control() && !matches!(*c, '\u{200B}'..='\u{200F}' | '\u{202A}'..='\u{202E}' | '\u{2066}'..='\u{2069}' | '\u{FEFF}')).collect();
    kept.split_whitespace().collect::<Vec<_>>().join(" ").chars().take(max_chars).collect()
}

fn dotted(v: Vip) -> String {
    format!("{}.{}.{}.{}", v[0], v[1], v[2], v[3])
}

#[derive(Debug, Clone)]
struct Peer {
    name: String,
    game: u8,
    master: bool,
    seen: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum OutState {
    Waiting,
    Sending,
    /// Finished: the receiver's `End` code, or `FAILED`, with a sentence.
    Over(u8, String),
}
const FAILED: u8 = 255;

struct Outgoing {
    id: u32,
    to: Vip,
    name: String,
    game: u8,
    players: u8,
    hash: [u8; 32],
    data: Arc<Vec<u8>>,
    state: OutState,
    next_ack: u32,
    next_send: u32,
    started: u64,
    last_offer: u64,
    last_ack: u64,
    last_resend: u64,
    over_at: u64,
}

impl Outgoing {
    fn chunks(&self) -> u32 {
        map_chunks(self.data.len() as u32)
    }
    fn open(&self) -> bool {
        !matches!(self.state, OutState::Over(..))
    }
    fn offer(&self) -> MapMsg {
        MapMsg::Offer { id: self.id, game: self.game, master: false, size: self.data.len() as u32, players: self.players, hash: self.hash, name: self.name.clone() }
    }
    fn finish(&mut self, now: u64, code: u8, note: &str) {
        if self.open() {
            self.state = OutState::Over(code, note.to_string());
            self.over_at = now;
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
enum InState {
    Prompt,
    Receiving,
    /// Finished: the `End` code sent, and a sentence (what was saved, or what went wrong).
    Over(u8, String),
}

struct Incoming {
    id: u32,
    from: Vip,
    game: u8,
    name: String,
    size: u32,
    players: u8,
    hash: [u8; 32],
    master: bool,
    /// The player has a different map of this name.
    conflict: bool,
    /// ... and chose to replace it.
    replace: bool,
    state: InState,
    buf: Vec<u8>,
    next: u32,
    acked: u32,
    last_rx: u64,
    last_ack: u64,
    dirty: bool,
    offered: u64,
    over_at: u64,
    installed: Option<Installed>,
}

impl Incoming {
    fn active(&self) -> bool {
        matches!(self.state, InState::Prompt | InState::Receiving)
    }
}

/// What the player decides about an incoming offer.
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Reply {
    /// Take it. If the player has a different map of that name, this one is saved under another name and theirs stays.
    Accept,
    /// Take it in place of the player's own map of that name (which is kept in the backup folder).
    Replace,
    Decline,
    /// Decline, and refuse everything else from this player for as long as the launcher stays in this room.
    Block,
}

pub struct MapShare {
    me: Option<Vip>,
    name: String,
    game: u8,
    auto_accept: bool,
    connected_at: u64,
    caps: bool,
    peers: BTreeMap<Vip, Peer>,
    out: Vec<Outgoing>,
    inc: Vec<Incoming>,
    blocked: HashSet<Vip>,
    next_id: u32,
    last_hi: Option<u64>,
    hi_wanted: bool,
    last_tick: u64,
    queue: Vec<(Vip, MapMsg)>,
}

impl MapShare {
    /// `seed` makes the first transfer id (ids only need to differ between one sender's transfers).
    pub fn new(seed: u32) -> MapShare {
        MapShare { me: None, name: String::new(), game: MAP_GENERALS, auto_accept: false, connected_at: 0, caps: false, peers: BTreeMap::new(), out: vec![], inc: vec![], blocked: HashSet::new(), next_id: seed | 1, last_hi: None, hi_wanted: false, last_tick: 0, queue: vec![] }
    }

    /// Who we are, called every round. `None` for the address means not in a room: everything is forgotten (blocks too).
    pub fn set_self(&mut self, now: u64, vip: Option<Vip>, name: &str, game: u8, auto_accept: bool) {
        if vip != self.me {
            *self = MapShare::new(self.next_id);
            self.me = vip;
            self.connected_at = now;
        }
        self.name = display(name, MAX_PEER_NAME);
        self.game = game;
        self.auto_accept = auto_accept;
    }

    /// Does this server share maps? `None` while we are still finding out.
    pub fn supported(&self, now: u64) -> Option<bool> {
        self.me?;
        if self.caps { Some(true) } else if now.saturating_sub(self.connected_at) > CAPS_WAIT { Some(false) } else { None }
    }

    /// Datagrams to send now: (destination, message). A destination of all zeros is the whole room.
    pub fn drain(&mut self) -> Vec<(Vip, MapMsg)> {
        std::mem::take(&mut self.queue)
    }

    fn peer_name(&self, v: Vip) -> String {
        self.peers.get(&v).map(|p| p.name.clone()).filter(|n| !n.is_empty()).unwrap_or_else(|| format!("Player {}", v[3]))
    }

    // ---- sending

    /// Offer `p` to each of `to`. Returns how many were started; if none could be, why not.
    pub fn send(&mut self, now: u64, to: &[Vip], p: &Prepared) -> Result<usize, String> {
        match self.supported(now) {
            None => return Err("Still checking whether the server can share maps. Try again in a moment.".into()),
            Some(false) => return Err("This server does not support sending maps yet. Its owner needs to update it.".into()),
            Some(true) => {}
        }
        if p.game != self.game {
            return Err("This map is for the other game. Switch games to send it.".into());
        }
        let mut started = 0;
        let mut why = String::new();
        for v in to {
            let name = self.peer_name(*v);
            let Some(peer) = self.peers.get(v) else {
                why = format!("{name} is no longer in the room.");
                continue;
            };
            if peer.game != p.game {
                why = format!("{name} has the other game selected.");
                continue;
            }
            if self.out.iter().any(|o| o.to == *v && o.open()) {
                why = format!("You are already sending a map to {name}.");
                continue;
            }
            if self.out.iter().filter(|o| o.open()).count() >= MAX_OPEN_OUT {
                why = "Four maps are already being sent. Wait for one to finish.".into();
                break;
            }
            let id = self.next_id;
            self.next_id = self.next_id.wrapping_add(1).max(1);
            let o = Outgoing { id, to: *v, name: p.name.clone(), game: p.game, players: p.players, hash: p.hash, data: p.packed.clone(), state: OutState::Waiting, next_ack: 0, next_send: 0, started: now, last_offer: now, last_ack: now, last_resend: now, over_at: 0 };
            self.queue.push((*v, o.offer()));
            self.out.push(o);
            started += 1;
        }
        if started == 0 {
            return Err(if why.is_empty() { "Choose at least one player.".into() } else { why });
        }
        Ok(started)
    }

    pub fn cancel_out(&mut self, now: u64, to: Vip, id: u32) {
        if let Some(o) = self.out.iter_mut().find(|o| o.to == to && o.id == id && o.open()) {
            o.finish(now, map_end::CANCELLED, "Cancelled");
            self.queue.push((to, MapMsg::Cancel { id }));
        }
    }

    /// Take a finished entry off the list.
    pub fn dismiss_out(&mut self, to: Vip, id: u32) {
        self.out.retain(|o| !(o.to == to && o.id == id && !o.open()));
    }

    pub fn dismiss_in(&mut self, from: Vip, id: u32) {
        self.inc.retain(|i| !(i.from == from && i.id == id && !i.active()));
    }

    // ---- receiving

    pub fn respond(&mut self, now: u64, from: Vip, id: u32, reply: Reply) {
        match reply {
            Reply::Block => {
                self.blocked.insert(from);
                let pending: Vec<u32> = self.inc.iter().filter(|i| i.from == from && i.active()).map(|i| i.id).collect();
                for p in pending {
                    self.decline(now, from, p, map_end::DECLINED, "Declined, and you blocked this player's maps");
                }
            }
            Reply::Decline => {
                let receiving = self.inc.iter().any(|i| i.from == from && i.id == id && i.state == InState::Receiving);
                if receiving { self.decline(now, from, id, map_end::CANCELLED, "Cancelled") } else { self.decline(now, from, id, map_end::DECLINED, "Declined") }
            }
            Reply::Accept | Reply::Replace => {
                let Some(i) = self.inc.iter_mut().find(|i| i.from == from && i.id == id && i.state == InState::Prompt) else { return };
                i.replace = reply == Reply::Replace && i.conflict;
                i.state = InState::Receiving;
                i.last_rx = now;
                self.queue.push((from, MapMsg::Accept { id }));
            }
        }
    }

    fn decline(&mut self, now: u64, from: Vip, id: u32, code: u8, note: &str) {
        if let Some(i) = self.inc.iter_mut().find(|i| i.from == from && i.id == id && i.active()) {
            i.state = InState::Over(code, note.to_string());
            i.over_at = now;
            i.buf = vec![];
            self.queue.push((from, MapMsg::End { id, code }));
        }
    }

    /// A message from the relay. `from` is the sender as the relay vouches for it.
    pub fn receive(&mut self, now: u64, from: Vip, msg: MapMsg, lib: &dyn Library) {
        if self.me.is_none() || Some(from) == self.me {
            return;
        }
        match msg {
            MapMsg::Caps { proto, .. } => self.caps |= from == [0; 4] && proto >= generals_proto::MAP_PROTO,
            MapMsg::Hi { game, master, name } => {
                let new = !self.peers.contains_key(&from);
                self.peers.insert(from, Peer { name: display(&name, MAX_PEER_NAME), game, master, seen: now });
                self.hi_wanted |= new; // tell the newcomer we are here too, soon
            }
            MapMsg::Offer { id, game, master, size, players, hash, name } => self.on_offer(now, from, id, game, master, size, players, hash, name, lib),
            MapMsg::Accept { id } => {
                if let Some(o) = self.out.iter_mut().find(|o| o.to == from && o.id == id && o.state == OutState::Waiting) {
                    o.state = OutState::Sending;
                    o.last_ack = now;
                    o.last_resend = now;
                }
            }
            MapMsg::End { id, code } => {
                if let Some(o) = self.out.iter_mut().find(|o| o.to == from && o.id == id && o.open()) {
                    o.finish(now, code, end_text(code));
                }
            }
            MapMsg::Ack { id, next } => {
                if let Some(o) = self.out.iter_mut().find(|o| o.to == from && o.id == id && o.state == OutState::Sending) {
                    if next > o.next_ack && next <= o.chunks() {
                        o.next_ack = next;
                        o.next_send = o.next_send.max(next);
                        o.last_ack = now;
                        o.last_resend = now;
                    }
                }
            }
            MapMsg::Data { id, seq, bytes } => self.on_data(now, from, id, seq, bytes, lib),
            MapMsg::Cancel { id } => {
                if let Some(i) = self.inc.iter_mut().find(|i| i.from == from && i.id == id && i.active()) {
                    i.state = InState::Over(map_end::CANCELLED, "The sender cancelled".into());
                    i.over_at = now;
                    i.buf = vec![];
                }
            }
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn on_offer(&mut self, now: u64, from: Vip, id: u32, game: u8, master: bool, size: u32, players: u8, hash: [u8; 32], name: String, lib: &dyn Library) {
        // The same offer again (the sender repeats it while waiting): once decided, say so again; otherwise it is already shown.
        if let Some(i) = self.inc.iter().find(|i| i.from == from && i.id == id) {
            if let InState::Over(code, _) = i.state {
                self.queue.push((from, MapMsg::End { id, code }));
            }
            return;
        }
        let name = display(&name, MAX_MAP_NAME);
        let no = |s: &mut MapShare, code: u8| s.queue.push((from, MapMsg::End { id, code }));
        if size == 0 || size > MAX_MAP_BYTES || name.is_empty() {
            return no(self, map_end::REFUSED);
        }
        if self.blocked.contains(&from) {
            return no(self, map_end::DECLINED); // the sender is not told they are blocked
        }
        if game != self.game {
            return no(self, map_end::OTHER_GAME);
        }
        let mut entry = Incoming { id, from, game, name: name.clone(), size, players, hash, master, conflict: false, replace: false, state: InState::Prompt, buf: vec![], next: 0, acked: 0, last_rx: now, last_ack: now, dirty: false, offered: now, over_at: 0, installed: None };
        if lib.has(game, &name, &hash) {
            entry.state = InState::Over(map_end::HAVE_IT, "You already have this map".into());
            entry.over_at = now;
            self.inc.push(entry);
            return no(self, map_end::HAVE_IT);
        }
        // One offer at a time from a player, and only a few in all: nobody can fill the screen.
        if self.inc.iter().any(|i| i.from == from && i.active()) || self.inc.iter().filter(|i| i.active()).count() >= MAX_OPEN_IN {
            return no(self, map_end::BUSY);
        }
        entry.conflict = lib.exists(game, &name);
        // Only the relay's own word that the sender is the room's master counts, and only if the player allowed it. A map
        // that would take the name of one the player has is never taken without asking: it waits for a click.
        if self.auto_accept && master && !entry.conflict {
            entry.state = InState::Receiving;
            self.queue.push((from, MapMsg::Accept { id }));
        }
        self.inc.push(entry);
    }

    fn on_data(&mut self, now: u64, from: Vip, id: u32, seq: u32, bytes: Vec<u8>, lib: &dyn Library) {
        let Some(idx) = self.inc.iter().position(|i| i.from == from && i.id == id && i.state == InState::Receiving) else { return };
        let i = &mut self.inc[idx];
        i.last_rx = now;
        let total = map_chunks(i.size);
        if seq != i.next {
            // a repeat or a gap: tell the sender where we are (not more than every ACK_GAP ms)
            if now.saturating_sub(i.last_ack) >= ACK_GAP {
                i.last_ack = now;
                self.queue.push((from, MapMsg::Ack { id, next: i.next }));
            }
            return;
        }
        if seq >= total || bytes.len() != map_chunk_len(i.size, seq) {
            return; // not what the offer promised
        }
        i.buf.extend_from_slice(&bytes);
        i.next += 1;
        i.dirty = true;
        if i.next == total {
            self.complete(now, idx, lib);
        } else if i.next - i.acked >= ACK_EVERY {
            i.acked = i.next;
            i.last_ack = now;
            i.dirty = false;
            self.queue.push((from, MapMsg::Ack { id, next: i.next }));
        }
    }

    /// Everything arrived: check it all, and only then let it into the maps folder.
    fn complete(&mut self, now: u64, idx: usize, lib: &dyn Library) {
        let i = &mut self.inc[idx];
        let packed = std::mem::take(&mut i.buf);
        let (from, id) = (i.from, i.id);
        let outcome = (|| -> Result<Installed, String> {
            let b = unpack(&packed)?;
            if content_hash(b.game, &b.name, &b.files) != i.hash || b.game != i.game || b.name.to_lowercase() != i.name.to_lowercase() {
                return Err("it was not the map that was offered".into());
            }
            validate(&b)?;
            lib.install(&b, i.replace)
        })();
        i.over_at = now;
        match outcome {
            Ok(done) => {
                i.state = InState::Over(
                    map_end::DELIVERED,
                    if done.replaced { format!("Saved as {}, in place of your map of that name (the old one is kept in the backup folder)", done.name) } else if done.renamed { format!("Saved as {}, because you have a different map with that name", done.name) } else { format!("Saved as {}", done.name) },
                );
                i.installed = Some(done);
                self.queue.push((from, MapMsg::Ack { id, next: map_chunks(i.size) }));
                self.queue.push((from, MapMsg::End { id, code: map_end::DELIVERED }));
            }
            Err(e) => {
                i.state = InState::Over(map_end::BAD, format!("Not saved: {e}"));
                self.queue.push((from, MapMsg::End { id, code: map_end::BAD }));
            }
        }
    }

    // ---- time

    /// Called every few tens of milliseconds.
    pub fn tick(&mut self, now: u64) {
        if self.me.is_none() {
            return;
        }
        let mut credit = (now.saturating_sub(self.last_tick) / MS_PER_CHUNK).clamp(1, 40);
        self.last_tick = now;
        // presence
        let due = self.last_hi.is_none_or(|t| now.saturating_sub(t) >= HI_EVERY || (self.hi_wanted && now.saturating_sub(t) >= HI_MIN_GAP));
        if due {
            self.queue.push(([0; 4], MapMsg::Hi { game: self.game, master: false, name: self.name.clone() }));
            self.last_hi = Some(now);
            self.hi_wanted = false;
        }
        self.peers.retain(|_, p| now.saturating_sub(p.seen) < PEER_STALE);
        // outgoing
        for o in self.out.iter_mut().filter(|o| o.open()) {
            match o.state {
                OutState::Waiting => {
                    if now.saturating_sub(o.started) > OFFER_WAIT {
                        o.finish(now, FAILED, "No answer");
                        self.queue.push((o.to, MapMsg::Cancel { id: o.id }));
                    } else if now.saturating_sub(o.last_offer) >= OFFER_REPEAT {
                        o.last_offer = now;
                        self.queue.push((o.to, o.offer()));
                    }
                }
                OutState::Sending => {
                    let total = o.chunks();
                    if o.next_ack >= total {
                        if now.saturating_sub(o.last_ack) > END_WAIT {
                            o.finish(now, FAILED, "Sent, but they never said it arrived");
                        }
                        continue;
                    }
                    if now.saturating_sub(o.last_ack) > STALL {
                        o.finish(now, FAILED, "The connection to them dropped");
                        self.queue.push((o.to, MapMsg::Cancel { id: o.id }));
                        continue;
                    }
                    if now.saturating_sub(o.last_resend) >= RESEND_AFTER && o.next_send > o.next_ack {
                        o.next_send = o.next_ack; // nothing came back: start again from the first one they lack
                        o.last_resend = now;
                    }
                    while credit > 0 && o.next_send < total && o.next_send < o.next_ack + WINDOW {
                        let seq = o.next_send;
                        let (a, len) = (seq as usize * MAP_CHUNK, map_chunk_len(o.data.len() as u32, seq));
                        self.queue.push((o.to, MapMsg::Data { id: o.id, seq, bytes: o.data[a..a + len].to_vec() }));
                        o.next_send += 1;
                        credit -= 1;
                    }
                }
                OutState::Over(..) => {}
            }
        }
        // incoming
        let mut say = vec![];
        for i in self.inc.iter_mut().filter(|i| i.active()) {
            match i.state {
                InState::Prompt if now.saturating_sub(i.offered) > PROMPT_WAIT => {
                    i.state = InState::Over(map_end::DECLINED, "You did not answer in time".into());
                    i.over_at = now;
                    say.push((i.from, MapMsg::End { id: i.id, code: map_end::DECLINED }));
                }
                InState::Receiving if now.saturating_sub(i.last_rx) > RECV_IDLE => {
                    i.state = InState::Over(map_end::CANCELLED, "The connection dropped".into());
                    i.over_at = now;
                    i.buf = vec![];
                    say.push((i.from, MapMsg::End { id: i.id, code: map_end::CANCELLED }));
                }
                InState::Receiving if i.dirty && now.saturating_sub(i.last_ack) >= ACK_GAP => {
                    i.acked = i.next;
                    i.last_ack = now;
                    i.dirty = false;
                    say.push((i.from, MapMsg::Ack { id: i.id, next: i.next }));
                }
                _ => {}
            }
        }
        self.queue.extend(say);
        // finished entries stay a while so the page can say how it went
        self.out.retain(|o| o.open() || now.saturating_sub(o.over_at) < KEEP_DONE);
        self.inc.retain(|i| i.active() || now.saturating_sub(i.over_at) < KEEP_DONE);
    }

    // ---- the page

    pub fn view(&self, now: u64) -> ShareView {
        let mine = self.game;
        ShareView {
            supported: self.supported(now),
            connected: self.me.is_some(),
            game: game_id(mine),
            auto_accept: self.auto_accept,
            peers: self.peers.iter().map(|(v, p)| PeerView { vip: dotted(*v), name: if p.name.is_empty() { format!("Player {}", v[3]) } else { p.name.clone() }, game: game_id(p.game), master: p.master, can_receive: p.game == mine }).collect(),
            incoming: self
                .inc
                .iter()
                .map(|i| InView {
                    id: i.id,
                    from: dotted(i.from),
                    from_name: self.peer_name(i.from),
                    name: i.name.clone(),
                    game: game_id(i.game),
                    size: i.size,
                    players: i.players,
                    master: i.master,
                    conflict: i.conflict,
                    state: match &i.state {
                        InState::Prompt => "prompt",
                        InState::Receiving => "receiving",
                        InState::Over(c, _) if *c == map_end::DELIVERED => "done",
                        InState::Over(c, _) if *c == map_end::HAVE_IT => "have",
                        InState::Over(..) => "failed",
                    },
                    percent: (i.next as u64 * 100 / map_chunks(i.size).max(1) as u64) as u8,
                    note: match &i.state {
                        InState::Over(_, n) => n.clone(),
                        _ => String::new(),
                    },
                    installed: i.installed.as_ref().map(|d| d.name.clone()),
                    seconds_left: if i.state == InState::Prompt { PROMPT_WAIT.saturating_sub(now.saturating_sub(i.offered)) / 1000 } else { 0 },
                })
                .collect(),
            outgoing: self
                .out
                .iter()
                .map(|o| OutView {
                    id: o.id,
                    to: dotted(o.to),
                    to_name: self.peer_name(o.to),
                    name: o.name.clone(),
                    game: game_id(o.game),
                    state: match &o.state {
                        OutState::Waiting => "waiting",
                        OutState::Sending => "sending",
                        OutState::Over(c, _) if *c == map_end::DELIVERED => "done",
                        OutState::Over(c, _) if *c == map_end::HAVE_IT => "have",
                        OutState::Over(c, _) if *c == map_end::DECLINED || *c == map_end::BLOCKED => "declined",
                        OutState::Over(c, _) if *c == map_end::CANCELLED => "cancelled",
                        OutState::Over(..) => "failed",
                    },
                    percent: (o.next_ack as u64 * 100 / o.chunks().max(1) as u64) as u8,
                    note: match &o.state {
                        OutState::Over(_, n) => n.clone(),
                        _ => String::new(),
                    },
                })
                .collect(),
        }
    }

    /// An address a page sent back (`"10.77.0.2"`).
    pub fn parse_vip(s: &str) -> Option<Vip> {
        let p: Vec<u8> = s.split('.').map(|x| x.parse().ok()).collect::<Option<_>>()?;
        <[u8; 4]>::try_from(p).ok()
    }
}

fn game_id(g: u8) -> &'static str {
    if g == MAP_RA2 { "ra2" } else { "generals" }
}

/// What the sender is told about how the receiver ended it.
pub fn end_text(code: u8) -> &'static str {
    match code {
        map_end::DELIVERED => "Delivered",
        map_end::DECLINED | map_end::BLOCKED => "Declined",
        map_end::HAVE_IT => "Already has this map",
        map_end::BUSY => "Busy with another map. Try again in a moment",
        map_end::OTHER_GAME => "Has the other game selected",
        map_end::BAD => "Could not use the map (it failed their checks)",
        map_end::CANCELLED => "Cancelled",
        _ => "Not allowed (too big, or over the server's limits)",
    }
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct PeerView {
    pub vip: String,
    pub name: String,
    pub game: &'static str,
    pub master: bool,
    /// Same game as ours: only these can be sent to.
    pub can_receive: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct InView {
    pub id: u32,
    pub from: String,
    pub from_name: String,
    pub name: String,
    pub game: &'static str,
    pub size: u32,
    pub players: u8,
    pub master: bool,
    /// The player has a different map with this name: the page asks whether to replace it or keep both.
    pub conflict: bool,
    /// prompt, receiving, done, have, failed
    pub state: &'static str,
    pub percent: u8,
    pub note: String,
    /// The name it was saved under, once it was.
    pub installed: Option<String>,
    pub seconds_left: u64,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct OutView {
    pub id: u32,
    pub to: String,
    pub to_name: String,
    pub name: String,
    pub game: &'static str,
    /// waiting, sending, done, have, declined, cancelled, failed
    pub state: &'static str,
    pub percent: u8,
    pub note: String,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct ShareView {
    /// `None` while we are still finding out whether the server shares maps.
    pub supported: Option<bool>,
    pub connected: bool,
    pub game: &'static str,
    pub auto_accept: bool,
    pub peers: Vec<PeerView>,
    pub incoming: Vec<InView>,
    pub outgoing: Vec<OutView>,
}

#[cfg(test)]
mod tests;
