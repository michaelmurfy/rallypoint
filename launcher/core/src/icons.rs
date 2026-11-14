//! Sharp command-bar icons. GenTool publishes the high-resolution Zero Hour control bar. Setup downloads it
//! without asking and Rallypoint loads it with the game, on macOS and Windows.

use crate::extract;
use crate::mods;
use crate::proc::quiet;
use std::path::Path;
use std::process::Command;

pub const FOLDER: &str = "Control Bar HD";
pub const URL: &str = "https://gentool.net/download/controlbar/1.1/ControlBarHDZH_v1.1_English.zip";
/// SHA-256 of that zip (ControlBarHDZH v1.1 English, 7,494,908 bytes).
pub const SHA256: &str = "fd055a7c122866d50100f87be62a55e3457024e68d50f4619fbbd679bdd8b1db";

/// Control Bar Pro (GenTool, MIT): a redrawn command bar laid out for one resolution. It goes on top of the HD icons.
pub const PRO_FOLDER: &str = "Control Bar Pro";

/// One Control Bar Pro v1.2 download: the resolution it is drawn for, its URL and the SHA-256 of the zip.
pub struct ProVariant {
    pub width: u32,
    pub height: u32,
    pub url: &'static str,
    pub sha256: &'static str,
}

/// Smallest first. The 1280x720 and 1600x900 zips differ only inside the `.big` files.
pub const PRO_VARIANTS: [ProVariant; 5] = [
    ProVariant { width: 1280, height: 720, url: "https://www.gentool.net/download/controlbarpro/1.2/ControlBarProZH_v1.2_1280x720.zip", sha256: "a49570d9db4661bb6cb5082d15a9708f8e47a36c63a3d732f3cc3a26b31b2946" },
    ProVariant { width: 1600, height: 900, url: "https://www.gentool.net/download/controlbarpro/1.2/ControlBarProZH_v1.2_1600x900.zip", sha256: "f97428cf730c03fe4a5d9ed9c7e68193d0599c71e2d48e7f14ebcd2721ed03d6" },
    ProVariant { width: 1920, height: 1080, url: "https://www.gentool.net/download/controlbarpro/1.2/ControlBarProZH_v1.2_1920x1080.zip", sha256: "5b987cb864aaf1c55ac8f4cf8dbf28a9473e1233bbe34f05316bfee5b7eb0836" },
    ProVariant { width: 2560, height: 1440, url: "https://www.gentool.net/download/controlbarpro/1.2/ControlBarProZH_v1.2_2560x1440.zip", sha256: "e5021b5e76a7646ae027d3104b8da5e2fcb104007fb82213d8a84205a2e1ede9" },
    ProVariant { width: 3840, height: 2160, url: "https://www.gentool.net/download/controlbarpro/1.2/ControlBarProZH_v1.2_3840x2160.zip", sha256: "66afbd045a09b514578bc696c670e6b58654db1740c44b9221db191e1273c5e7" },
];

/// The largest preset that fits inside the game window (the smallest when none does).
pub fn pro_variant(width: u32, height: u32) -> &'static ProVariant {
    PRO_VARIANTS.iter().rev().find(|v| v.width <= width && v.height <= height).unwrap_or(&PRO_VARIANTS[0])
}

/// Folders Rallypoint installs itself: they are not listed as the player's own mods.
pub fn is_bundled(folder: &str) -> bool {
    folder == FOLDER || folder == PRO_FOLDER
}

pub fn install(mods_dir: &Path, work: &Path) -> Result<mods::Installed, String> {
    fetch_and_install(mods_dir, work, URL, SHA256, FOLDER)
}

/// A note inside the Pro folder saying which download it holds (the zip's SHA-256), so choosing Pro again after a spell
/// on the sharp icons does not fetch 40 MB that are already there.
const PRO_MARK: &str = "rallypoint-variant.txt";

/// Whether the Pro folder already holds this size.
pub fn pro_installed(mods_dir: &Path, v: &ProVariant) -> bool {
    let dir = mods_dir.join(PRO_FOLDER);
    let has_big = std::fs::read_dir(&dir).map(|d| d.flatten().any(|e| e.path().extension().is_some_and(|x| x.eq_ignore_ascii_case("big")))).unwrap_or(false);
    has_big && std::fs::read_to_string(dir.join(PRO_MARK)).is_ok_and(|m| m.trim() == v.sha256)
}

/// Install Control Bar Pro for a window of this size, replacing any other size. Nothing is downloaded when that size is
/// already installed.
pub fn install_pro(mods_dir: &Path, work: &Path, width: u32, height: u32) -> Result<(), String> {
    let v = pro_variant(width, height);
    if pro_installed(mods_dir, v) {
        return Ok(());
    }
    fetch_and_install(mods_dir, work, v.url, v.sha256, PRO_FOLDER)?;
    // Without the note the next switch downloads again, which is only slower: not worth failing the install over.
    let _ = std::fs::write(mods_dir.join(PRO_FOLDER).join(PRO_MARK), v.sha256);
    Ok(())
}

fn fetch_and_install(mods_dir: &Path, work: &Path, url: &str, sha256: &str, folder: &str) -> Result<mods::Installed, String> {
    std::fs::create_dir_all(work).map_err(|e| e.to_string())?;
    let zip = work.join("control-bar.zip");
    let mut child = quiet(&mut Command::new("curl"))
        .args(["-fsSL", "--proto", "=https", "--proto-redir", "=https", "--max-time", "600", "-o"])
        .arg(&zip)
        .arg(url)
        .spawn()
        .map_err(|e| format!("curl isn't available ({e})"))?;
    let status = child.wait().map_err(|e| e.to_string())?;
    if !status.success() {
        let _ = std::fs::remove_file(&zip);
        return Err("couldn't download the sharp icons".into());
    }
    let got = extract::hex(&extract::sha256_file(&zip).map_err(|e| e.to_string())?);
    if got != sha256 {
        let _ = std::fs::remove_file(&zip);
        return Err("the icon download didn't match the expected checksum".into());
    }
    mods::install(&zip, mods_dir, folder)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn icon_pack_comes_from_gentool() {
        assert!(URL.starts_with("https://gentool.net/download/controlbar/"));
        assert_eq!(SHA256.len(), 64);
        assert_eq!(mods::sanitize_folder(FOLDER).as_deref(), Some(FOLDER));
    }

    #[test]
    fn pro_picks_the_largest_preset_that_fits() {
        assert_eq!(pro_variant(3840, 2160).width, 3840);
        assert_eq!(pro_variant(3440, 1440).width, 2560); // ultrawide: the height decides
        assert_eq!(pro_variant(1920, 1200).height, 1080);
        assert_eq!(pro_variant(800, 600).width, 1280);
        for v in &PRO_VARIANTS {
            assert!(v.url.starts_with("https://www.gentool.net/download/controlbarpro/1.2/"));
            assert_eq!(v.sha256.len(), 64);
        }
        assert!(is_bundled(FOLDER) && is_bundled(PRO_FOLDER) && !is_bundled("Shockwave"));
    }

    #[test]
    fn pro_is_only_downloaded_when_that_size_is_not_there() {
        let mods = std::env::temp_dir().join(format!("rp-pro-{}", std::process::id()));
        let dir = mods.join(PRO_FOLDER);
        let _ = std::fs::remove_dir_all(&mods);
        std::fs::create_dir_all(&dir).unwrap();
        let (small, big) = (pro_variant(1280, 720), pro_variant(3840, 2160));
        assert!(!pro_installed(&mods, small)); // an empty folder
        std::fs::write(dir.join("340_ControlBarProZH.big"), b"BIGF").unwrap();
        assert!(!pro_installed(&mods, small)); // installed by an older version: no note, so it is fetched once more
        std::fs::write(dir.join(PRO_MARK), format!("{}\n", small.sha256)).unwrap();
        assert!(pro_installed(&mods, small) && !pro_installed(&mods, big)); // another size still downloads
        std::fs::remove_file(dir.join("340_ControlBarProZH.big")).unwrap();
        assert!(!pro_installed(&mods, small)); // the note alone is not an install
        let _ = std::fs::remove_dir_all(&mods);
    }
}
