//! Country limiting without a GeoIP database licence: per-country CIDR zone files (one `a.b.c.d/len` per line,
//! as published by ipdeny.com) that `make geo` downloads. Fails closed: a listed country with no file allows nothing.
use std::net::IpAddr;
use std::path::Path;

#[derive(Default)]
pub struct IpFilter {
    enabled: bool,
    allow_private: bool,
    ranges: Vec<(u32, u32)>, // sorted, merged, inclusive
}

const PRIVATE: [(u32, u32); 4] = [
    (0x0A00_0000, 0x0AFF_FFFF), // 10/8
    (0xAC10_0000, 0xAC1F_FFFF), // 172.16/12
    (0xC0A8_0000, 0xC0A8_FFFF), // 192.168/16
    (0x7F00_0000, 0x7FFF_FFFF), // 127/8
];

/// `1.2.3.0/24` -> inclusive range. Prefixes shorter than /8 are rejected so a bad line cannot open the whole internet.
fn parse_cidr(line: &str) -> Option<(u32, u32)> {
    let (ip, len) = line.trim().split_once('/')?;
    let ip: std::net::Ipv4Addr = ip.parse().ok()?;
    let len: u32 = len.parse().ok()?;
    if !(8..=32).contains(&len) {
        return None;
    }
    let mask = u32::MAX << (32 - len);
    let start = u32::from(ip) & mask;
    Some((start, start | !mask))
}

pub fn parse_cidrs(text: &str) -> Vec<(u32, u32)> {
    text.lines().filter_map(parse_cidr).collect()
}

impl IpFilter {
    pub fn disabled() -> IpFilter {
        IpFilter::default()
    }

    pub fn new(mut ranges: Vec<(u32, u32)>, allow_private: bool) -> IpFilter {
        ranges.sort_unstable();
        let mut merged: Vec<(u32, u32)> = vec![];
        for (s, e) in ranges {
            match merged.last_mut() {
                Some(last) if s <= last.1.saturating_add(1) => last.1 = last.1.max(e),
                _ => merged.push((s, e)),
            }
        }
        IpFilter { enabled: true, allow_private, ranges: merged }
    }

    pub fn allows(&self, ip: IpAddr) -> bool {
        if !self.enabled {
            return true;
        }
        // A dual-stack socket reports IPv4 clients as ::ffff:a.b.c.d; judge those by their IPv4 address. Real IPv6 is
        // refused: the zone files only cover IPv4.
        let ip = match ip {
            IpAddr::V6(a) => a.to_ipv4_mapped().map(IpAddr::V4).unwrap_or(ip),
            v4 => v4,
        };
        let IpAddr::V4(v4) = ip else { return false };
        let n = u32::from(v4);
        if self.allow_private && PRIVATE.iter().any(|&(s, e)| n >= s && n <= e) {
            return true;
        }
        let i = self.ranges.partition_point(|&(s, _)| s <= n);
        i > 0 && n <= self.ranges[i - 1].1
    }
}

/// Load `<dir>/<cc>.zone` for each country code. Returns the filter and warnings for anything unusable.
pub fn load(dir: &Path, countries: &[String], allow_private: bool) -> (IpFilter, Vec<String>) {
    let (mut ranges, mut warnings) = (vec![], vec![]);
    for cc in countries {
        let cc = cc.trim().to_ascii_lowercase();
        if cc.len() != 2 || !cc.bytes().all(|b| b.is_ascii_lowercase()) {
            warnings.push(format!("ignoring invalid country code {cc:?}"));
            continue; // also keeps env input out of file paths
        }
        match std::fs::read_to_string(dir.join(format!("{cc}.zone"))) {
            Ok(t) => {
                let r = parse_cidrs(&t);
                if r.is_empty() {
                    warnings.push(format!("{cc}.zone has no usable ranges; nobody from {} can connect", cc.to_uppercase()));
                }
                ranges.extend(r);
            }
            Err(_) => warnings.push(format!("no {cc}.zone in {} (run `make geo`); nobody from {} can connect", dir.display(), cc.to_uppercase())),
        }
    }
    (IpFilter::new(ranges, allow_private), warnings)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ip(s: &str) -> IpAddr {
        s.parse().unwrap()
    }

    #[test]
    fn parses_and_matches_ranges() {
        let f = IpFilter::new(parse_cidrs("1.2.3.0/24\n1.2.4.0/24\n# junk\n9.9.9.9\n0.0.0.0/0\n5.0.0.0/4\n203.0.113.7/32\n"), false);
        assert!(f.allows(ip("1.2.3.200")) && f.allows(ip("1.2.4.1"))); // adjacent ranges merge
        assert!(!f.allows(ip("1.2.5.1")) && !f.allows(ip("9.9.9.9")));
        assert!(!f.allows(ip("5.1.1.1")), "/4 and /0 lines are ignored");
        assert!(f.allows(ip("203.0.113.7")) && !f.allows(ip("203.0.113.8")));
    }

    #[test]
    fn private_ranges_and_ipv6() {
        let f = IpFilter::new(vec![], true);
        assert!(f.allows(ip("192.168.1.5")) && f.allows(ip("127.0.0.1")) && f.allows(ip("10.1.2.3")) && f.allows(ip("172.20.0.1")));
        assert!(!f.allows(ip("8.8.8.8")) && !f.allows(ip("::1")));
        // IPv4 clients of a dual-stack socket are judged by their IPv4 address; other IPv6 is never let through
        assert!(f.allows(ip("::ffff:192.168.1.5")) && !f.allows(ip("::ffff:8.8.8.8")) && !f.allows(ip("2001:db8::1")));
        let nz = IpFilter::new(parse_cidrs("1.2.3.0/24\n"), false);
        assert!(nz.allows(ip("::ffff:1.2.3.4")) && !nz.allows(ip("2001:db8::1")));
        assert!(!IpFilter::new(vec![], false).allows(ip("127.0.0.1")));
        assert!(IpFilter::disabled().allows(ip("8.8.8.8")));
    }

    #[test]
    fn load_fails_closed_and_rejects_bad_codes() {
        let d = std::env::temp_dir().join(format!("gen-geo-{}", std::process::id()));
        std::fs::create_dir_all(&d).unwrap();
        std::fs::write(d.join("nz.zone"), "1.2.3.0/24\n").unwrap();
        let (f, w) = load(&d, &["NZ".into(), "au".into(), "../x".into(), "n1".into()], false);
        assert!(f.allows(ip("1.2.3.4")) && !f.allows(ip("4.4.4.4")));
        assert_eq!(w.len(), 3); // au missing + two invalid codes
        std::fs::remove_dir_all(d).unwrap();
    }
}
