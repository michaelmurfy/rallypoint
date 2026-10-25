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

pub fn install(mods_dir: &Path, work: &Path) -> Result<mods::Installed, String> {
    std::fs::create_dir_all(work).map_err(|e| e.to_string())?;
    let zip = work.join("control-bar-hd.zip");
    let mut child = quiet(&mut Command::new("curl"))
        .args(["-fsSL", "--proto", "=https", "--proto-redir", "=https", "--max-time", "600", "-o"])
        .arg(&zip)
        .arg(URL)
        .spawn()
        .map_err(|e| format!("curl isn't available ({e})"))?;
    let status = child.wait().map_err(|e| e.to_string())?;
    if !status.success() {
        let _ = std::fs::remove_file(&zip);
        return Err("couldn't download the sharp icons".into());
    }
    let got = extract::hex(&extract::sha256_file(&zip).map_err(|e| e.to_string())?);
    if got != SHA256 {
        let _ = std::fs::remove_file(&zip);
        return Err("the icon download didn't match the expected checksum".into());
    }
    mods::install(&zip, mods_dir, FOLDER)
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
}
