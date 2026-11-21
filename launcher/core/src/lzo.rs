//! LZO1X decompression, the format Red Alert 2 packs its map previews and terrain in (`[PreviewPack]`,
//! `[IsoMapPack5]`). Written from the format of liblzo2's `lzo1x_d.ch`: every read and copy is checked, malformed
//! input is an `Err`, and the output never grows past the caller's limit.

/// Unpack one LZO1X stream (it ends with the `11 00 00` end marker), producing at most `limit` bytes.
pub fn decompress(src: &[u8], limit: usize) -> Result<Vec<u8>, String> {
    let bad = || "damaged LZO data".to_string();
    let mut out: Vec<u8> = Vec::new();
    let mut ip = 0usize;
    let byte = |ip: &mut usize| -> Result<usize, String> {
        let b = *src.get(*ip).ok_or_else(bad)?;
        *ip += 1;
        Ok(b as usize)
    };
    // a length that continues in following bytes: each zero adds 255, the first non-zero ends it
    let long = |ip: &mut usize, base: usize| -> Result<usize, String> {
        let mut n = base;
        loop {
            match byte(ip)? {
                0 => n = n.checked_add(255).filter(|&n| n <= limit).ok_or_else(bad)?,
                b => return Ok(n + b),
            }
        }
    };
    let literals = |ip: &mut usize, out: &mut Vec<u8>, n: usize| -> Result<(), String> {
        let run = src.get(*ip..*ip + n).ok_or_else(bad)?;
        if out.len() + n > limit {
            return Err(bad());
        }
        out.extend_from_slice(run);
        *ip += n;
        Ok(())
    };
    let copy = |out: &mut Vec<u8>, dist: usize, n: usize| -> Result<(), String> {
        if dist > out.len() || out.len() + n > limit {
            return Err(bad());
        }
        let from = out.len() - dist;
        for i in 0..n {
            out.push(out[from + i]); // byte by byte: a match may overlap what it writes
        }
        Ok(())
    };
    // The states of lzo1x_d.ch: an instruction after a literal run (or at the start), the one straight after the
    // first literal run, a match instruction, its end (the low two bits say how many literals follow) and those literals.
    enum S {
        Top,
        AfterLiterals,
        Match,
        MatchDone,
        Trailing,
    }
    let mut t: usize;
    let mut s = S::Top;
    if *src.first().ok_or_else(bad)? > 17 {
        t = byte(&mut ip)? - 17;
        if t < 4 {
            s = S::Trailing;
        } else {
            literals(&mut ip, &mut out, t)?;
            s = S::AfterLiterals;
        }
    } else {
        t = 0;
    }
    loop {
        match s {
            S::Top => {
                t = byte(&mut ip)?;
                if t >= 16 {
                    s = S::Match;
                    continue;
                }
                let n = if t == 0 { long(&mut ip, 15)? } else { t };
                literals(&mut ip, &mut out, n + 3)?;
                s = S::AfterLiterals;
            }
            S::AfterLiterals => {
                t = byte(&mut ip)?;
                if t >= 16 {
                    s = S::Match;
                    continue;
                }
                // a three-byte match 2049 to 3072 back
                let dist = 1 + 0x800 + (t >> 2) + (byte(&mut ip)? << 2);
                copy(&mut out, dist, 3)?;
                s = S::MatchDone;
            }
            S::Match => {
                let (dist, n) = if t >= 64 {
                    (1 + ((t >> 2) & 7) + (byte(&mut ip)? << 3), (t >> 5) + 1)
                } else if t >= 32 {
                    let n = if t & 31 == 0 { long(&mut ip, 31)? } else { t & 31 };
                    let d = byte(&mut ip)? >> 2;
                    (1 + d + (byte(&mut ip)? << 6), n + 2)
                } else if t >= 16 {
                    let high = (t & 8) << 11;
                    let n = if t & 7 == 0 { long(&mut ip, 7)? } else { t & 7 };
                    let d = byte(&mut ip)? >> 2;
                    let d = high + d + (byte(&mut ip)? << 6);
                    if d == 0 {
                        // the end marker: the whole input must have been used
                        return if ip == src.len() { Ok(out) } else { Err(bad()) };
                    }
                    (d + 0x4000, n + 2)
                } else {
                    (1 + (t >> 2) + (byte(&mut ip)? << 2), 2)
                };
                copy(&mut out, dist, n)?;
                s = S::MatchDone;
            }
            S::MatchDone => {
                t = (*src.get(ip.wrapping_sub(2)).ok_or_else(bad)? & 3) as usize; // the low bits of the match's first distance byte
                s = if t == 0 { S::Top } else { S::Trailing };
            }
            S::Trailing => {
                literals(&mut ip, &mut out, t)?;
                t = byte(&mut ip)?;
                s = S::Match;
            }
        }
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    // Made with liblzo2 2.10 (`lzo1x_1_compress` and `lzo1x_999_compress`) from the inputs below.
    const HELLO_1: [u8; 89] = [3, 104, 101, 108, 108, 111, 32, 42, 20, 0, 3, 119, 111, 114, 108, 100, 44, 200, 2, 144, 1, 0, 14, 33, 32, 116, 104, 101, 32, 113, 117, 105, 99, 107, 32, 98, 114, 111, 119, 110, 32, 102, 111, 120, 32, 106, 117, 109, 112, 115, 32, 111, 118, 101, 114, 152, 3, 6, 108, 97, 122, 121, 32, 100, 111, 103, 46, 148, 1, 13, 113, 117, 105, 99, 107, 32, 98, 114, 111, 119, 110, 32, 102, 111, 120, 46, 17, 0, 0];
    const FAR_999: [u8; 461] = [0, 0, 34, 198, 126, 129, 107, 75, 251, 226, 251, 84, 246, 189, 223, 124, 28, 225, 135, 1, 191, 49, 222, 86, 114, 15, 71, 103, 102, 135, 89, 170, 136, 60, 89, 234, 86, 19, 123, 210, 133, 161, 216, 60, 84, 85, 47, 55, 174, 101, 91, 218, 2, 121, 152, 204, 227, 26, 118, 142, 95, 217, 153, 143, 31, 63, 54, 238, 67, 120, 77, 13, 250, 190, 166, 218, 228, 134, 142, 220, 41, 109, 78, 255, 86, 225, 112, 32, 251, 143, 177, 88, 5, 144, 197, 9, 220, 83, 205, 170, 59, 72, 153, 82, 211, 82, 157, 6, 159, 234, 181, 194, 6, 19, 152, 73, 178, 1, 30, 172, 50, 136, 49, 156, 82, 70, 149, 113, 54, 143, 87, 246, 57, 29, 22, 250, 136, 116, 245, 152, 124, 23, 92, 65, 187, 109, 113, 142, 15, 112, 89, 199, 1, 27, 47, 51, 61, 145, 192, 29, 165, 13, 13, 171, 51, 141, 126, 94, 143, 62, 230, 104, 116, 166, 58, 177, 195, 147, 17, 168, 100, 199, 219, 202, 224, 96, 225, 243, 191, 9, 0, 103, 162, 227, 37, 160, 33, 49, 135, 213, 98, 197, 168, 79, 126, 46, 9, 107, 148, 159, 176, 109, 169, 158, 90, 11, 70, 112, 128, 182, 207, 71, 12, 166, 165, 42, 216, 172, 251, 160, 235, 183, 121, 36, 114, 35, 146, 72, 128, 197, 166, 167, 133, 183, 215, 140, 144, 228, 171, 99, 68, 82, 102, 227, 156, 51, 37, 249, 94, 170, 186, 115, 96, 93, 75, 113, 126, 190, 169, 140, 87, 25, 113, 195, 202, 94, 229, 42, 51, 172, 136, 81, 102, 161, 123, 117, 103, 100, 154, 105, 239, 111, 86, 66, 160, 29, 81, 197, 2, 247, 187, 146, 69, 0, 1, 2, 3, 4, 5, 6, 32, 0, 0, 0, 0, 0, 0, 0, 230, 24, 0, 32, 0, 0, 0, 0, 0, 0, 0, 230, 24, 0, 32, 0, 0, 0, 0, 0, 0, 0, 230, 24, 0, 32, 0, 0, 0, 0, 0, 0, 0, 230, 24, 0, 32, 0, 0, 0, 0, 0, 0, 0, 230, 24, 0, 32, 0, 0, 0, 0, 0, 0, 0, 230, 24, 0, 32, 0, 0, 0, 0, 0, 0, 0, 230, 24, 0, 32, 0, 0, 0, 0, 0, 0, 0, 230, 24, 0, 32, 0, 0, 66, 24, 0, 16, 0, 36, 80, 14, 0, 22, 190, 111, 13, 182, 56, 204, 16, 253, 187, 84, 81, 28, 123, 7, 148, 39, 147, 125, 146, 195, 212, 198, 165, 97, 81, 1, 56, 56, 167, 191, 241, 4, 13, 21, 155, 128, 31, 131, 213, 164, 159, 29, 105, 136, 124, 84, 0, 17, 0, 0];
    pub(crate) const GRAD_999: [u8; 86] = [20, 0, 0, 128, 39, 9, 0, 60, 41, 9, 0, 120, 41, 9, 0, 180, 41, 8, 0, 32, 16, 189, 0, 60, 40, 9, 0, 60, 41, 9, 0, 120, 41, 9, 0, 180, 41, 8, 0, 32, 16, 189, 0, 120, 40, 9, 0, 60, 41, 9, 0, 120, 41, 9, 0, 180, 41, 8, 0, 32, 16, 189, 0, 180, 40, 9, 0, 60, 41, 9, 0, 120, 41, 9, 0, 180, 41, 8, 0, 32, 15, 188, 0, 17, 0, 0];
    const HELLO_999: [u8; 73] = [23, 104, 101, 108, 108, 111, 32, 42, 20, 0, 3, 119, 111, 114, 108, 100, 44, 42, 51, 0, 33, 32, 116, 4, 3, 0, 9, 32, 113, 117, 105, 99, 107, 32, 98, 114, 111, 119, 110, 32, 102, 111, 120, 32, 106, 117, 109, 112, 115, 32, 111, 118, 101, 114, 152, 3, 6, 108, 97, 122, 121, 32, 100, 111, 103, 46, 50, 177, 0, 46, 17, 0, 0];
    const FAR_1: [u8; 473] = [0, 0, 66, 198, 126, 129, 107, 75, 251, 226, 251, 84, 246, 189, 223, 124, 28, 225, 135, 1, 191, 49, 222, 86, 114, 15, 71, 103, 102, 135, 89, 170, 136, 60, 89, 234, 86, 19, 123, 210, 133, 161, 216, 60, 84, 85, 47, 55, 174, 101, 91, 218, 2, 121, 152, 204, 227, 26, 118, 142, 95, 217, 153, 143, 31, 63, 54, 238, 67, 120, 77, 13, 250, 190, 166, 218, 228, 134, 142, 220, 41, 109, 78, 255, 86, 225, 112, 32, 251, 143, 177, 88, 5, 144, 197, 9, 220, 83, 205, 170, 59, 72, 153, 82, 211, 82, 157, 6, 159, 234, 181, 194, 6, 19, 152, 73, 178, 1, 30, 172, 50, 136, 49, 156, 82, 70, 149, 113, 54, 143, 87, 246, 57, 29, 22, 250, 136, 116, 245, 152, 124, 23, 92, 65, 187, 109, 113, 142, 15, 112, 89, 199, 1, 27, 47, 51, 61, 145, 192, 29, 165, 13, 13, 171, 51, 141, 126, 94, 143, 62, 230, 104, 116, 166, 58, 177, 195, 147, 17, 168, 100, 199, 219, 202, 224, 96, 225, 243, 191, 9, 0, 103, 162, 227, 37, 160, 33, 49, 135, 213, 98, 197, 168, 79, 126, 46, 9, 107, 148, 159, 176, 109, 169, 158, 90, 11, 70, 112, 128, 182, 207, 71, 12, 166, 165, 42, 216, 172, 251, 160, 235, 183, 121, 36, 114, 35, 146, 72, 128, 197, 166, 167, 133, 183, 215, 140, 144, 228, 171, 99, 68, 82, 102, 227, 156, 51, 37, 249, 94, 170, 186, 115, 96, 93, 75, 113, 126, 190, 169, 140, 87, 25, 113, 195, 202, 94, 229, 42, 51, 172, 136, 81, 102, 161, 123, 117, 103, 100, 154, 105, 239, 111, 86, 66, 160, 29, 81, 197, 2, 247, 187, 146, 69, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 98, 80, 0, 16, 0, 36, 80, 14, 0, 33, 190, 111, 13, 182, 56, 204, 16, 253, 187, 84, 81, 28, 123, 7, 148, 39, 147, 125, 146, 195, 212, 198, 165, 97, 81, 1, 56, 56, 167, 191, 241, 4, 13, 21, 155, 128, 31, 131, 213, 164, 82, 211, 82, 157, 6, 105, 136, 124, 82, 157, 6, 17, 0, 0];

    const HELLO: &[u8] = b"hello hello hello world, hello world! the quick brown fox jumps over the lazy dog. the quick brown fox.";

    /// 300 random bytes, 17000 of a short cycle, the 300 again (more than 16 kB back), and a few short matches.
    fn far() -> Vec<u8> {
        let mut st: u32 = 1;
        let mut rnd = || {
            st = st.wrapping_mul(1103515245).wrapping_add(12345);
            (st >> 16) as u8
        };
        let mut b: Vec<u8> = (0..300).map(|_| rnd()).collect();
        b.extend((0..17000).map(|i| (i % 7) as u8));
        b.extend_from_within(0..300);
        b.extend((0..40).map(|_| rnd()));
        b.extend_from_within(100..105);
        b.extend((0..3).map(|_| rnd()));
        for _ in 0..3 {
            b.push(b[b.len() - 6]);
        }
        b
    }

    /// A 16 by 8 picture in blue-green-red bytes, in blocks of colour.
    pub(crate) fn gradient() -> Vec<u8> {
        (0..8).flat_map(|y| (0..16).flat_map(move |x| [(x / 4 * 60) as u8, (y / 2 * 60) as u8, 128])).collect()
    }

    #[test]
    fn unpacks_what_liblzo2_packed() {
        assert_eq!(decompress(&HELLO_1, 1 << 20).unwrap(), HELLO);
        assert_eq!(decompress(&HELLO_999, 1 << 20).unwrap(), HELLO);
        assert_eq!(decompress(&FAR_1, 1 << 20).unwrap(), far());
        assert_eq!(decompress(&FAR_999, 1 << 20).unwrap(), far());
        assert_eq!(decompress(&GRAD_999, 1 << 20).unwrap(), gradient());
    }

    #[test]
    fn damaged_or_oversized_input_is_an_error() {
        for v in [&HELLO_1[..], &HELLO_999, &FAR_1, &FAR_999, &GRAD_999] {
            for cut in 0..v.len() {
                assert!(decompress(&v[..cut], 1 << 20).is_err(), "cut at {cut}");
            }
            let mut extra = v.to_vec();
            extra.push(0);
            assert!(decompress(&extra, 1 << 20).is_err(), "bytes after the end marker");
        }
        assert!(decompress(&FAR_1, 1000).is_err(), "more than the limit");
        // a match further back than anything written
        assert!(decompress(&[0x12, b'a', 0x7c, 0x00, 0x11, 0x00, 0x00], 100).is_err());
        // every bit pattern of the first bytes, and a byte flipped everywhere, never panics
        let mut st: u32 = 7;
        for _ in 0..20000 {
            let junk: Vec<u8> = (0..24).map(|_| {
                st = st.wrapping_mul(1103515245).wrapping_add(12345);
                (st >> 16) as u8
            }).collect();
            let _ = decompress(&junk, 4096);
        }
        for i in 0..FAR_999.len() {
            let mut v = FAR_999.to_vec();
            v[i] ^= 0x5a;
            let _ = decompress(&v, 1 << 20);
        }
    }
}
