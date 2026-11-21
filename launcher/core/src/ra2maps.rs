//! Red Alert 2 and Yuri's Revenge maps (`.map`, `.mpr`, `.yrm`, `.yro`): INI text with the preview picture, the start
//! positions and the terrain inside. Shown the same way as Generals maps (`mapview::MapView`), so the page can draw
//! both with the same canvas code.
//!
//! What is read (see modenc.renegadeprojects.com, pages Map, Waypoints, PreviewPack and IsoMapPack5):
//! - `[Basic] Name`, `[Map] Size` (x,y,w,h of the whole map) and `[Map] LocalSize` (the visible, playable part).
//! - `[Waypoints]`: keys 0 to 7 are the start positions, each value is `y * 1000 + x` (the last three digits are x).
//! - `[Preview] Size=0,0,w,h` and `[PreviewPack]`: its lines joined in key order are base64 of blocks of (u16 packed
//!   size, u16 unpacked size, LZO1X data), unpacking to w*h RGB pixels, top row first (modenc says BGR, but the
//!   ccmaps-net writer, the CnCNet client and real maps are red first).
//! - `[IsoMapPack5]`: the same blocks, unpacking to 11-byte cells (x u16, y u16, tile i32, subtile, level, ice), read
//!   only for the height of the start cells.
use crate::mapview::{base64, Image, MapInfo, MapView, Start};
use std::collections::{BTreeMap, HashMap};
use std::path::{Path, PathBuf};
use std::io::Read;
use std::process::Command;
use std::time::UNIX_EPOCH;

/// Map files larger than this are not read. Real ones are a few hundred kB to a few MB.
const MAX_FILE: u64 = 16 << 20;
/// The largest unpacked preview or terrain accepted.
const MAX_UNPACKED: usize = 64 << 20;
/// A cell of the isometric grid on screen, in pixels (Tiberian Sun's size; only the 2:1 shape matters here).
const CELL_W: i64 = 48;
const CELL_H: i64 = 24;
const EXTENSIONS: [&str; 4] = ["map", "mpr", "yrm", "yro"];
/// What FinalAlert 2 writes for a map whose preview the author hid: a picture of nothing, so show none.
const HIDDEN_PREVIEW: &str = "yAsAIAXQ5PDQ5PDQ6JQATAEE6PDQ4PDI4JgBTAFEAkgAJyAATAG0AydEAEABpAJIA0wBVA";

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Game {
    Ra2,
    Yr,
}

#[derive(Debug)]
pub struct Ra2Map {
    pub name: String,
    pub game: Game,
    /// `LocalSize` in cells.
    pub cells_w: u32,
    pub cells_h: u32,
    /// `width`/`height` are the area the preview shows (the visible part when there is no preview) in screen pixels,
    /// so their ratio is the picture's; starts are fractions of it, y up, as in `mapview`.
    pub info: MapInfo,
    /// RGBA, top row first.
    pub preview: Option<Image>,
}

/// What the page gets: the same fields as a Generals map, plus which game and the size in cells.
#[derive(Debug, Clone, serde::Serialize)]
pub struct Ra2View {
    #[serde(flatten)]
    pub map: MapView,
    pub game: Game,
    pub cells_w: u32,
    pub cells_h: u32,
}

#[derive(Debug, Clone, PartialEq, serde::Serialize)]
pub struct Ra2Entry {
    pub path: PathBuf,
    pub name: String,
    pub players: u8,
    pub game: Game,
    pub size_kb: u64,
    /// Exact `Map=` value for the engine (`xmp01t4.map`, or a path to a plain INI).
    pub map: String,
}

/// The sections asked for, by lower-case name, each as (lower-case key, value) in file order. Comments (`;`) are cut.
fn ini(text: &str, wanted: &[&str]) -> HashMap<String, Vec<(String, String)>> {
    let mut out: HashMap<String, Vec<(String, String)>> = HashMap::new();
    let mut cur: Option<String> = None;
    for line in text.lines() {
        let line = line.split(';').next().unwrap_or("").trim();
        if let Some(rest) = line.strip_prefix('[') {
            let name = rest.split(']').next().unwrap_or("").trim().to_ascii_lowercase();
            cur = wanted.contains(&name.as_str()).then_some(name);
        } else if let (Some(sec), Some((k, v))) = (&cur, line.split_once('=')) {
            out.entry(sec.clone()).or_default().push((k.trim().to_ascii_lowercase(), v.trim().to_string()));
        }
    }
    out
}

fn get<'a>(s: &'a HashMap<String, Vec<(String, String)>>, sec: &str, key: &str) -> Option<&'a str> {
    s.get(sec)?.iter().rev().find(|(k, _)| k == key).map(|(_, v)| v.as_str())
}

fn rect(v: Option<&str>) -> Option<[i64; 4]> {
    let n: Vec<i64> = v?.split(',').map(|p| p.trim().parse().ok()).collect::<Option<_>>()?;
    n.try_into().ok()
}

fn unbase64(text: &str) -> Result<Vec<u8>, String> {
    let mut out = Vec::with_capacity(text.len() / 4 * 3);
    let (mut acc, mut bits) = (0u32, 0);
    for c in text.bytes() {
        let v = match c {
            b'A'..=b'Z' => c - b'A',
            b'a'..=b'z' => c - b'a' + 26,
            b'0'..=b'9' => c - b'0' + 52,
            b'+' => 62,
            b'/' => 63,
            b'=' | b' ' | b'\t' | b'\r' | b'\n' => continue,
            _ => return Err("the map's packed data is not base64".into()),
        };
        acc = acc << 6 | v as u32;
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            out.push((acc >> bits) as u8);
        }
    }
    Ok(out)
}

/// A packed section's lines joined in numeric key order, unbase64'd and unpacked block by block.
fn unpack(lines: &[(String, String)]) -> Result<Vec<u8>, String> {
    let mut lines: Vec<_> = lines.iter().filter_map(|(k, v)| Some((k.parse::<u32>().ok()?, v.as_str()))).collect();
    lines.sort_by_key(|l| l.0);
    let data = unbase64(&lines.iter().map(|l| l.1).collect::<String>())?;
    let mut out = Vec::new();
    let mut p = 0;
    while p + 4 <= data.len() {
        let packed = u16::from_le_bytes([data[p], data[p + 1]]) as usize;
        let size = u16::from_le_bytes([data[p + 2], data[p + 3]]) as usize;
        if packed == 0 || size == 0 {
            break;
        }
        let block = data.get(p + 4..p + 4 + packed).ok_or("the map's packed data is cut short")?;
        if out.len() + size > MAX_UNPACKED {
            return Err("the map's packed data is too big".into());
        }
        let b = crate::lzo::decompress(block, size)?;
        if b.len() != size {
            return Err("a packed block of the map is the wrong size".into());
        }
        out.extend_from_slice(&b);
        p += 4 + packed;
    }
    Ok(out)
}

/// The height (level) of each cell, from `[IsoMapPack5]`.
fn levels(lines: &[(String, String)]) -> HashMap<(i64, i64), i64> {
    let cells = unpack(lines).unwrap_or_default();
    cells.as_chunks::<11>().0.iter().map(|c| ((u16::from_le_bytes([c[0], c[1]]) as i64, u16::from_le_bytes([c[2], c[3]]) as i64), c[9] as i64)).collect()
}

/// Where the middle of cell (x, y) at height `level` lies in `frame` (x,y,w,h in cells and rows), as fractions with y
/// up. The isometric projection is the one the CnCNet client uses for its start markers (xna-cncnet-client,
/// `Map.GetIsoTilePixelCoord`), plus half a cell to reach the middle: screen x = (x - y + W - 1) half cells, screen
/// y = (x + y - W - 1 - level) half rows, W the width in `[Map] Size`.
fn place(x: i64, y: i64, level: i64, map_w: i64, frame: [i64; 4]) -> (f32, f32) {
    let px = (x - y + map_w) * CELL_W / 2 - frame[0] * CELL_W;
    let py = (x + y - map_w - level) * CELL_H / 2 - frame[1] * CELL_H;
    let fx = px as f64 / (frame[2] * CELL_W) as f64;
    let fy = py as f64 / (frame[3] * CELL_H) as f64;
    (fx.clamp(0.0, 1.0) as f32, (1.0 - fy).clamp(0.0, 1.0) as f32)
}

fn game_of(file_name: &str, theater: Option<&str>) -> Game {
    let ext = Path::new(file_name).extension().map(|e| e.to_string_lossy().to_ascii_lowercase()).unwrap_or_default();
    // FinalAlert 2 saves Yuri's Revenge maps as .yrm/.yro; these three theaters only exist in Yuri's Revenge
    let yr_theater = theater.is_some_and(|t| ["newurban", "lunar", "desert"].contains(&t.to_ascii_lowercase().as_str()));
    if ext == "yrm" || ext == "yro" || yr_theater {
        Game::Yr
    } else {
        Game::Ra2
    }
}

/// The start cells: `[Waypoints]` 0 to 7, stopping at the first missing one (how the game and the CnCNet client count
/// players).
fn start_cells(s: &HashMap<String, Vec<(String, String)>>) -> Vec<(i64, i64)> {
    (0..8).map_while(|i| get(s, "waypoints", &i.to_string())?.parse::<i64>().ok().filter(|&v| v >= 0)).map(|v| (v % 1000, v / 1000)).collect()
}

fn read(bytes: &[u8], file_name: &str, full: bool) -> Result<Ra2Map, String> {
    let text = String::from_utf8_lossy(bytes);
    let wanted: &[&str] = if full { &["basic", "map", "waypoints", "preview", "previewpack", "isomappack5"] } else { &["basic", "map", "waypoints"] };
    let s = ini(&text, wanted);
    let size = rect(get(&s, "map", "size")).ok_or("not a Red Alert 2 map: no [Map] Size")?;
    let local = rect(get(&s, "map", "localsize")).unwrap_or(size);
    if local[2] <= 0 || local[3] <= 0 {
        return Err("the map's LocalSize is empty".into());
    }
    let stem = Path::new(file_name).file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
    let name = get(&s, "basic", "name").filter(|n| !n.is_empty()).map(str::to_string).unwrap_or(stem);
    let cells = start_cells(&s);
    let lv = if full { s.get("isomappack5").map(|l| levels(l)).unwrap_or_default() } else { HashMap::new() };
    let preview = if full { preview(&s) } else { None };
    // Red Alert 2's previews show the whole map, Yuri's Revenge's the LocalSize part (modenc, PreviewPack): about two
    // pixels per cell across, so the preview's width says which. Without one, the visible part.
    let frame = match &preview {
        Some(p) if (p.w as i64 - 2 * size[2]).abs() < (p.w as i64 - 2 * local[2]).abs() => [0, 0, size[2], size[3]],
        _ => local,
    };
    let starts = cells
        .iter()
        .enumerate()
        .map(|(i, &(x, y))| {
            let (fx, fy) = place(x, y, *lv.get(&(x, y)).unwrap_or(&0), size[2], frame);
            Start { number: i as u8 + 1, x: fx, y: fy }
        })
        .collect();
    Ok(Ra2Map {
        name,
        game: game_of(file_name, get(&s, "map", "theater")),
        cells_w: local[2] as u32,
        cells_h: local[3] as u32,
        info: MapInfo { width: (frame[2] * CELL_W) as f32, height: (frame[3] * CELL_H) as f32, starts },
        preview,
    })
}

fn preview(s: &HashMap<String, Vec<(String, String)>>) -> Option<Image> {
    let [_, _, w, h] = rect(get(s, "preview", "size"))?;
    let lines = s.get("previewpack")?;
    if w <= 0 || h <= 0 || (w * h * 3) as usize > MAX_UNPACKED || get(s, "previewpack", "1") == Some(HIDDEN_PREVIEW) {
        return None;
    }
    let rgb = unpack(lines).ok().filter(|p| p.len() == (w * h * 3) as usize)?;
    let rgba = rgb.as_chunks::<3>().0.iter().flat_map(|p| [p[0], p[1], p[2], 255]).collect();
    Some(Image { w: w as u32, h: h as u32, rgba })
}

/// Everything about a map from its bytes; `file_name` gives the game for .yrm/.yro.
pub fn parse(bytes: &[u8], file_name: &str) -> Result<Ra2Map, String> {
    read(bytes, file_name, true)
}

fn read_file(path: &Path) -> Result<Vec<u8>, String> {
    let len = std::fs::metadata(path).map_err(|e| e.to_string())?.len();
    if len > MAX_FILE {
        return Err("the map file is too big".into());
    }
    std::fs::read(path).map_err(|e| e.to_string())
}

/// A map file, ready for the page.
pub fn view(path: &Path) -> Result<Ra2View, String> {
    let bytes = read_file(path)?;
    let m = parse(&bytes, &path.file_name().unwrap_or_default().to_string_lossy())?;
    let (image_w, image_h, rgba) = m.preview.map(|i| (i.w, i.h, base64(&i.rgba))).unwrap_or((0, 0, String::new()));
    let map = MapView { name: m.name, image_w, image_h, rgba, info: Some(m.info), size_kb: bytes.len() as u64 / 1024 };
    Ok(Ra2View { map, game: m.game, cells_w: m.cells_w, cells_h: m.cells_h })
}

#[derive(serde::Serialize, serde::Deserialize, Clone, PartialEq)]
struct Known {
    size: u64,
    mtime: u64,
    name: String,
    players: u8,
    game: Game,
}

fn mtime_secs(meta: &std::fs::Metadata) -> u64 {
    meta.modified().ok().and_then(|t| t.duration_since(UNIX_EPOCH).ok()).map_or(0, |d| d.as_secs())
}

/// A fingerprint of the game archives that `--extract-maps` reads: their names, sizes and change times.
pub fn archive_stamp(game_dir: &Path) -> String {
    let mut lines = Vec::new();
    for name in ["multimd.mix", "langmd.mix"] {
        if let Some(p) = std::fs::read_dir(game_dir).ok().into_iter().flatten().flatten().map(|e| e.path())
            .find(|p| p.file_name().is_some_and(|n| n.to_string_lossy().eq_ignore_ascii_case(name)))
        {
            if let Ok(m) = std::fs::metadata(&p) {
                lines.push(format!("{}\t{}\t{}", p.file_name().unwrap().to_string_lossy(), m.len(), mtime_secs(&m)));
            }
        }
    }
    let mut packs: Vec<_> = std::fs::read_dir(game_dir).into_iter().flatten().flatten().map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|e| e.eq_ignore_ascii_case("mmx") || e.eq_ignore_ascii_case("yro")))
        .collect();
    packs.sort();
    for p in packs {
        if let Ok(m) = std::fs::metadata(&p) {
            lines.push(format!("{}\t{}\t{}", p.file_name().unwrap().to_string_lossy(), m.len(), mtime_secs(&m)));
        }
    }
    lines.join("\n")
}

#[derive(serde::Deserialize)]
struct IndexRow {
    file: String,
    name: String,
    #[allow(dead_code)]
    source: String,
    map: String,
    players: u8,
}

/// Run the bundled engine's `--extract-maps` into `out` when the archive stamp changed (or the index is missing).
pub fn refresh_extract(engine: &Path, game_dir: &Path, out: &Path) -> Result<(), String> {
    let stamp = archive_stamp(game_dir);
    let stamp_path = out.join("stamp.txt");
    let index = out.join("index.json");
    let old = std::fs::read_to_string(&stamp_path).unwrap_or_default();
    if old == stamp && index.is_file() {
        return Ok(());
    }
    std::fs::create_dir_all(out).map_err(|e| e.to_string())?;
    let status = Command::new(engine)
        .arg("--data").arg(game_dir)
        .arg("--extract-maps").arg(out)
        .status()
        .map_err(|e| format!("could not run {}: {e}", engine.display()))?;
    if !status.success() {
        return Err(format!("--extract-maps failed ({status})"));
    }
    std::fs::write(&stamp_path, stamp).map_err(|e| e.to_string())?;
    Ok(())
}

/// Maps written by `--extract-maps` (`index.json` + plain INI files under `out`).
pub fn list_extracted(out: &Path) -> Vec<Ra2Entry> {
    let text = match std::fs::read_to_string(out.join("index.json")) {
        Ok(t) => t,
        Err(_) => return Vec::new(),
    };
    let rows: Vec<IndexRow> = match serde_json::from_str(&text) {
        Ok(r) => r,
        Err(_) => return Vec::new(),
    };
    let mut out_list = Vec::new();
    for r in rows {
        if r.players < 2 {
            continue;
        }
        let path = out.join(&r.file);
        let size = std::fs::metadata(&path).map(|m| m.len()).unwrap_or(0);
        let game = read_file(&path).ok().and_then(|b| read(&b, &r.file, false).ok()).map(|m| m.game).unwrap_or(Game::Yr);
        out_list.push(Ra2Entry {
            path,
            name: r.name,
            players: r.players,
            game,
            size_kb: size / 1024,
            map: r.map,
        });
    }
    out_list
}

/// Loose plain map files in each of `dirs`, plus (when `extract` is set) the engine's extracted catalog. Name, players
/// and game for loose files are kept in `cache` (JSON). Extracted maps win a name clash with an official multimd entry
/// only when the loose folder has the same file name; loose maps always win over the extract cache.
pub fn list(dirs: &[PathBuf], cache: &Path) -> Vec<Ra2Entry> {
    list_all(dirs, cache, None)
}

/// Like `list`, then merges `list_extracted(extract)` (skipping names already present from loose dirs).
pub fn list_all(dirs: &[PathBuf], cache: &Path, extract: Option<&Path>) -> Vec<Ra2Entry> {
    let old: BTreeMap<String, Known> = std::fs::read_to_string(cache).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default();
    let mut new = BTreeMap::new();
    let mut out = Vec::new();
    let mut seen = std::collections::HashSet::new();
    for dir in dirs {
        for path in std::fs::read_dir(dir).into_iter().flatten().flatten().map(|e| e.path()) {
            let is_map = path.extension().is_some_and(|e| EXTENSIONS.iter().any(|x| e.eq_ignore_ascii_case(x)));
            // Plain INI only: .mmx/.yro packs are opened by --extract-maps (.yro can be either).
            let Some(meta) = is_map.then(|| std::fs::metadata(&path).ok()).flatten().filter(|m| m.is_file()) else { continue };
            if b_looks_like_mix(&path) {
                continue;
            }
            let mtime = mtime_secs(&meta);
            let key = path.to_string_lossy().into_owned();
            let known = match old.get(&key) {
                Some(k) if k.size == meta.len() && k.mtime == mtime => Some(k.clone()),
                _ => read_file(&path).ok().and_then(|b| read(&b, &path.file_name().unwrap_or_default().to_string_lossy(), false).ok()).map(|m| Known {
                    size: meta.len(),
                    mtime,
                    name: m.name,
                    players: m.info.starts.len() as u8,
                    game: m.game,
                }),
            };
            if let Some(k) = known {
                let file = path.file_name().unwrap_or_default().to_string_lossy().to_ascii_lowercase();
                seen.insert(file);
                let map = path.to_string_lossy().replace('\\', "/");
                out.push(Ra2Entry { path: path.clone(), name: k.name.clone(), players: k.players, game: k.game, size_kb: k.size / 1024, map });
                new.insert(key, k);
            }
        }
    }
    if new != old {
        if let Some(d) = cache.parent() {
            let _ = std::fs::create_dir_all(d);
        }
        let _ = serde_json::to_vec(&new).map(|b| std::fs::write(cache, b));
    }
    if let Some(ex) = extract {
        for e in list_extracted(ex) {
            let file = e.path.file_name().unwrap_or_default().to_string_lossy().to_ascii_lowercase();
            if seen.insert(file) {
                out.push(e);
            }
        }
    }
    out.sort_by_key(|e| e.name.to_lowercase());
    out
}

fn b_looks_like_mix(path: &Path) -> bool {
    let Ok(mut f) = std::fs::File::open(path) else { return false };
    let mut head = [0u8; 4];
    if f.read_exact(&mut head).is_err() {
        return false;
    }
    // New MIX: flags dword with low 16 bits zero. Plain maps start with printable/`[`.
    head[0] == 0 && head[1] == 0 && head[2] != b'[' && head[3] != b'['
}

#[cfg(test)]
mod tests {
    use super::*;

    /// `[PreviewPack]` lines for the 16 by 8 test picture, packed by liblzo2, cut into lines as FinalAlert 2 does.
    fn preview_pack() -> String {
        let v = &crate::lzo::tests::GRAD_999;
        let mut block = (v.len() as u16).to_le_bytes().to_vec();
        block.extend_from_slice(&384u16.to_le_bytes());
        block.extend_from_slice(v);
        let b = base64(&block);
        b.as_bytes().chunks(70).enumerate().map(|(i, c)| format!("{}={}\n", i + 1, std::str::from_utf8(c).unwrap())).collect()
    }

    /// `[IsoMapPack5]` with one cell (40, 30) at level 4, as a literal-only LZO stream (17 + n, n bytes, end marker).
    fn iso_pack() -> String {
        let mut cell = vec![40, 0, 30, 0, 0, 0, 0, 0, 0, 4, 0];
        let mut lzo = vec![17 + cell.len() as u8];
        lzo.append(&mut cell);
        lzo.extend_from_slice(&[0x11, 0, 0]);
        let mut block = (lzo.len() as u16).to_le_bytes().to_vec();
        block.extend_from_slice(&11u16.to_le_bytes());
        block.extend_from_slice(&lzo);
        format!("1={}\n", base64(&block))
    }

    fn sample(with_preview: bool) -> String {
        let pack = if with_preview { format!("[Preview]\nSize=0,0,16,8\n\n[PreviewPack]\n{}\n", preview_pack()) } else { String::new() };
        format!(
            "; made by hand\n{pack}[Basic]\nName=Two Rivers ; a comment\nPlayer=Americans\n\n[Map]\nSize=0,0,50,50\nLocalSize=2,4,46,42\nTheater=TEMPERATE\n\n\
             [IsoMapPack5]\n{}\n[Waypoints]\n0=30040\n1=20010\n3=1005\n98=5005\n",
            iso_pack()
        )
    }

    #[test]
    fn reads_name_starts_and_preview() {
        let m = parse(sample(true).as_bytes(), "rivers.map").unwrap();
        assert_eq!(m.name, "Two Rivers");
        assert_eq!(m.game, Game::Ra2);
        assert_eq!((m.cells_w, m.cells_h), (46, 42));
        assert_eq!(m.info.starts.len(), 2, "waypoint 2 is missing, so 3 is not a start");
        // a 16 pixel wide preview is nearer twice LocalSize's width than twice Size's, so the frame is LocalSize.
        // The middle of cell (40, 30) at level 4: x = (40-30+50)*24 - 2*48 = 1344 of 46*48, y = (40+30-50-4)*12 - 4*24 = 96
        // of 42*24 from the top
        let s = &m.info.starts[0];
        assert_eq!(s.number, 1);
        assert!((s.x - 1344.0 / 2208.0).abs() < 1e-6 && (s.y - (1.0 - 96.0 / 1008.0)).abs() < 1e-6, "{s:?}");
        // cell (10, 20) at level 0: x = (10-20+50)*24 - 96 = 864, y = (10+20-50)*12 - 96 < 0, clamped to the top
        let s = &m.info.starts[1];
        assert!((s.x - 864.0 / 2208.0).abs() < 1e-6 && s.y == 1.0, "{s:?}");
        assert_eq!((m.info.width, m.info.height), (2208.0, 1008.0));
        let p = m.preview.expect("the preview is unpacked");
        assert_eq!((p.w, p.h, p.rgba.len()), (16, 8, 16 * 8 * 4));
        assert_eq!(&p.rgba[..4], &[0, 0, 128, 255], "the pixels are red, green, blue");
        assert_eq!(&p.rgba[(8 * 16 - 1) * 4..], &[180, 180, 128, 255]);
    }

    #[test]
    fn yuri_maps_and_maps_without_a_preview() {
        let m = parse(sample(false).as_bytes(), "rivers.yrm").unwrap();
        assert_eq!(m.game, Game::Yr);
        assert!(m.preview.is_none());
        let lunar = sample(false).replace("TEMPERATE", "LUNAR");
        assert_eq!(parse(lunar.as_bytes(), "x.map").unwrap().game, Game::Yr);
        // a damaged preview is no preview, not an error
        let broken = sample(true).replace("[PreviewPack]\n1=", "[PreviewPack]\n1=AAAA");
        assert!(parse(broken.as_bytes(), "x.map").unwrap().preview.is_none());
        let hidden = format!("[Preview]\nSize=0,0,16,8\n[PreviewPack]\n1={HIDDEN_PREVIEW}\n{}", sample(false));
        assert!(parse(hidden.as_bytes(), "x.map").unwrap().preview.is_none());
        assert!(parse(b"not a map", "x.map").is_err());
        assert!(parse(&[0xff, 0xfe, 0x00, b'['], "x.map").is_err());
        // a nameless map is called after its file, and non-UTF-8 bytes do not stop it being read
        let mut odd = sample(false).replace("Name=Two Rivers ; a comment\n", "").into_bytes();
        odd.extend_from_slice(b"\n[Junk]\nx=\xff\xfe\n");
        assert_eq!(parse(&odd, "Odd One.mpr").unwrap().name, "Odd One");
    }

    #[test]
    fn view_and_list_with_its_cache() {
        let t = std::env::temp_dir().join(format!("ra2maps-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        std::fs::create_dir_all(t.join("game")).unwrap();
        std::fs::create_dir_all(t.join("mine")).unwrap();
        std::fs::write(t.join("game").join("rivers.mpr"), sample(true)).unwrap();
        std::fs::write(t.join("game").join("ra2.ini"), "[Map]\nSize=0,0,1,1\n").unwrap();
        std::fs::write(t.join("game").join("broken.map"), "nothing").unwrap();
        std::fs::write(t.join("mine").join("moon.yrm"), sample(false).replace("Two Rivers", "Moon")).unwrap();

        let v = view(&t.join("game").join("rivers.mpr")).unwrap();
        assert_eq!((v.map.image_w, v.map.image_h, v.game), (16, 8, Game::Ra2));
        assert_eq!(v.map.rgba.len(), base64(&[0; 16 * 8 * 4]).len());
        let json = serde_json::to_value(&v).unwrap();
        assert!(json["rgba"].is_string() && json["info"]["starts"].is_array() && json["game"] == "ra2", "flattened like a Generals map");

        let cache = t.join("lib").join("ra2maps.json");
        let dirs = [t.join("game"), t.join("mine")];
        let l = list(&dirs, &cache);
        assert_eq!(l.iter().map(|e| (e.name.as_str(), e.players, e.game)).collect::<Vec<_>>(), [("Moon", 2, Game::Yr), ("Two Rivers", 2, Game::Ra2)]);
        assert!(l.iter().any(|e| e.map.ends_with("rivers.mpr")), "loose Map= is the file path: {:?}", l[1].map);
        assert!(cache.is_file());
        // answered from the cache while size and time are unchanged, even if the file could no longer be read
        let before = std::fs::metadata(t.join("mine").join("moon.yrm")).unwrap().modified().unwrap();
        let same = sample(false).replace("Two Rivers", "Moon").replace("[Map]", "[Mxp]");
        std::fs::write(t.join("mine").join("moon.yrm"), same).unwrap();
        std::fs::File::options().write(true).open(t.join("mine").join("moon.yrm")).unwrap().set_modified(before).unwrap();
        assert_eq!(list(&dirs, &cache).len(), 2);

        // extracted catalog: index.json + a plain INI; list_all merges it
        let ex = t.join("extract");
        std::fs::create_dir_all(&ex).unwrap();
        std::fs::write(ex.join("xmp01t4.map"), sample(false).replace("Two Rivers", "South Pacific")).unwrap();
        std::fs::write(
            ex.join("index.json"),
            r#"[{"file":"xmp01t4.map","name":"South Pacific (2-4)","source":"multimd.mix/xmp01t4.map","map":"xmp01t4.map","players":2}]"#,
        ).unwrap();
        let all = list_all(&dirs, &cache, Some(&ex));
        assert_eq!(all.len(), 3);
        let off = all.iter().find(|e| e.map == "xmp01t4.map").expect("official Map=");
        assert_eq!((off.name.as_str(), off.players), ("South Pacific (2-4)", 2));
        assert_eq!(archive_stamp(&t.join("empty_missing")), "");
        let _ = std::fs::remove_dir_all(t);
    }

    #[test]
    fn parse_every_extracted_map_when_asked() {
        // `RA2_MAPS_EXTRACT=/tmp/ra2-maps-extract cargo test -p generals-core parse_every_extracted -- --exact`
        let Some(dir) = std::env::var_os("RA2_MAPS_EXTRACT").map(PathBuf::from) else { return };
        let rows = list_extracted(&dir);
        assert!(!rows.is_empty(), "no maps in {}", dir.display());
        let mut ok = 0usize;
        let mut err = Vec::new();
        for e in &rows {
            match view(&e.path) {
                Ok(v) => {
                    assert!(v.map.info.as_ref().map(|i| i.starts.len()).unwrap_or(0) as u8 >= e.players.min(2) || e.players >= 2);
                    ok += 1;
                }
                Err(m) => err.push(format!("{}: {m}", e.path.display())),
            }
        }
        assert!(err.is_empty(), "{ok} ok, {} failed: {}", err.len(), err.join("; "));
    }

    #[test]
    fn mix_pack_files_are_not_listed_as_loose_maps() {
        let t = std::env::temp_dir().join(format!("ra2maps-mix-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        std::fs::create_dir_all(&t).unwrap();
        // New-format MIX header (flags=0): must not be parsed as INI.
        std::fs::write(t.join("Arena.yro"), [0u8, 0, 3, 0, 1, 2, 3, 4]).unwrap();
        std::fs::write(t.join("plain.map"), sample(false)).unwrap();
        let l = list(&[t.clone()], &t.join("c.json"));
        assert_eq!(l.len(), 1);
        assert!(l[0].path.ends_with("plain.map"));
        let _ = std::fs::remove_dir_all(t);
    }
}
