//! Self-update through GitHub releases, the same on every OS: ask GitHub for the newest release (prereleases
//! count, since alphas are what exist), pick the installer for this machine, download it, check it against the
//! release's SHA256SUMS, and open it. Installing is left to the platform's own installer, so there is no signing
//! key to manage. (The checksum comes from the same release, so it guards against corrupt downloads, not against
//! a compromised repository.)
use crate::extract::{hex, sha256_file};
use crate::proc::quiet;
use serde::{Deserialize, Serialize};
use std::cmp::Ordering;
use std::path::{Path, PathBuf};
use std::process::Command;

pub const REPO: &str = "michaelmurfy/rallypoint";
const API: &str = "https://api.github.com/repos/michaelmurfy/rallypoint/releases?per_page=15";
const DOWNLOAD_PREFIX: &str = "https://github.com/michaelmurfy/rallypoint/releases/download/";

/// The running version: the release tag when CI built this binary, otherwise the crate version.
pub fn current_version() -> &'static str {
    option_env!("RALLYPOINT_VERSION").unwrap_or(env!("CARGO_PKG_VERSION")).trim_start_matches('v')
}

// ---- versions (semver precedence, which is what makes alpha.10 newer than alpha.9) -----------------------------

#[derive(Debug, Clone, PartialEq, Eq)]
enum Ident {
    Num(u64),
    Alpha(String),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Version {
    core: [u64; 3],
    pre: Vec<Ident>,
}

/// `v0.1.0-alpha.3` -> Version. Build metadata (`+...`) is ignored.
pub fn parse_version(s: &str) -> Option<Version> {
    let s = s.trim().trim_start_matches('v');
    let s = s.split('+').next()?;
    let (core, pre) = match s.split_once('-') {
        Some((c, p)) => (c, Some(p)),
        None => (s, None),
    };
    let mut it = core.split('.');
    let mut nums = [0u64; 3];
    for n in nums.iter_mut() {
        *n = it.next()?.parse().ok()?;
    }
    if it.next().is_some() {
        return None;
    }
    let pre = pre
        .map(|p| p.split('.').map(|i| i.parse().map(Ident::Num).unwrap_or_else(|_| Ident::Alpha(i.to_string()))).collect())
        .unwrap_or_default();
    Some(Version { core: nums, pre })
}

impl Ord for Version {
    fn cmp(&self, o: &Self) -> Ordering {
        self.core.cmp(&o.core).then_with(|| match (self.pre.is_empty(), o.pre.is_empty()) {
            (true, true) => Ordering::Equal,
            (true, false) => Ordering::Greater, // 1.0.0 is newer than 1.0.0-alpha
            (false, true) => Ordering::Less,
            (false, false) => {
                for (a, b) in self.pre.iter().zip(&o.pre) {
                    let c = match (a, b) {
                        (Ident::Num(x), Ident::Num(y)) => x.cmp(y),
                        (Ident::Num(_), Ident::Alpha(_)) => Ordering::Less,
                        (Ident::Alpha(_), Ident::Num(_)) => Ordering::Greater,
                        (Ident::Alpha(x), Ident::Alpha(y)) => x.cmp(y),
                    };
                    if c != Ordering::Equal {
                        return c;
                    }
                }
                self.pre.len().cmp(&o.pre.len())
            }
        })
    }
}
impl PartialOrd for Version {
    fn partial_cmp(&self, o: &Self) -> Option<Ordering> {
        Some(self.cmp(o))
    }
}

// ---- releases ---------------------------------------------------------------------------------------------------

#[derive(Debug, Clone, Deserialize)]
pub struct Asset {
    pub name: String,
    #[serde(default)]
    pub size: u64,
    pub browser_download_url: String,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Release {
    pub tag_name: String,
    pub html_url: String,
    #[serde(default)]
    pub body: Option<String>,
    #[serde(default)]
    pub draft: bool,
    #[serde(default)]
    pub assets: Vec<Asset>,
}

#[derive(Debug, Clone, Serialize)]
pub struct UpdateInfo {
    pub current: String,
    pub latest: String,
    pub page: String,
    pub notes: String,
    /// Installer for this machine, if the release has one.
    #[serde(skip)]
    pub asset: Option<Asset>,
    #[serde(skip)]
    pub sums: Option<Asset>,
    pub size_mb: u64,
    /// There is an installer for this computer, so it can be downloaded from inside the app.
    pub installer: bool,
}

/// The installer file for an OS/architecture, by the names our release workflow produces.
pub fn installer_for(os: &str, arch: &str, assets: &[Asset]) -> Option<Asset> {
    let pick = |f: &dyn Fn(&str) -> bool| assets.iter().find(|a| f(&a.name.to_ascii_lowercase())).cloned();
    match (os, arch) {
        ("macos", "aarch64") => pick(&|n| n.ends_with(".dmg") && (n.contains("aarch64") || n.contains("arm64"))),
        ("macos", "x86_64") => pick(&|n| n.ends_with(".dmg") && (n.contains("x64") || n.contains("x86_64"))),
        ("windows", _) => pick(&|n| n.ends_with("-setup.exe")),
        ("linux", "x86_64") => pick(&|n| n.ends_with(".appimage") && (n.contains("amd64") || n.contains("x86_64"))),
        _ => None,
    }
}

/// Newer than `current`? Drafts are ignored; prereleases count. An unparseable running version counts as 0.0.0.
pub fn newer(current: &str, releases: &[Release], os: &str, arch: &str) -> Option<UpdateInfo> {
    let cur = parse_version(current).unwrap_or(Version { core: [0, 0, 0], pre: vec![] });
    let best = releases.iter().filter(|r| !r.draft).filter_map(|r| Some((parse_version(&r.tag_name)?, r))).max_by(|a, b| a.0.cmp(&b.0))?;
    if best.0 <= cur {
        return None;
    }
    let r = best.1;
    let asset = installer_for(os, arch, &r.assets);
    Some(UpdateInfo {
        current: current.trim_start_matches('v').to_string(),
        latest: r.tag_name.trim_start_matches('v').to_string(),
        page: r.html_url.clone(),
        notes: r.body.clone().unwrap_or_default().chars().take(1500).collect(),
        sums: r.assets.iter().find(|a| a.name == "SHA256SUMS").cloned(),
        size_mb: asset.as_ref().map(|a| a.size / 1_048_576).unwrap_or(0),
        installer: asset.is_some(),
        asset,
    })
}

fn curl(url: &str, proto: &str, out: Option<&Path>) -> Result<Vec<u8>, String> {
    let mut c = Command::new("curl");
    quiet(&mut c).args(["-fsSL", "--proto", proto, "--proto-redir", proto, "-H", "Accept: application/vnd.github+json", "-H", "User-Agent: rallypoint-updater", "--max-time", "900"]);
    if let Some(o) = out {
        c.arg("-o").arg(o);
    }
    let r = c.arg(url).output().map_err(|e| format!("curl isn't available ({e})"))?;
    if r.status.success() { Ok(r.stdout) } else { Err("couldn't reach GitHub".into()) }
}

pub fn parse_releases(json: &[u8]) -> Result<Vec<Release>, String> {
    serde_json::from_slice(json).map_err(|e| format!("unexpected answer from GitHub: {e}"))
}

/// Ask GitHub whether a newer release exists.
pub fn check(current: &str) -> Result<Option<UpdateInfo>, String> {
    let rel = parse_releases(&curl(API, "=https", None)?)?;
    Ok(newer(current, &rel, std::env::consts::OS, std::env::consts::ARCH))
}

/// `<hex>  <file>` lines (sha256sum output); the hash for `name`.
pub fn checksum_for(sums: &str, name: &str) -> Option<String> {
    sums.lines().filter_map(|l| l.split_once(char::is_whitespace)).find(|(_, f)| f.trim().trim_start_matches('*') == name).map(|(h, _)| h.to_ascii_lowercase())
}

pub fn downloads_dir() -> PathBuf {
    crate::mods::downloads_dir().filter(|d| d.is_dir()).unwrap_or_else(std::env::temp_dir)
}

fn fetch_verified(asset: &Asset, sums_text: &str, dest_dir: &Path, proto: &str) -> Result<PathBuf, String> {
    let want = checksum_for(sums_text, &asset.name).ok_or("the release has no checksum for this installer")?;
    let dest = dest_dir.join(&asset.name);
    let part = dest_dir.join(format!("{}.part", asset.name));
    curl(&asset.browser_download_url, proto, Some(&part))?;
    let got = hex(&sha256_file(&part).map_err(|e| e.to_string())?);
    if got != want {
        let _ = std::fs::remove_file(&part);
        return Err("the download didn't match its checksum, so it was discarded".into());
    }
    std::fs::rename(&part, &dest).map_err(|e| e.to_string())?;
    Ok(dest)
}

/// Download this machine's installer into `dest_dir` and verify it. Only files from this repository's releases.
pub fn download(info: &UpdateInfo, dest_dir: &Path) -> Result<PathBuf, String> {
    let asset = info.asset.as_ref().ok_or("this release has no installer for your computer; open the release page")?;
    let sums = info.sums.as_ref().ok_or("this release has no checksums, so it won't be installed automatically")?;
    if !asset.browser_download_url.starts_with(DOWNLOAD_PREFIX) || !sums.browser_download_url.starts_with(DOWNLOAD_PREFIX) {
        return Err("the release points outside this repository; refusing to download it".into());
    }
    let sums_text = String::from_utf8_lossy(&curl(&sums.browser_download_url, "=https", None)?).into_owned();
    fetch_verified(asset, &sums_text, dest_dir, "=https")
}

/// Hand the installer to the OS. Returns what the user should do next.
pub fn open_installer(path: &Path) -> Result<String, String> {
    #[cfg(target_os = "macos")]
    {
        Command::new("open").arg(path).spawn().map_err(|e| e.to_string())?;
        Ok("The installer disk image is open. Quit Rallypoint, then drag the new Rallypoint onto Applications.".into())
    }
    #[cfg(windows)]
    {
        Command::new(path).spawn().map_err(|e| e.to_string())?;
        Ok("The installer is starting. Quit Rallypoint and follow its steps.".into())
    }
    #[cfg(all(unix, not(target_os = "macos")))]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o755)).map_err(|e| e.to_string())?;
        if let Some(dir) = path.parent() {
            let _ = Command::new("xdg-open").arg(dir).spawn();
        }
        Ok(format!("Saved {}. Quit Rallypoint and run the new file.", path.display()))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn v(s: &str) -> Version {
        parse_version(s).unwrap()
    }

    #[test]
    fn orders_versions_like_semver() {
        assert!(v("0.1.0-alpha.10") > v("0.1.0-alpha.9"), "numeric parts compare as numbers");
        assert!(v("0.1.0-alpha.3") > v("0.1.0-alpha.2"));
        assert!(v("0.1.0") > v("0.1.0-alpha.99"), "a release beats its own prereleases");
        assert!(v("0.1.0-beta.1") > v("0.1.0-alpha.9"));
        assert!(v("0.2.0-alpha.1") > v("0.1.9"));
        assert!(v("v1.0.0") == v("1.0.0+build5"));
        assert!(v("0.1.0-alpha") < v("0.1.0-alpha.1"), "more identifiers wins on a tie");
        for bad in ["", "x", "1.2", "1.2.3.4", "1.a.3"] {
            assert!(parse_version(bad).is_none(), "{bad:?}");
        }
    }

    fn asset(name: &str) -> Asset {
        Asset { name: name.into(), size: 8 * 1_048_576, browser_download_url: format!("{DOWNLOAD_PREFIX}v1/{name}") }
    }
    fn assets() -> Vec<Asset> {
        ["Rallypoint-0.1.0-1.x86_64.rpm", "rallypoint-server-linux-x86_64.tar.gz", "Rallypoint_0.1.0_aarch64.dmg", "Rallypoint_0.1.0_amd64.AppImage", "Rallypoint_0.1.0_amd64.deb", "Rallypoint_0.1.0_x64-setup.exe", "SHA256SUMS"].iter().map(|n| asset(n)).collect()
    }

    #[test]
    fn picks_the_installer_for_each_platform() {
        let a = assets();
        assert_eq!(installer_for("macos", "aarch64", &a).unwrap().name, "Rallypoint_0.1.0_aarch64.dmg");
        assert_eq!(installer_for("windows", "x86_64", &a).unwrap().name, "Rallypoint_0.1.0_x64-setup.exe");
        assert_eq!(installer_for("linux", "x86_64", &a).unwrap().name, "Rallypoint_0.1.0_amd64.AppImage");
        assert!(installer_for("macos", "x86_64", &a).is_none(), "no Intel Mac build, so no wrong-architecture download");
        assert!(installer_for("freebsd", "x86_64", &a).is_none());
    }

    fn release(tag: &str, draft: bool) -> Release {
        Release { tag_name: tag.into(), html_url: format!("https://github.com/{REPO}/releases/tag/{tag}"), body: Some("notes".into()), draft, assets: assets() }
    }

    #[test]
    fn finds_a_newer_release_including_prereleases_and_skips_drafts() {
        let rels = vec![release("v0.1.0-alpha.2", false), release("v0.1.0-alpha.4", false), release("v0.1.0-alpha.9", true), release("v0.1.0-alpha.3", false)];
        let u = newer("0.1.0-alpha.2", &rels, "macos", "aarch64").unwrap();
        assert_eq!((u.latest.as_str(), u.current.as_str()), ("0.1.0-alpha.4", "0.1.0-alpha.2"));
        assert!(u.asset.is_some() && u.sums.is_some() && u.size_mb == 8);
        assert!(newer("0.1.0-alpha.4", &rels, "macos", "aarch64").is_none(), "already current");
        assert!(newer("0.2.0", &rels, "macos", "aarch64").is_none(), "ahead of every release");
        assert!(newer("0.1.0-alpha.2", &[], "macos", "aarch64").is_none());
        // a build whose version isn't a version (a CI dry run) is offered the newest release
        assert!(newer("main", &rels, "windows", "x86_64").is_some());
    }

    #[test]
    fn parses_github_release_json() {
        let json = br#"[{"tag_name":"v0.1.0-alpha.3","html_url":"https://github.com/michaelmurfy/rallypoint/releases/tag/v0.1.0-alpha.3","body":null,"draft":false,"prerelease":true,
            "assets":[{"name":"SHA256SUMS","size":10,"browser_download_url":"https://github.com/michaelmurfy/rallypoint/releases/download/v0.1.0-alpha.3/SHA256SUMS","extra":1}],"unknown":true}]"#;
        let r = parse_releases(json).unwrap();
        assert_eq!(r[0].tag_name, "v0.1.0-alpha.3");
        assert!(parse_releases(b"{\"message\":\"rate limited\"}").is_err());
    }

    #[test]
    fn reads_sha256sum_lines() {
        let sums = "AAAA1111  Rallypoint_0.1.0_aarch64.dmg\nbbbb2222 *other.exe\n";
        assert_eq!(checksum_for(sums, "Rallypoint_0.1.0_aarch64.dmg").as_deref(), Some("aaaa1111"));
        assert_eq!(checksum_for(sums, "other.exe").as_deref(), Some("bbbb2222"));
        assert_eq!(checksum_for(sums, "missing"), None);
    }

    #[test]
    fn refuses_downloads_from_outside_the_repository() {
        let mut info = newer("0.0.1", &[release("v0.1.0", false)], "windows", "x86_64").unwrap();
        info.asset.as_mut().unwrap().browser_download_url = "https://evil.example/x-setup.exe".into();
        assert!(download(&info, Path::new(".")).unwrap_err().contains("outside this repository"));
        let mut no_sums = newer("0.0.1", &[release("v0.1.0", false)], "windows", "x86_64").unwrap();
        no_sums.sums = None;
        assert!(download(&no_sums, Path::new(".")).unwrap_err().contains("no checksums"));
    }

    /// The download and checksum path against local files (curl can fetch file:// URLs), so no network is needed.
    #[cfg(unix)]
    #[test]
    fn verifies_the_download_against_the_checksum() {
        let t = std::env::temp_dir().join(format!("gen-upd-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        std::fs::create_dir_all(t.join("out")).unwrap();
        let src = t.join("Rallypoint_9_x64-setup.exe");
        std::fs::write(&src, "installer bytes").unwrap();
        let good = hex(&sha256_file(&src).unwrap());
        let a = Asset { name: "Rallypoint_9_x64-setup.exe".into(), size: 15, browser_download_url: format!("file://{}", src.display()) };
        let ok = fetch_verified(&a, &format!("{good}  Rallypoint_9_x64-setup.exe\n"), &t.join("out"), "=file").unwrap();
        assert_eq!(std::fs::read(&ok).unwrap(), b"installer bytes");
        // a wrong checksum discards the file
        let bad = fetch_verified(&a, &format!("{}  Rallypoint_9_x64-setup.exe\n", "0".repeat(64)), &t.join("out2"), "=file");
        assert!(bad.is_err());
        std::fs::create_dir_all(t.join("out3")).unwrap();
        let e = fetch_verified(&a, &format!("{}  Rallypoint_9_x64-setup.exe\n", "0".repeat(64)), &t.join("out3"), "=file").unwrap_err();
        assert!(e.contains("didn't match"));
        assert!(!t.join("out3/Rallypoint_9_x64-setup.exe").exists() && !t.join("out3/Rallypoint_9_x64-setup.exe.part").exists());
        assert!(fetch_verified(&a, "", &t.join("out"), "=file").unwrap_err().contains("no checksum"));
        std::fs::remove_dir_all(t).unwrap();
    }
}
