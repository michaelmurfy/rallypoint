//! The country flags Red Alert 2 and Yuri's Revenge draw beside a player: 47x23 PCX pictures that pair each
//! country's flag with its side's emblem (the Allied eagle, the Soviet star, the Yuri psi symbol), on a magenta
//! colour key. They live in the encrypted MIX archives `ra2.mix/local.mix` (the nine RA2 countries) and
//! `ra2md.mix/localmd.mix` (Yuri). None of EA's art is shipped: it is read from the player's own files here and
//! handed to the page as PNG data URLs.
//!
//! Reading a MIX means the Westwood header (flags dword, 80-byte key source, Blowfish-encrypted count/index) and
//! the key derivation published for the format (an RSA public operation on each 40-byte half of the key source).
use crate::ra2::resolve_file;
use crate::ra2flags_blowfish::{P_INIT, S_INIT};
use flate2::write::ZlibEncoder;
use flate2::Compression;
use serde::Serialize;
use std::cmp::Ordering;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::Path;

/// One country's flag picture, ready for an `<img src>`.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct Ra2Flag {
    /// Index into the engine's country list: 0 America, .. 8 Russia, 9 Yuri (`ra2launch::SIDES`).
    pub side: i32,
    /// `data:image/png;base64,...`, the game's own picture with the colour key made transparent.
    pub png: String,
}

/// Country picture, archive and the archive's inner MIX, in `ra2launch::SIDES` order. Yuri is a Yuri's
/// Revenge country, so its picture is only in the expansion archives.
const FLAGS: [(&str, &str, &str); 10] = [
    ("usai.pcx", "ra2.mix", "local.mix"),
    ("japi.pcx", "ra2.mix", "local.mix"),
    ("frai.pcx", "ra2.mix", "local.mix"),
    ("geri.pcx", "ra2.mix", "local.mix"),
    ("gbri.pcx", "ra2.mix", "local.mix"),
    ("djbi.pcx", "ra2.mix", "local.mix"),
    ("arbi.pcx", "ra2.mix", "local.mix"),
    ("lati.pcx", "ra2.mix", "local.mix"),
    ("rusi.pcx", "ra2.mix", "local.mix"),
    ("yrii.pcx", "ra2md.mix", "localmd.mix"),
];

/// Every country flag that could be read, in country order. Pictures that are missing are simply left out (the
/// page falls back to its drawn placeholders); an error only when nothing at all could be read.
pub fn country_flags(game_dir: &Path) -> Result<Vec<Ra2Flag>, String> {
    let mut archives: Vec<(String, Option<MixFile>)> = Vec::new();
    let mut out = Vec::new();
    let mut last_err = String::new();
    for (side, (file, archive, inner)) in FLAGS.iter().enumerate() {
        let key = format!("{archive}/{inner}");
        if !archives.iter().any(|(k, _)| *k == key) {
            let mix = match nested_archive(game_dir, archive, inner) {
                Ok(m) => Some(m),
                Err(e) => {
                    last_err = e;
                    None
                }
            };
            archives.push((key.clone(), mix));
        }
        let Some(slot) = archives.iter_mut().find(|(k, _)| *k == key).map(|(_, m)| m) else {
            continue;
        };
        let Some(mix) = slot.as_mut() else {
            continue;
        };
        let bytes = match mix.read(file) {
            Ok(b) => b,
            Err(e) => {
                last_err = e;
                continue;
            }
        };
        match flag_png(&bytes) {
            Ok(png) => out.push(Ra2Flag { side: side as i32, png: format!("data:image/png;base64,{}", crate::mapview::base64(&png)) }),
            Err(e) => last_err = e,
        }
    }
    if out.is_empty() {
        return Err(if last_err.is_empty() { "The game's country flags could not be read".into() } else { last_err });
    }
    Ok(out)
}

fn nested_archive(game_dir: &Path, archive: &str, inner: &str) -> Result<MixFile, String> {
    let path = resolve_file(game_dir, archive).ok_or_else(|| format!("{archive} is not in {}", game_dir.display()))?;
    MixFile::open(&path)?.open_nested(inner)
}

/// Decode a flag PCX and re-encode it as an RGBA PNG with the magenta colour key transparent.
fn flag_png(data: &[u8]) -> Result<Vec<u8>, String> {
    let pcx = decode_pcx(data)?;
    let mut rgba = Vec::with_capacity(pcx.indices.len() * 4);
    for &i in &pcx.indices {
        let [r, g, b] = pcx.palette[i as usize];
        rgba.extend_from_slice(&[r, g, b, if (r, g, b) == (255, 0, 255) { 0 } else { 255 }]);
    }
    Ok(encode_png(pcx.w, pcx.h, &rgba))
}

// ---------------------------------------------------------------------------------------------
// MIX archives
// ---------------------------------------------------------------------------------------------

struct Entry {
    id: u32,
    offset: u32,
    size: u32,
}

/// A MIX archive, read from a file without loading the whole thing (RA2's is 281 MB). Nested archives share the
/// same file through `try_clone`; every read seeks first, so the shared cursor does not matter.
struct MixFile {
    file: File,
    base: u64,
    body: u64,
    entries: Vec<Entry>,
}

impl MixFile {
    fn open(path: &Path) -> Result<Self, String> {
        let file = File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
        let size = file.metadata().map_err(|e| format!("{}: {e}", path.display()))?.len();
        Self::parse(file, 0, size)
    }

    fn parse(mut file: File, base: u64, size: u64) -> Result<Self, String> {
        if size < 4 {
            return Err("mix: too small".into());
        }
        let flags = u32::from_le_bytes(read_array(&mut file, base)?);
        let (body, entries) = if flags & 0xffff == 0 {
            // RA2/YR layout: flags, then either plain count/body/index or the encrypted form.
            let mut pos = 4u64;
            if flags & 0x0002_0000 != 0 {
                let ks = read_array::<80>(&mut file, base + pos)?;
                pos += 80;
                let bf = Blowfish::new(&derive_key(&ks));
                // The first block carries the count and body size; the whole header+index follows, encrypted
                // in 8-byte blocks and zero-padded to the last one.
                let mut first = read_array::<8>(&mut file, base + pos)?;
                bf.decrypt_block(&mut first);
                let count = u16::from_le_bytes([first[0], first[1]]) as usize;
                let blocks = ((6 + count * 12) + 7) / 8;
                if pos + (blocks as u64) * 8 > size {
                    return Err("mix: encrypted index is longer than the file".into());
                }
                let mut enc = vec![0u8; blocks * 8];
                read_exact_at(&mut file, base + pos, &mut enc)?;
                for chunk in enc.chunks_exact_mut(8) {
                    let block: &mut [u8; 8] = chunk.try_into().unwrap();
                    bf.decrypt_block(block);
                }
                (pos + (blocks as u64) * 8, header_entries(&enc)?)
            } else {
                (pos + 6 + read_count(&mut file, base + pos)? as u64 * 12, plain_entries(&mut file, base + pos)?)
            }
        } else {
            // Older TD/RA1 archive: starts straight at the count (never used by RA2/YR, kept for completeness).
            (6 + read_count(&mut file, base)? as u64 * 12, plain_entries(&mut file, base)?)
        };
        for e in &entries {
            if body + e.offset as u64 + e.size as u64 > size {
                return Err("mix: entry outside the archive".into());
            }
        }
        Ok(MixFile { file, base, body, entries })
    }

    fn id(&self, name: &str) -> Option<(u32, u32)> {
        let id = mix_id(name);
        self.entries.iter().find(|e| e.id == id).map(|e| (e.offset, e.size))
    }

    fn read(&mut self, name: &str) -> Result<Vec<u8>, String> {
        let (offset, size) = self.id(name).ok_or_else(|| format!("mix: {name} is not in the archive"))?;
        let mut out = vec![0u8; size as usize];
        read_exact_at(&mut self.file, self.base + self.body + offset as u64, &mut out)?;
        Ok(out)
    }

    fn open_nested(&self, name: &str) -> Result<MixFile, String> {
        let (offset, size) = self.id(name).ok_or_else(|| format!("mix: {name} is not in the archive"))?;
        let file = self.file.try_clone().map_err(|e| e.to_string())?;
        MixFile::parse(file, self.base + self.body + offset as u64, size as u64)
    }
}

fn read_count(file: &mut File, pos: u64) -> Result<usize, String> {
    let h = read_array::<6>(file, pos)?;
    Ok(u16::from_le_bytes([h[0], h[1]]) as usize)
}

fn plain_entries(file: &mut File, pos: u64) -> Result<Vec<Entry>, String> {
    let count = read_count(file, pos)?;
    let mut idx = vec![0u8; count * 12];
    read_exact_at(file, pos + 6, &mut idx)?;
    Ok(entries_from(&idx, count))
}

fn header_entries(dec: &[u8]) -> Result<Vec<Entry>, String> {
    if dec.len() < 6 {
        return Err("mix: truncated header".into());
    }
    let count = u16::from_le_bytes([dec[0], dec[1]]) as usize;
    if dec.len() < 6 + count * 12 {
        return Err("mix: truncated index".into());
    }
    Ok(entries_from(&dec[6..], count))
}

fn entries_from(idx: &[u8], count: usize) -> Vec<Entry> {
    (0..count)
        .map(|i| {
            let p = i * 12;
            Entry {
                id: u32::from_le_bytes(idx[p..p + 4].try_into().unwrap()),
                offset: u32::from_le_bytes(idx[p + 4..p + 8].try_into().unwrap()),
                size: u32::from_le_bytes(idx[p + 8..p + 12].try_into().unwrap()),
            }
        })
        .collect()
}

fn read_array<const N: usize>(file: &mut File, pos: u64) -> Result<[u8; N], String> {
    let mut b = [0u8; N];
    read_exact_at(file, pos, &mut b)?;
    Ok(b)
}

fn read_exact_at(file: &mut File, pos: u64, buf: &mut [u8]) -> Result<(), String> {
    file.seek(SeekFrom::Start(pos)).map_err(|e| e.to_string())?;
    file.read_exact(buf).map_err(|e| e.to_string())
}

/// Westwood's MIX name hash: upper-case, pad to a multiple of four with the length's remainder followed by the
/// first character of the final block, then a standard CRC-32 (`0xEDB88320`, reflected).
pub fn mix_id(name: &str) -> u32 {
    let mut s: Vec<u8> = name.bytes().map(|b| b.to_ascii_uppercase()).collect();
    if s.len() & 3 != 0 {
        let block_start = s.len() & !3;
        s.push((s.len() & 3) as u8);
        while s.len() & 3 != 0 {
            s.push(s[block_start]);
        }
    }
    let mut crc = 0xffff_ffffu32;
    for b in s {
        crc ^= b as u32;
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb8_8320 & 0u32.wrapping_sub(crc & 1));
        }
    }
    !crc
}

// ---------------------------------------------------------------------------------------------
// Westwood key derivation (RSA public operation on the 80-byte key source)
// ---------------------------------------------------------------------------------------------

/// Westwood's published MIX public-key modulus (`AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V`,
/// after its two DER header bytes). A fixed interoperability constant of the format, like an SSL root key.
const MIX_MODULUS: [u8; 40] = [
    0x51, 0xbc, 0xda, 0x08, 0x6d, 0x39, 0xfc, 0xe4, 0x56, 0x51, 0x60, 0xd6, 0x51, 0x71, 0x3f, 0xa2, 0xe8, 0xaa, 0x54, 0xfa,
    0x66, 0x82, 0xb0, 0x4a, 0xab, 0xdd, 0x0e, 0x6a, 0xf8, 0xb0, 0xc1, 0xe6, 0xd1, 0xfb, 0x4f, 0x3d, 0xaa, 0x43, 0x7f, 0x15,
];

/// The 56-byte Blowfish key a MIX header is encrypted with: each 40-byte half of the key source is read as a
/// little-endian integer, RSA-encrypted with exponent 65537 and the modulus above, and its low 39 bytes kept.
fn derive_key(source: &[u8; 80]) -> [u8; 56] {
    let n = big_from_be(&MIX_MODULUS);
    let mut joined = [0u8; 78];
    for half in 0..2 {
        let m = big_from_le(&source[half * 40..half * 40 + 40]);
        let c = big_mod_pow(&m, 65_537, &n);
        let mut bytes = [0u8; 40];
        big_to_le(&c, &mut bytes);
        joined[half * 39..half * 39 + 39].copy_from_slice(&bytes[..39]);
    }
    joined[..56].try_into().unwrap()
}

/// A small unsigned big integer (32-bit limbs, least significant first), only what RSA with e = 65537 needs.
type Big = Vec<u32>;

fn big_normalize(a: &mut Big) {
    while a.last() == Some(&0) {
        a.pop();
    }
}

fn big_from_le(bytes: &[u8]) -> Big {
    let mut a = vec![0u32; (bytes.len() + 3) / 4];
    for (i, b) in bytes.iter().enumerate() {
        a[i / 4] |= (*b as u32) << (8 * (i % 4));
    }
    big_normalize(&mut a);
    a
}

fn big_from_be(bytes: &[u8]) -> Big {
    let mut a = vec![0u32; (bytes.len() + 3) / 4];
    for (i, b) in bytes.iter().enumerate() {
        let weight = bytes.len() - 1 - i;
        a[weight / 4] |= (*b as u32) << (8 * (weight % 4));
    }
    big_normalize(&mut a);
    a
}

fn big_to_le(a: &Big, out: &mut [u8]) {
    for (i, o) in out.iter_mut().enumerate() {
        let v = *a.get(i / 4).unwrap_or(&0);
        *o = (v >> (8 * (i % 4))) as u8;
    }
}

fn big_cmp(a: &Big, b: &Big) -> Ordering {
    match a.len().cmp(&b.len()) {
        Ordering::Equal => a.iter().rev().cmp(b.iter().rev()),
        o => o,
    }
}

fn big_bit_len(a: &Big) -> usize {
    match a.last() {
        None => 0,
        Some(top) => (a.len() - 1) * 32 + (32 - top.leading_zeros() as usize),
    }
}

fn big_bit(a: &Big, i: usize) -> bool {
    a.get(i / 32).is_some_and(|l| (*l >> (i % 32)) & 1 == 1)
}

fn big_shift_in(a: &mut Big, bit: u32) {
    let mut carry = bit & 1;
    for limb in a.iter_mut() {
        let next = *limb >> 31;
        *limb = (*limb << 1) | carry;
        carry = next;
    }
    if carry != 0 {
        a.push(carry);
    }
}

fn big_sub(a: &mut Big, b: &Big) {
    let mut borrow = 0u64;
    for i in 0..a.len() {
        let rhs = *b.get(i).unwrap_or(&0) as u64 + borrow;
        let cur = (a[i] as u64).wrapping_sub(rhs);
        a[i] = cur as u32;
        borrow = (cur >> 32) & 1;
    }
    big_normalize(a);
}

fn big_mul(a: &Big, b: &Big) -> Big {
    if a.is_empty() || b.is_empty() {
        return Big::new();
    }
    let mut r = vec![0u32; a.len() + b.len()];
    for (i, &ai) in a.iter().enumerate() {
        let mut carry = 0u64;
        for (j, &bj) in b.iter().enumerate() {
            let cur = r[i + j] as u64 + ai as u64 * bj as u64 + carry;
            r[i + j] = cur as u32;
            carry = cur >> 32;
        }
        r[i + b.len()] = carry as u32;
    }
    big_normalize(&mut r);
    r
}

fn big_mod(x: &Big, m: &Big) -> Big {
    let mut r = Big::new();
    for i in (0..big_bit_len(x)).rev() {
        big_shift_in(&mut r, big_bit(x, i) as u32);
        if big_cmp(&r, m) != Ordering::Less {
            big_sub(&mut r, m);
        }
    }
    r
}

fn big_mod_pow(base: &Big, exp: u32, m: &Big) -> Big {
    let mut result: Big = vec![1];
    let mut b = big_mod(base, m);
    let mut e = exp;
    while e != 0 {
        if e & 1 != 0 {
            result = big_mod(&big_mul(&result, &b), m);
        }
        e >>= 1;
        if e != 0 {
            b = big_mod(&big_mul(&b, &b), m);
        }
    }
    result
}

// ---------------------------------------------------------------------------------------------
// Blowfish (decryption only; the key schedule needs the cipher's round function)
// ---------------------------------------------------------------------------------------------

struct Blowfish {
    p: [u32; 18],
    s: [[u32; 256]; 4],
}

impl Blowfish {
    fn new(key: &[u8]) -> Self {
        let mut bf = Blowfish { p: P_INIT, s: S_INIT };
        let mut k = 0usize;
        for i in 0..18 {
            let mut d = 0u32;
            for _ in 0..4 {
                d = (d << 8) | key[k] as u32;
                k = (k + 1) % key.len();
            }
            bf.p[i] ^= d;
        }
        let (mut l, mut r) = (0u32, 0u32);
        for i in (0..18).step_by(2) {
            let (nl, nr) = bf.encrypt(l, r);
            l = nl;
            r = nr;
            bf.p[i] = l;
            bf.p[i + 1] = r;
        }
        for i in 0..4 {
            for j in (0..256).step_by(2) {
                let (nl, nr) = bf.encrypt(l, r);
                l = nl;
                r = nr;
                bf.s[i][j] = l;
                bf.s[i][j + 1] = r;
            }
        }
        bf
    }

    fn f(&self, x: u32) -> u32 {
        let a = self.s[0][(x >> 24) as usize & 0xff];
        let b = self.s[1][(x >> 16) as usize & 0xff];
        let c = self.s[2][(x >> 8) as usize & 0xff];
        let d = self.s[3][x as usize & 0xff];
        (a.wrapping_add(b) ^ c).wrapping_add(d)
    }

    fn encrypt(&self, mut l: u32, mut r: u32) -> (u32, u32) {
        for i in (0..16).step_by(2) {
            l ^= self.p[i];
            r ^= self.f(l);
            r ^= self.p[i + 1];
            l ^= self.f(r);
        }
        (r ^ self.p[17], l ^ self.p[16])
    }

    fn decrypt_block(&self, block: &mut [u8; 8]) {
        let mut l = u32::from_be_bytes(block[0..4].try_into().unwrap());
        let mut r = u32::from_be_bytes(block[4..8].try_into().unwrap());
        for i in (2..=16).rev().step_by(2) {
            l ^= self.p[i + 1];
            r ^= self.f(l);
            r ^= self.p[i];
            l ^= self.f(r);
        }
        let (out_l, out_r) = (r ^ self.p[0], l ^ self.p[1]);
        block[0..4].copy_from_slice(&out_l.to_be_bytes());
        block[4..8].copy_from_slice(&out_r.to_be_bytes());
    }
}

// ---------------------------------------------------------------------------------------------
// PCX and PNG
// ---------------------------------------------------------------------------------------------

struct Pcx {
    w: u32,
    h: u32,
    indices: Vec<u8>,
    palette: [[u8; 3]; 256],
}

fn decode_pcx(d: &[u8]) -> Result<Pcx, String> {
    if d.len() < 128 || d[0] != 0x0a {
        return Err("pcx: not a PCX picture".into());
    }
    if d[2] != 1 || d[3] != 8 || d[65] != 1 {
        return Err("pcx: only 8-bit single-plane RLE is supported".into());
    }
    let xmin = u16::from_le_bytes([d[4], d[5]]) as u32;
    let ymin = u16::from_le_bytes([d[6], d[7]]) as u32;
    let xmax = u16::from_le_bytes([d[8], d[9]]) as u32;
    let ymax = u16::from_le_bytes([d[10], d[11]]) as u32;
    let w = xmax.saturating_sub(xmin) + 1;
    let h = ymax.saturating_sub(ymin) + 1;
    let bpl = u16::from_le_bytes([d[66], d[67]]) as usize;
    if w == 0 || h == 0 || w > 4096 || h > 4096 {
        return Err("pcx: bad dimensions".into());
    }
    let mut indices = vec![0u8; w as usize * h as usize];
    let mut pos = 128usize;
    for row in 0..h as usize {
        let mut x = 0usize;
        while x < bpl {
            if pos >= d.len() {
                return Err("pcx: truncated picture".into());
            }
            let mut byte = d[pos];
            pos += 1;
            let count = if byte & 0xc0 == 0xc0 {
                let c = (byte & 0x3f) as usize;
                if pos >= d.len() {
                    return Err("pcx: truncated picture".into());
                }
                byte = d[pos];
                pos += 1;
                c
            } else {
                1
            };
            for _ in 0..count {
                if x < bpl {
                    if x < w as usize {
                        indices[row * w as usize + x] = byte;
                    }
                    x += 1;
                }
            }
        }
    }
    if d.len() < 769 {
        return Err("pcx: no palette".into());
    }
    let pal = &d[d.len() - 769..];
    if pal[0] != 0x0c {
        return Err("pcx: no palette".into());
    }
    let mut palette = [[0u8; 3]; 256];
    for (i, slot) in palette.iter_mut().enumerate() {
        slot.copy_from_slice(&[pal[1 + i * 3], pal[2 + i * 3], pal[3 + i * 3]]);
    }
    Ok(Pcx { w, h, indices, palette })
}

fn encode_png(w: u32, h: u32, rgba: &[u8]) -> Vec<u8> {
    let mut raw = Vec::with_capacity((w as usize * 4 + 1) * h as usize);
    for row in rgba.chunks(w as usize * 4) {
        raw.push(0); // filter: none
        raw.extend_from_slice(row);
    }
    let mut z = ZlibEncoder::new(Vec::new(), Compression::default());
    z.write_all(&raw).expect("in-memory zlib write");
    let idat = z.finish().expect("in-memory zlib finish");
    let mut png = b"\x89PNG\r\n\x1a\n".to_vec();
    let mut ihdr = [0u8; 13];
    ihdr[0..4].copy_from_slice(&w.to_be_bytes());
    ihdr[4..8].copy_from_slice(&h.to_be_bytes());
    ihdr[8] = 8; // bit depth
    ihdr[9] = 6; // RGBA
    png_chunk(&mut png, b"IHDR", &ihdr);
    png_chunk(&mut png, b"IDAT", &idat);
    png_chunk(&mut png, b"IEND", &[]);
    png
}

fn png_chunk(out: &mut Vec<u8>, kind: &[u8; 4], data: &[u8]) {
    out.extend_from_slice(&(data.len() as u32).to_be_bytes());
    out.extend_from_slice(kind);
    out.extend_from_slice(data);
    out.extend_from_slice(&crc32(&[&kind[..], data]).to_be_bytes());
}

fn crc32(parts: &[&[u8]]) -> u32 {
    let mut c = 0xffff_ffffu32;
    for part in parts {
        for &b in *part {
            c ^= b as u32;
            for _ in 0..8 {
                c = (c >> 1) ^ (0xedb8_8320 & 0u32.wrapping_sub(c & 1));
            }
        }
    }
    !c
}

// ---------------------------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mix_ids_match_the_game_archives() {
        // The ids the shipped archives list for these names (XCC/OpenRA's CRC-32 scheme).
        assert_eq!(mix_id("local.mix"), 0xa8548fd9);
        assert_eq!(mix_id("localmd.mix"), 0xfbe0d09d);
        assert_eq!(mix_id("rulesmd.ini"), 0x8218f9f4);
        assert_eq!(mix_id("USAI.PCX"), mix_id("usai.pcx"));
        assert_ne!(mix_id("usai.pcx"), mix_id("japi.pcx"));
    }

    #[test]
    fn blowfish_matches_the_published_test_vector() {
        // Schneier's Blowfish reference vectors: all-zero key, all-zero plaintext.
        let bf = Blowfish::new(&[0u8; 8]);
        let mut block = [0u8; 8];
        let l = u32::from_be_bytes(block[0..4].try_into().unwrap());
        let r = u32::from_be_bytes(block[4..8].try_into().unwrap());
        let (el, er) = bf.encrypt(l, r);
        assert_eq!(el, 0x4ef9_9745);
        assert_eq!(er, 0x6198_dd78);
        block[0..4].copy_from_slice(&el.to_be_bytes());
        block[4..8].copy_from_slice(&er.to_be_bytes());
        bf.decrypt_block(&mut block);
        assert_eq!(block, [0u8; 8]);
    }

    #[test]
    fn rsa_small_numbers() {
        let m = big_from_be(&[0x03, 0xe8]); // 1000
        assert_eq!(big_mod_pow(&big_from_le(&[2]), 10, &m), big_from_le(&[24]));
        let seven = big_from_le(&[7]);
        // 3^65537 mod 7: 3^6 == 1 (mod 7), 65537 mod 6 == 5, so 3^5 == 5 (mod 7).
        assert_eq!(big_mod_pow(&big_from_le(&[3]), 65_537, &seven), big_from_le(&[5]));
    }

    #[test]
    fn round_trips_a_pcx_to_png() {
        // 2x2, one RLE run per row, palette 11 -> red, 22 -> magenta (transparent).
        let mut d = vec![0u8; 128];
        d[0] = 0x0a;
        d[2] = 1;
        d[3] = 8;
        d[8] = 1; // xmax = 1 -> width 2
        d[10] = 1; // ymax = 1 -> height 2
        d[65] = 1;
        d[66] = 2; // bytes per line
        d.extend_from_slice(&[0xc2, 11, 0xc2, 22]);
        d.extend_from_slice(&[0x0c]);
        let mut pal = [[0u8; 3]; 256];
        pal[11] = [255, 0, 0];
        pal[22] = [255, 0, 255];
        for entry in pal {
            d.extend_from_slice(&entry);
        }
        let pcx = decode_pcx(&d).unwrap();
        assert_eq!((pcx.w, pcx.h), (2, 2));
        let png = flag_png(&d).unwrap();
        assert_eq!(&png[..8], b"\x89PNG\r\n\x1a\n");
        assert!(png.windows(4).any(|w| w == b"IHDR"));
        assert!(png.windows(4).any(|w| w == b"IEND"));
    }

    /// Reads the flags from a real install when `RA2_INSTALL_DIR` points at one (the engine's own tests do the
    /// same). Skipped in CI, and never needs the art to be committed.
    #[test]
    fn reads_the_players_own_flags_when_installed() {
        let Ok(dir) = std::env::var("RA2_INSTALL_DIR") else { return };
        let flags = country_flags(Path::new(&dir)).expect("flags");
        assert_eq!(flags.len(), 10, "one picture per country");
        for f in &flags {
            assert!(f.png.starts_with("data:image/png;base64,"));
            assert!(f.png.len() > 200);
        }
    }
}
