//! Self-update through GitHub releases, the same on every OS: ask GitHub for the newest release (prereleases
//! count, since alphas are what exist), pick the installer for this machine, download it, check it against the
//! release's SHA256SUMS, and open it. SHA256SUMS comes from the same release as the installer, so on its own it only
//! guards against corrupt downloads. It is therefore signed (Ed25519, `SHA256SUMS.sig`, key in `UPDATE_PUBLIC_KEY`)
//! and a release whose signature does not check is never installed from inside the app.
use crate::extract::{hex, sha256_file};
use crate::proc::quiet;
use ed25519_dalek::{Signature, Verifier, VerifyingKey};
use serde::{Deserialize, Serialize};
use std::cmp::Ordering;
use std::path::{Path, PathBuf};
use std::process::Command;

pub const REPO: &str = "michaelmurfy/rallypoint";
const API: &str = "https://api.github.com/repos/michaelmurfy/rallypoint/releases?per_page=50";
const DOWNLOAD_PREFIX: &str = "https://github.com/michaelmurfy/rallypoint/releases/download/";
/// The public half of the release signing key (hex). The private half is a GitHub Actions secret and a backup held by
/// the maintainer; `cargo run -p generals-core --example release_sign -- keygen` makes a pair.
pub const UPDATE_PUBLIC_KEY: &str = "07eed9ab3aabb5451aa2899ac42df17425928685e731eab5e802e2c5e7a5466b";

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

impl Version {
    /// `0.7.0-beta.2` is a prerelease (alpha, beta, rc); `0.7.0` is not.
    pub fn is_prerelease(&self) -> bool {
        !self.pre.is_empty()
    }
}

/// Whether a version string names a prerelease. Anything that is not a version (a CI dry run) does not.
pub fn is_prerelease(version: &str) -> bool {
    parse_version(version).is_some_and(|v| v.is_prerelease())
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
    /// GitHub's own "pre-release" tick box; a prerelease version in the tag counts as well.
    #[serde(default)]
    pub prerelease: bool,
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
    /// The signature over SHA256SUMS.
    #[serde(skip)]
    pub sig: Option<Asset>,
    pub size_mb: u64,
    /// There is an installer for this computer, so it can be downloaded from inside the app.
    pub installer: bool,
    /// The offered release is a beta (or another prerelease).
    pub beta: bool,
    /// The player chose "Skip this version" for it: Settings still shows it, the start-up banner does not.
    pub skipped: bool,
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

/// The update to offer someone running `current`, or `None`.
///
/// - Drafts never count. Betas (a prerelease version in the tag, or GitHub's pre-release box) count only with `beta`:
///   they are opt-in. Someone running a beta with the option off is offered the next full release and nothing before it.
/// - Of the releases newer than `current`, the newest one that can be installed here (installer, `SHA256SUMS` and its
///   signature all uploaded) wins. A release that is still being uploaded, or has no installer for this computer, is only
///   offered (as a page to open) when nothing installable is newer than `current`.
/// - An unparseable running version (a CI dry run) counts as 0.0.0.
pub fn newer(current: &str, releases: &[Release], os: &str, arch: &str, beta: bool) -> Option<UpdateInfo> {
    let cur = parse_version(current).unwrap_or(Version { core: [0, 0, 0], pre: vec![] });
    let mut newer: Vec<(Version, &Release)> = releases
        .iter()
        .filter(|r| !r.draft)
        .filter_map(|r| Some((parse_version(&r.tag_name)?, r)))
        .filter(|(v, r)| *v > cur && (beta || !(v.is_prerelease() || r.prerelease)))
        .collect();
    newer.sort_by(|a, b| b.0.cmp(&a.0));
    let has = |r: &Release, name: &str| r.assets.iter().any(|a| a.name == name);
    let installable = |r: &Release| installer_for(os, arch, &r.assets).is_some() && has(r, "SHA256SUMS") && has(r, "SHA256SUMS.sig");
    let (version, r) = newer.iter().find(|(_, r)| installable(r)).or(newer.first())?;
    let asset = installer_for(os, arch, &r.assets);
    Some(UpdateInfo {
        current: current.trim_start_matches('v').to_string(),
        latest: r.tag_name.trim_start_matches('v').to_string(),
        page: r.html_url.clone(),
        notes: r.body.clone().unwrap_or_default(),
        sums: r.assets.iter().find(|a| a.name == "SHA256SUMS").cloned(),
        sig: r.assets.iter().find(|a| a.name == "SHA256SUMS.sig").cloned(),
        size_mb: asset.as_ref().map(|a| a.size / 1_048_576).unwrap_or(0),
        installer: asset.is_some(),
        asset,
        beta: version.is_prerelease() || r.prerelease,
        skipped: false,
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

/// Ask GitHub whether a newer release exists. `beta`: the player opted in to beta releases. `skipped`: the version they
/// chose to skip, which is still returned (Settings shows it) but marked.
pub fn check(current: &str, beta: bool, skipped: &str) -> Result<Option<UpdateInfo>, String> {
    let rel = parse_releases(&curl(API, "=https", None)?)?;
    Ok(newer(current, &rel, std::env::consts::OS, std::env::consts::ARCH, beta).map(|mut u| {
        u.skipped = !skipped.is_empty() && u.latest == skipped.trim_start_matches('v');
        u
    }))
}

/// `<hex>  <file>` lines (sha256sum output); the hash for `name`.
pub fn checksum_for(sums: &str, name: &str) -> Option<String> {
    sums.lines().filter_map(|l| l.split_once(char::is_whitespace)).find(|(_, f)| f.trim().trim_start_matches('*') == name).map(|(h, _)| h.to_ascii_lowercase())
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
/// An installer on disk whose checksum and signature chain has been checked, and the checksum it must still have when
/// it is run (the file sits in a folder other programs of this user can write to until then).
#[derive(Debug, Clone)]
pub struct Downloaded {
    pub path: PathBuf,
    pub sha256: String,
}

impl Downloaded {
    /// Still the file that was verified? Call right before running it.
    pub fn unchanged(&self) -> Result<(), String> {
        let now = hex(&sha256_file(&self.path).map_err(|e| e.to_string())?);
        if now == self.sha256 { Ok(()) } else { Err("the downloaded installer changed after it was checked, so it won't be run".into()) }
    }
}

/// Is `sig_text` (hex of the 64 signature bytes) a valid signature by `public_hex` over `message`?
pub fn verify_signature(public_hex: &str, message: &[u8], sig_text: &str) -> Result<(), String> {
    let key: [u8; 32] = unhex(public_hex.trim()).and_then(|v| v.try_into().ok()).ok_or("the app's update key is not set up")?;
    let key = VerifyingKey::from_bytes(&key).map_err(|_| "the app's update key is not valid".to_string())?;
    let sig: [u8; 64] = unhex(sig_text.trim()).and_then(|v| v.try_into().ok()).ok_or("the release's signature is malformed")?;
    key.verify(message, &Signature::from_bytes(&sig)).map_err(|_| "the release's signature does not match, so it won't be installed".to_string())
}

fn unhex(s: &str) -> Option<Vec<u8>> {
    if s.len() % 2 != 0 || !s.is_ascii() {
        return None;
    }
    (0..s.len()).step_by(2).map(|i| u8::from_str_radix(&s[i..i + 2], 16).ok()).collect()
}

/// A folder only this user can use (installers are kept here between the check and the run).
pub fn private_dir(dir: &Path) -> Result<(), String> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::{DirBuilderExt, PermissionsExt};
        std::fs::DirBuilder::new().recursive(true).mode(0o700).create(dir).map_err(|e| e.to_string())?;
        std::fs::set_permissions(dir, std::fs::Permissions::from_mode(0o700)).map_err(|e| e.to_string())
    }
    #[cfg(not(unix))]
    {
        // %APPDATA% is only readable and writable by this user (and administrators) by default
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())
    }
}

/// Download the installer for `info` into `dest_dir` after checking the signature on SHA256SUMS and then the
/// installer against it. A release without a valid signature is refused.
pub fn download(info: &UpdateInfo, dest_dir: &Path) -> Result<Downloaded, String> {
    download_with(info, dest_dir, UPDATE_PUBLIC_KEY, "=https", true)
}

fn download_with(info: &UpdateInfo, dest_dir: &Path, public_hex: &str, proto: &str, require_prefix: bool) -> Result<Downloaded, String> {
    let asset = info.asset.as_ref().ok_or("this release has no installer for your computer; open the release page")?;
    let sums = info.sums.as_ref().ok_or("this release has no checksums, so it won't be installed automatically")?;
    let sig = info.sig.as_ref().ok_or("this release isn't signed, so it won't be installed automatically; open the release page")?;
    if require_prefix && [asset, sums, sig].iter().any(|a| !a.browser_download_url.starts_with(DOWNLOAD_PREFIX)) {
        return Err("the release points outside this repository; refusing to download it".into());
    }
    let sums_bytes = curl(&sums.browser_download_url, proto, None)?;
    let sig_text = String::from_utf8_lossy(&curl(&sig.browser_download_url, proto, None)?).into_owned();
    verify_signature(public_hex, &sums_bytes, &sig_text)?;
    let sums_text = String::from_utf8_lossy(&sums_bytes).into_owned();
    private_dir(dest_dir)?;
    let path = fetch_verified(asset, &sums_text, dest_dir, proto)?;
    let sha256 = checksum_for(&sums_text, &asset.name).ok_or("the release has no checksum for this installer")?;
    Ok(Downloaded { path, sha256 })
}

/// `x.y.z` of an installer file as our releases name them (`Rallypoint_0.4.5_x64-setup.exe`, `Rallypoint_0.4.5_aarch64.dmg`,
/// `Rallypoint-0.4.5-1.x86_64.rpm`, and a `.part` of any of them); `None` for any other file.
pub fn installer_version(name: &str) -> Option<[u64; 3]> {
    let n = name.strip_suffix(".part").unwrap_or(name);
    let rest = n.strip_prefix("Rallypoint")?.strip_prefix(['_', '-'])?;
    if !["-setup.exe", ".dmg", ".AppImage", ".deb", ".rpm"].iter().any(|e| n.ends_with(e)) {
        return None;
    }
    let core: String = rest.chars().take_while(|c| c.is_ascii_digit() || *c == '.').collect();
    let v = parse_version(&core)?;
    Some(v.core)
}

/// After an update: remove what the updater left behind. Everything in `<app data>/update` goes except the log of the last
/// update (it says what happened if the update failed) and an installer for a newer version than the one running. The
/// updater only ever works in that folder: it does not read or write the user's Downloads folder, which on macOS asks the
/// user for permission each time. Returns how many things were removed.
pub fn cleanup(data: &Path, running: &str) -> usize {
    let mut gone = 0;
    for e in std::fs::read_dir(data.join("update")).into_iter().flatten().flatten() {
        if e.file_name() == "apply-update.log" {
            continue;
        }
        let p = e.path();
        // an installer for a newer version than the one running is an update that has not been run yet: keep it
        if e.file_name() == "download" {
            for f in std::fs::read_dir(&p).into_iter().flatten().flatten() {
                let newer = parse_version(running).zip(installer_version(&f.file_name().to_string_lossy())).is_some_and(|(now, v)| v > now.core);
                if !newer && std::fs::remove_file(f.path()).is_ok() {
                    gone += 1;
                }
            }
            let _ = std::fs::remove_dir(&p); // only if it is empty now
            continue;
        }
        let done = if e.file_type().map(|t| t.is_dir()).unwrap_or(false) { std::fs::remove_dir_all(&p) } else { std::fs::remove_file(&p) };
        if done.is_ok() {
            gone += 1;
        }
    }
    gone
}

/// Hand the installer to the OS. Returns what the user should do next.
pub fn open_installer(file: &Downloaded) -> Result<String, String> {
    file.unchanged()?;
    let path = file.path.as_path();
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
        ["Rallypoint-0.1.0-1.x86_64.rpm", "rallypoint-server-linux-x86_64.tar.gz", "Rallypoint_0.1.0_aarch64.dmg", "Rallypoint_0.1.0_amd64.AppImage", "Rallypoint_0.1.0_amd64.deb", "Rallypoint_0.1.0_x64-setup.exe", "SHA256SUMS", "SHA256SUMS.sig"].iter().map(|n| asset(n)).collect()
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
        Release { tag_name: tag.into(), html_url: format!("https://github.com/{REPO}/releases/tag/{tag}"), body: Some("notes".into()), draft, prerelease: false, assets: assets() }
    }

    #[test]
    fn with_betas_on_finds_the_newest_release_and_skips_drafts() {
        let rels = vec![release("v0.1.0-alpha.2", false), release("v0.1.0-alpha.4", false), release("v0.1.0-alpha.9", true), release("v0.1.0-alpha.3", false)];
        let u = newer("0.1.0-alpha.2", &rels, "macos", "aarch64", true).unwrap();
        assert_eq!((u.latest.as_str(), u.current.as_str()), ("0.1.0-alpha.4", "0.1.0-alpha.2"));
        assert!(u.asset.is_some() && u.sums.is_some() && u.size_mb == 8 && u.beta && !u.skipped);
        assert!(newer("0.1.0-alpha.4", &rels, "macos", "aarch64", true).is_none(), "already current");
        assert!(newer("0.2.0", &rels, "macos", "aarch64", true).is_none(), "ahead of every release");
        assert!(newer("0.1.0-alpha.2", &[], "macos", "aarch64", true).is_none());
        // a build whose version isn't a version (a CI dry run) is offered the newest release
        assert!(newer("main", &rels, "windows", "x86_64", true).is_some());
    }

    #[test]
    fn betas_are_only_offered_to_those_who_asked() {
        let rels = vec![release("v0.7.0-beta.2", false), release("v0.7.0-beta.1", false), release("v0.6.1", false), release("v0.6.0", false)];
        // on a full release, betas off: the beta is not an update
        assert!(newer("0.6.1", &rels, "windows", "x86_64", false).is_none());
        let stable = newer("0.6.0", &rels, "windows", "x86_64", false).unwrap();
        assert_eq!((stable.latest.as_str(), stable.beta), ("0.6.1", false));
        // opted in: the newest beta
        let beta = newer("0.6.1", &rels, "windows", "x86_64", true).unwrap();
        assert_eq!((beta.latest.as_str(), beta.beta), ("0.7.0-beta.2", true));
        // running a beta with the option off: no further betas, but the full release when it comes
        assert!(newer("0.7.0-beta.1", &rels, "windows", "x86_64", false).is_none());
        let mut later = rels.clone();
        later.insert(0, release("v0.7.0", false));
        assert_eq!(newer("0.7.0-beta.1", &later, "windows", "x86_64", false).unwrap().latest, "0.7.0");
        // GitHub's pre-release box counts as a beta even when the tag looks like a full release
        let mut ticked = release("v0.8.0", false);
        ticked.prerelease = true;
        assert!(newer("0.6.1", &[ticked.clone()], "windows", "x86_64", false).is_none());
        assert!(newer("0.6.1", &[ticked], "windows", "x86_64", true).unwrap().beta);
        assert!(is_prerelease("v0.7.0-beta.2") && !is_prerelease("0.7.0") && !is_prerelease("main"));
    }

    #[test]
    fn a_release_still_being_uploaded_does_not_hide_the_one_before_it() {
        // 0.6.2 was published a minute ago: the installers and the signature are not up yet
        let mut uploading = release("v0.6.2", false);
        uploading.assets.clear();
        let rels = vec![uploading.clone(), release("v0.6.1", false)];
        let u = newer("0.6.0", &rels, "macos", "aarch64", false).unwrap();
        assert_eq!((u.latest.as_str(), u.installer), ("0.6.1", true));
        // nothing installable is newer: the page of the new one is still worth showing
        let u = newer("0.6.1", &rels, "macos", "aarch64", false).unwrap();
        assert_eq!((u.latest.as_str(), u.installer), ("0.6.2", false));
        // unsigned counts as not installable too
        let mut unsigned = release("v0.6.2", false);
        unsigned.assets.retain(|a| a.name != "SHA256SUMS.sig");
        assert_eq!(newer("0.6.0", &[unsigned, release("v0.6.1", false)], "macos", "aarch64", false).unwrap().latest, "0.6.1");
    }

    #[test]
    fn release_notes_are_kept_whole() {
        let mut r = release("v0.2.0", false);
        let body = "word ".repeat(400);
        r.body = Some(body.clone());
        let u = newer("0.1.0", &[r], "windows", "x86_64", false).unwrap();
        assert_eq!(u.notes, body);
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
        let mut info = newer("0.0.1", &[release("v0.1.0", false)], "windows", "x86_64", false).unwrap();
        info.asset.as_mut().unwrap().browser_download_url = "https://evil.example/x-setup.exe".into();
        assert!(download(&info, Path::new(".")).unwrap_err().contains("outside this repository"));
        let mut no_sums = newer("0.0.1", &[release("v0.1.0", false)], "windows", "x86_64", false).unwrap();
        no_sums.sums = None;
        assert!(download(&no_sums, Path::new(".")).unwrap_err().contains("no checksums"));
        let mut unsigned = newer("0.0.1", &[release("v0.1.0", false)], "windows", "x86_64", false).unwrap();
        unsigned.sig = None;
        assert!(download(&unsigned, Path::new(".")).unwrap_err().contains("isn't signed"));
    }

    fn keypair(seed: u8) -> (ed25519_dalek::SigningKey, String) {
        let key = ed25519_dalek::SigningKey::from_bytes(&[seed; 32]);
        let public = key.verifying_key().as_bytes().iter().map(|b| format!("{b:02x}")).collect::<String>();
        (key, public)
    }
    fn sign(key: &ed25519_dalek::SigningKey, message: &[u8]) -> String {
        use ed25519_dalek::Signer;
        key.sign(message).to_bytes().iter().map(|b| format!("{b:02x}")).collect()
    }

    #[test]
    fn only_the_key_holder_can_make_a_release_installable() {
        let (key, public) = keypair(7);
        let sums = b"abc  file.exe\n";
        let sig = sign(&key, sums);
        assert!(verify_signature(&public, sums, &sig).is_ok());
        assert!(verify_signature(&public, sums, &format!("{sig}\n")).is_ok(), "a trailing newline is fine");
        // changed checksums, a signature by another key, junk, and a missing key are all refused
        assert!(verify_signature(&public, b"abc  other.exe\n", &sig).is_err());
        assert!(verify_signature(&public, sums, &sign(&keypair(8).0, sums)).is_err());
        assert!(verify_signature(&public, sums, "nonsense").is_err());
        assert!(verify_signature(&public, sums, &"0".repeat(128)).is_err());
        assert!(verify_signature("", sums, &sig).is_err());
        // the key built into the app is a real public key
        assert!(VerifyingKey::from_bytes(&unhex(UPDATE_PUBLIC_KEY).unwrap().try_into().unwrap()).is_ok());
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

    /// The whole chain on local files: signature on SHA256SUMS, then the installer against it, then the re-check at run time.
    #[cfg(unix)]
    #[test]
    fn a_signed_release_downloads_and_a_swapped_file_is_caught() {
        let t = std::env::temp_dir().join(format!("gen-upd-sig-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        std::fs::create_dir_all(&t).unwrap();
        let (key, public) = keypair(9);
        let name = "Rallypoint_9_x64-setup.exe";
        std::fs::write(t.join(name), "installer bytes").unwrap();
        let sums = format!("{}  {name}\n", hex(&sha256_file(&t.join(name)).unwrap()));
        std::fs::write(t.join("SHA256SUMS"), &sums).unwrap();
        std::fs::write(t.join("SHA256SUMS.sig"), sign(&key, sums.as_bytes())).unwrap();
        let at = |n: &str| Asset { name: n.into(), size: 15, browser_download_url: format!("file://{}", t.join(n).display()) };
        let info = UpdateInfo { current: "0.0.1".into(), latest: "9".into(), page: String::new(), notes: String::new(), asset: Some(at(name)), sums: Some(at("SHA256SUMS")), sig: Some(at("SHA256SUMS.sig")), size_mb: 0, installer: true, beta: false, skipped: false };
        let got = download_with(&info, &t.join("private"), &public, "=file", false).unwrap();
        got.unchanged().unwrap();
        // the folder is private, and a file replaced after the check is not run
        use std::os::unix::fs::PermissionsExt;
        assert_eq!(std::fs::metadata(t.join("private")).unwrap().permissions().mode() & 0o777, 0o700);
        std::fs::write(&got.path, "something else").unwrap();
        assert!(got.unchanged().unwrap_err().contains("changed after it was checked"));
        // a different key refuses the same release
        assert!(download_with(&info, &t.join("private2"), &keypair(10).1, "=file", false).unwrap_err().contains("signature"));
        assert!(!t.join("private2").exists(), "nothing is written before the signature is good");
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn installer_names_are_recognised_by_version() {
        for (name, v) in [("Rallypoint_0.4.5_x64-setup.exe", [0, 4, 5]), ("Rallypoint_0.4.5_aarch64.dmg", [0, 4, 5]), ("Rallypoint_0.1.0_amd64.AppImage", [0, 1, 0]), ("Rallypoint-0.4.2-1.x86_64.rpm", [0, 4, 2]), ("Rallypoint_0.4.5_x64-setup.exe.part", [0, 4, 5]), ("Rallypoint_0.4.5_amd64.deb", [0, 4, 5])] {
            assert_eq!(installer_version(name), Some(v), "{name}");
        }
        for other in ["Rallypoint.dmg", "Rallypoint_notes.txt", "Shockwave_1.0_x64-setup.exe", "rallypoint-server-0.4.5-linux-x86_64.tar.gz", "Rallypoint_0.4.5_x64.zip", "report.exe"] {
            assert_eq!(installer_version(other), None, "{other}");
        }
    }

    #[test]
    fn what_the_updater_left_behind_is_removed_and_nothing_else() {
        let t = std::env::temp_dir().join(format!("gen-upd-clean-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        let data = t.join("data");
        std::fs::create_dir_all(data.join("update/download")).unwrap();
        std::fs::create_dir_all(data.join("update/stage/Rallypoint.app")).unwrap();
        for f in ["update/download/Rallypoint_0.4.5_aarch64.dmg", "update/download/Rallypoint_0.4.6_aarch64.dmg", "update/apply-update.sh", "update/stage/Rallypoint.app/x", "update/apply-update.log"] {
            std::fs::write(data.join(f), "x").unwrap();
        }
        let n = cleanup(&data, "0.4.5");
        assert_eq!(n, 3, "the update folder's three entries: the old installer, the script and the staged app");
        assert!(data.join("update/apply-update.log").exists(), "the log of the last update stays");
        assert!(!data.join("update/download/Rallypoint_0.4.5_aarch64.dmg").exists() && !data.join("update/stage").exists() && !data.join("update/apply-update.sh").exists());
        assert!(data.join("update/download/Rallypoint_0.4.6_aarch64.dmg").exists(), "an update that has not been installed yet is kept");
        assert_eq!(cleanup(&t.join("none"), "0.4.5"), 0);
        std::fs::remove_dir_all(t).unwrap();
    }
}
