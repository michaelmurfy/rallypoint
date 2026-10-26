//! Showing a map the way the game does: its preview picture (the `.tga` beside the `.map`, which the game's lobby
//! scales up) with the start positions marked. The start positions are the `Player_N_Start` waypoints inside the
//! `.map`, which is a chunked file, usually compressed with EA's RefPack (`EAR\0` + size + stream).
use std::collections::HashMap;
use std::path::Path;

/// A map's start positions are given as fractions of the playable area (0,0 is the bottom left, as in the game).
#[derive(Debug, Clone, PartialEq, serde::Serialize)]
pub struct Start {
    pub number: u8,
    pub x: f32,
    pub y: f32,
}

#[derive(Debug, Clone, PartialEq, serde::Serialize)]
pub struct MapInfo {
    /// The playable area in world units (ten per cell), without the unplayable border.
    pub width: f32,
    pub height: f32,
    pub starts: Vec<Start>,
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct MapView {
    pub name: String,
    /// The preview picture, top row first, as base64 of RGBA bytes. Empty when the map has none.
    pub image_w: u32,
    pub image_h: u32,
    pub rgba: String,
    pub info: Option<MapInfo>,
    pub size_kb: u64,
}

/// The largest map data accepted once unpacked. Real maps are a few MB.
const MAX_UNPACKED: usize = 128 << 20;

/// Unpack EA RefPack data. Never produces more than the size it declares (and never more than `MAX_UNPACKED`).
pub fn refpack(src: &[u8]) -> Result<Vec<u8>, String> {
    let bad = || "the map data is damaged".to_string();
    let flags = *src.first().ok_or_else(bad)?;
    if src.get(1) != Some(&0xFB) {
        return Err(bad());
    }
    let big = flags & 0x80 != 0;
    let field = if big { 4 } else { 3 };
    let mut pos = 2;
    if flags & 0x01 != 0 {
        pos += field; // a stored packed size, which is not needed
    }
    let size_bytes = src.get(pos..pos + field).ok_or_else(bad)?;
    let size = size_bytes.iter().fold(0usize, |a, b| (a << 8) | *b as usize);
    pos += field;
    if size > MAX_UNPACKED {
        return Err("the map is implausibly large".into());
    }
    let mut out: Vec<u8> = Vec::with_capacity(size);
    let byte = |p: usize| src.get(p).copied().ok_or_else(bad);
    while pos < src.len() && out.len() < size {
        let b0 = byte(pos)?;
        pos += 1;
        let (lit, reflen, refoff);
        if b0 < 0x80 {
            let b1 = byte(pos)? as usize;
            pos += 1;
            lit = (b0 & 3) as usize;
            reflen = (((b0 & 0x1C) >> 2) + 3) as usize;
            refoff = (((b0 & 0x60) as usize) << 3) + b1 + 1;
        } else if b0 < 0xC0 {
            let (b1, b2) = (byte(pos)? as usize, byte(pos + 1)? as usize);
            pos += 2;
            lit = (b1 >> 6) & 3;
            reflen = ((b0 & 0x3F) + 4) as usize;
            refoff = ((b1 & 0x3F) << 8) + b2 + 1;
        } else if b0 < 0xE0 {
            let (b1, b2, b3) = (byte(pos)? as usize, byte(pos + 1)? as usize, byte(pos + 2)? as usize);
            pos += 3;
            lit = (b0 & 3) as usize;
            reflen = (((b0 & 0x0C) as usize) << 6) + b3 + 5;
            refoff = (((b0 & 0x10) as usize) << 12) + (b1 << 8) + b2 + 1;
        } else if b0 < 0xFC {
            lit = (((b0 & 0x1F) as usize) << 2) + 4;
            reflen = 0;
            refoff = 0;
        } else {
            lit = (b0 & 3) as usize;
            reflen = 0;
            refoff = 0;
        }
        out.extend_from_slice(src.get(pos..pos + lit).ok_or_else(bad)?);
        pos += lit;
        if reflen > 0 {
            if refoff > out.len() {
                return Err(bad());
            }
            for _ in 0..reflen {
                let b = out[out.len() - refoff];
                out.push(b);
            }
        }
        if b0 >= 0xFC {
            break;
        }
    }
    out.truncate(size);
    if out.len() != size {
        return Err(bad());
    }
    Ok(out)
}

/// The map file's bytes as chunk data (`CkMp...`), unpacked if needed.
fn chunk_data(file: &[u8]) -> Result<Vec<u8>, String> {
    match file.get(..4) {
        Some(b"EAR\0") => refpack(file.get(8..).ok_or("the map data is damaged")?),
        // older maps: zlib, behind the same four-byte tag and size
        Some(b"ZL5\0") => {
            use std::io::Read;
            let mut out = Vec::new();
            flate2::read::ZlibDecoder::new(file.get(8..).ok_or("the map data is damaged")?).take(MAX_UNPACKED as u64 + 1).read_to_end(&mut out).map_err(|_| "the map data is damaged".to_string())?;
            if out.len() > MAX_UNPACKED {
                return Err("the map is implausibly large".into());
            }
            Ok(out)
        }
        Some(b"CkMp") => Ok(file.to_vec()),
        _ => Err("this isn't a map file".into()),
    }
}

struct Reader<'a> {
    d: &'a [u8],
    p: usize,
}

impl<'a> Reader<'a> {
    fn take(&mut self, n: usize) -> Option<&'a [u8]> {
        let s = self.d.get(self.p..self.p.checked_add(n)?)?;
        self.p += n;
        Some(s)
    }
    fn u8(&mut self) -> Option<u8> {
        self.take(1).map(|b| b[0])
    }
    fn u16(&mut self) -> Option<u16> {
        self.take(2).map(|b| u16::from_le_bytes([b[0], b[1]]))
    }
    fn u32(&mut self) -> Option<u32> {
        self.take(4).map(|b| u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
    }
    fn i32(&mut self) -> Option<i32> {
        self.u32().map(|v| v as i32)
    }
    fn f32(&mut self) -> Option<f32> {
        self.u32().map(f32::from_bits)
    }
}

/// `(name, version, data)` for each chunk laid end to end in `d`.
fn chunks<'a>(d: &'a [u8], names: &HashMap<u32, String>) -> Vec<(String, u16, &'a [u8])> {
    let mut r = Reader { d, p: 0 };
    let mut out = vec![];
    while let (Some(id), Some(ver), Some(size)) = (r.u32(), r.u16(), r.u32()) {
        let Some(data) = r.take(size as usize) else { break };
        out.push((names.get(&id).cloned().unwrap_or_default(), ver, data));
    }
    out
}

/// The playable size and the start positions of a map.
pub fn parse_map(file: &[u8]) -> Result<MapInfo, String> {
    let data = chunk_data(file)?;
    let damaged = || "the map data is damaged".to_string();
    let mut r = Reader { d: &data, p: 4 };
    let count = r.u32().ok_or_else(damaged)?;
    let mut names = HashMap::new();
    for _ in 0..count.min(4096) {
        let len = r.u8().ok_or_else(damaged)? as usize;
        let name = String::from_utf8_lossy(r.take(len).ok_or_else(damaged)?).into_owned();
        names.insert(r.u32().ok_or_else(damaged)?, name);
    }
    let top = chunks(&data[r.p..], &names);

    let (_, ver, hm) = top.iter().find(|c| c.0 == "HeightMapData").ok_or("the map has no terrain")?;
    let mut h = Reader { d: hm, p: 0 };
    let (w, ht) = (h.i32().ok_or_else(damaged)?, h.i32().ok_or_else(damaged)?);
    let border = if *ver >= 3 { h.i32().ok_or_else(damaged)? } else { 0 };
    let (play_w, play_h) = (((w - 2 * border).max(1) * 10) as f32, ((ht - 2 * border).max(1) * 10) as f32);

    let mut starts = vec![];
    if let Some((_, _, objects)) = top.iter().find(|c| c.0 == "ObjectsList") {
        for (name, _, o) in chunks(objects, &names) {
            if name != "Object" {
                continue;
            }
            if let Some(s) = start_of(o, &names, play_w, play_h) {
                starts.push(s);
            }
        }
    }
    starts.sort_by_key(|s| s.number);
    starts.dedup_by_key(|s| s.number);
    Ok(MapInfo { width: play_w, height: play_h, starts })
}

/// `Player_N_Start` if this Object chunk is that waypoint.
fn start_of(o: &[u8], names: &HashMap<u32, String>, play_w: f32, play_h: f32) -> Option<Start> {
    let mut r = Reader { d: o, p: 0 };
    let (x, y, _z, _angle, _flags) = (r.f32()?, r.f32()?, r.f32()?, r.f32()?, r.i32()?);
    let name_len = r.u16()? as usize;
    r.take(name_len)?;
    let props = r.u16()?;
    for _ in 0..props {
        let kt = r.u32()?;
        let key = names.get(&(kt >> 8)).map(String::as_str).unwrap_or("");
        match kt & 0xFF {
            0 => {
                r.u8()?;
            }
            1 | 2 => {
                r.u32()?;
            }
            3 => {
                let len = r.u16()? as usize;
                let value = r.take(len)?;
                if key == "waypointName" {
                    let v = String::from_utf8_lossy(value);
                    let n: u8 = v.strip_prefix("Player_")?.strip_suffix("_Start")?.parse().ok()?;
                    return Some(Start { number: n, x: (x / play_w).clamp(0.0, 1.0), y: (y / play_h).clamp(0.0, 1.0) });
                }
            }
            4 => {
                let len = r.u16()? as usize;
                r.take(len * 2)?;
            }
            _ => return None,
        }
    }
    None
}

#[derive(Debug, PartialEq)]
pub struct Image {
    pub w: u32,
    pub h: u32,
    /// RGBA, top row first.
    pub rgba: Vec<u8>,
}

/// Decode a Targa picture: uncompressed or run-length truecolor, 24 or 32 bits, either vertical order.
pub fn decode_tga(d: &[u8]) -> Result<Image, String> {
    let bad = || "the preview picture isn't a readable image".to_string();
    if d.len() < 18 {
        return Err(bad());
    }
    let (id_len, cmap_type, kind) = (d[0] as usize, d[1], d[2]);
    let (w, h, bits, desc) = (u16::from_le_bytes([d[12], d[13]]) as usize, u16::from_le_bytes([d[14], d[15]]) as usize, d[16], d[17]);
    if cmap_type != 0 || !(kind == 2 || kind == 10) || !(bits == 24 || bits == 32) || w == 0 || h == 0 || w > 4096 || h > 4096 {
        return Err(bad());
    }
    let bpp = (bits / 8) as usize;
    let mut p = 18 + id_len;
    let mut px: Vec<u8> = Vec::with_capacity(w * h * 4);
    let push = |px: &mut Vec<u8>, c: &[u8]| px.extend_from_slice(&[c[2], c[1], c[0], if bpp == 4 { c[3] } else { 255 }]);
    if kind == 2 {
        for _ in 0..w * h {
            push(&mut px, d.get(p..p + bpp).ok_or_else(bad)?);
            p += bpp;
        }
    } else {
        while px.len() < w * h * 4 {
            let head = *d.get(p).ok_or_else(bad)?;
            p += 1;
            let n = (head & 0x7F) as usize + 1;
            if head & 0x80 != 0 {
                let c = d.get(p..p + bpp).ok_or_else(bad)?;
                p += bpp;
                for _ in 0..n {
                    push(&mut px, c);
                }
            } else {
                for _ in 0..n {
                    push(&mut px, d.get(p..p + bpp).ok_or_else(bad)?);
                    p += bpp;
                }
            }
        }
        px.truncate(w * h * 4);
    }
    // bit 5 of the descriptor set: rows already run top to bottom; clear: bottom to top
    if desc & 0x20 == 0 {
        let row = w * 4;
        let mut flipped = Vec::with_capacity(px.len());
        for r in (0..h).rev() {
            flipped.extend_from_slice(&px[r * row..(r + 1) * row]);
        }
        px = flipped;
    }
    Ok(Image { w: w as u32, h: h as u32, rgba: px })
}

pub fn base64(bytes: &[u8]) -> String {
    const T: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::with_capacity(bytes.len().div_ceil(3) * 4);
    for c in bytes.chunks(3) {
        let n = (c[0] as u32) << 16 | (*c.get(1).unwrap_or(&0) as u32) << 8 | *c.get(2).unwrap_or(&0) as u32;
        out.push(T[(n >> 18) as usize & 63] as char);
        out.push(T[(n >> 12) as usize & 63] as char);
        out.push(if c.len() > 1 { T[(n >> 6) as usize & 63] as char } else { '=' });
        out.push(if c.len() > 2 { T[n as usize & 63] as char } else { '=' });
    }
    out
}

fn dir_size_kb(dir: &Path) -> u64 {
    std::fs::read_dir(dir).into_iter().flatten().flatten().filter_map(|e| e.metadata().ok()).filter(|m| m.is_file()).map(|m| m.len()).sum::<u64>() / 1024
}

/// Everything needed to show the map in `dir` (a map folder): its preview picture and its start positions.
pub fn view(name: &str, dir: &Path) -> Result<MapView, String> {
    let files: Vec<_> = std::fs::read_dir(dir).map_err(|e| e.to_string())?.flatten().map(|e| e.path()).collect();
    let ext = |p: &Path, e: &str| p.extension().is_some_and(|x| x.eq_ignore_ascii_case(e));
    let pick = |e: &str| files.iter().find(|p| ext(p, e) && p.file_stem().is_some_and(|s| s.eq_ignore_ascii_case(name))).or_else(|| files.iter().find(|p| ext(p, e)));
    let info = pick("map").and_then(|p| std::fs::metadata(p).ok().filter(|m| m.len() < (64 << 20)).and_then(|_| std::fs::read(p).ok())).and_then(|b| parse_map(&b).ok());
    let image = pick("tga").and_then(|p| std::fs::metadata(p).ok().filter(|m| m.len() < (16 << 20)).and_then(|_| std::fs::read(p).ok())).and_then(|b| decode_tga(&b).ok());
    let (image_w, image_h, rgba) = image.map(|i| (i.w, i.h, base64(&i.rgba))).unwrap_or((0, 0, String::new()));
    Ok(MapView { name: name.to_string(), image_w, image_h, rgba, info, size_kb: dir_size_kb(dir) })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unpacks_refpack_literals_and_back_references() {
        // header 10 FB + size 12; E0 = four literals "abcd"; 14 03 = copy 8 bytes from 4 back; FC = the end
        let mut src = vec![0x10, 0xFB, 0, 0, 12, 0xE0];
        src.extend_from_slice(b"abcd");
        src.extend_from_slice(&[0x14, 0x03, 0xFC]);
        assert_eq!(refpack(&src).unwrap(), b"abcdabcdabcd");
        // a reference before the start, a short stream and a huge declared size are all refused
        assert!(refpack(&[0x10, 0xFB, 0, 0, 4, 0x00, 0x05, 0xFC]).is_err());
        assert!(refpack(&[0x10, 0xFB, 0, 0, 12, 0xE0, b'a']).is_err());
        assert!(refpack(&[0x10, 0xFB, 0x7F, 0xFF, 0xFF]).is_err());
        assert!(refpack(b"nope").is_err());
    }

    /// A tiny map file: one terrain chunk (10 x 8 cells, border 1) and an object list with two start waypoints.
    fn tiny_map() -> Vec<u8> {
        let names = ["HeightMapData", "ObjectsList", "Object", "waypointName"];
        let mut m = b"CkMp".to_vec();
        m.extend_from_slice(&(names.len() as u32).to_le_bytes());
        for (i, n) in names.iter().enumerate() {
            m.push(n.len() as u8);
            m.extend_from_slice(n.as_bytes());
            m.extend_from_slice(&(i as u32 + 1).to_le_bytes());
        }
        let chunk = |id: u32, ver: u16, data: &[u8]| {
            let mut c = id.to_le_bytes().to_vec();
            c.extend_from_slice(&ver.to_le_bytes());
            c.extend_from_slice(&(data.len() as u32).to_le_bytes());
            c.extend_from_slice(data);
            c
        };
        let mut hm = vec![];
        for v in [10i32, 8, 1] {
            hm.extend_from_slice(&v.to_le_bytes());
        }
        m.extend(chunk(1, 4, &hm));
        let object = |x: f32, y: f32, wp: &str| {
            let mut o = vec![];
            for v in [x, y, 0.0, 0.0] {
                o.extend_from_slice(&v.to_le_bytes());
            }
            o.extend_from_slice(&0i32.to_le_bytes());
            o.extend_from_slice(&0u16.to_le_bytes()); // empty name
            o.extend_from_slice(&1u16.to_le_bytes()); // one property
            o.extend_from_slice(&((4u32 << 8) | 3).to_le_bytes()); // waypointName, a string
            o.extend_from_slice(&(wp.len() as u16).to_le_bytes());
            o.extend_from_slice(wp.as_bytes());
            chunk(3, 3, &o)
        };
        let mut objects = object(60.0, 30.0, "Player_2_Start");
        objects.extend(object(10.0, 15.0, "Player_1_Start"));
        objects.extend(object(5.0, 5.0, "Some Other Waypoint"));
        m.extend(chunk(2, 3, &objects));
        m
    }

    #[test]
    fn reads_the_playable_size_and_start_positions_of_a_map() {
        let info = parse_map(&tiny_map()).unwrap();
        // (10 - 2) x (8 - 2) cells of ten units
        assert_eq!((info.width, info.height), (80.0, 60.0));
        assert_eq!(info.starts, [Start { number: 1, x: 0.125, y: 0.25 }, Start { number: 2, x: 0.75, y: 0.5 }]);
        assert!(parse_map(b"EAR\0\0\0\0\0junk").is_err() && parse_map(b"nothing here").is_err());
        assert!(parse_map(&tiny_map()[..40]).is_err(), "a cut-off file is refused, not read past");
    }

    #[test]
    fn reads_a_zlib_packed_map_too() {
        use std::io::Write;
        let mut z = flate2::write::ZlibEncoder::new(Vec::new(), flate2::Compression::default());
        z.write_all(&tiny_map()).unwrap();
        let mut file = b"ZL5\0".to_vec();
        file.extend_from_slice(&(tiny_map().len() as u32).to_le_bytes());
        file.extend_from_slice(&z.finish().unwrap());
        assert_eq!(parse_map(&file).unwrap().starts.len(), 2);
    }

    #[test]
    fn decodes_both_picture_orders_and_run_length_data() {
        // 2 x 2, 32-bit, rows stored bottom first: bottom row red, green; top row blue, white (BGRA bytes)
        let mut tga = vec![0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 32, 0];
        tga.extend_from_slice(&[0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 255, 255, 255, 255]);
        let img = decode_tga(&tga).unwrap();
        assert_eq!((img.w, img.h), (2, 2));
        assert_eq!(img.rgba[..4], [0, 0, 255, 255], "the top row comes first: blue");
        assert_eq!(img.rgba[8..12], [255, 0, 0, 255], "then the bottom row: red");
        // the same picture stored top-down (bit 5) needs no flip
        let mut top = tga.clone();
        top[17] = 0x20;
        assert_eq!(decode_tga(&top).unwrap().rgba[..4], [255, 0, 0, 255]);
        // run-length: one packet repeating a 24-bit grey pixel four times
        let rle = [0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 24, 0x20, 0x83, 10, 20, 30];
        let img = decode_tga(&rle).unwrap();
        assert_eq!(img.rgba[12..16], [30, 20, 10, 255]);
        assert!(decode_tga(&tga[..30]).is_err() && decode_tga(b"short").is_err());
    }

    #[test]
    fn base64_matches_the_standard_examples() {
        assert_eq!(base64(b""), "");
        assert_eq!(base64(b"f"), "Zg==");
        assert_eq!(base64(b"fo"), "Zm8=");
        assert_eq!(base64(b"foo"), "Zm9v");
        assert_eq!(base64(b"foobar"), "Zm9vYmFy");
    }

    /// A real map from the player's own folder, when there is one: the reader copes with the game's actual files.
    #[test]
    fn reads_a_real_map_if_present() {
        let Some(home) = std::env::var_os("HOME") else { return };
        let dir = std::path::PathBuf::from(home).join("Documents/Command and Conquer Generals Zero Hour Data/Maps/Unity A ZH v2");
        let Ok(v) = view("Unity A ZH v2", &dir) else { return };
        let info = v.info.expect("the map is read");
        assert_eq!(info.starts.len(), 4);
        assert_eq!((v.image_w, v.image_h), (128, 128));
    }
}
