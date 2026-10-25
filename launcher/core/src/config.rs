//! Launcher settings. The serial is a credential the user never sees: stored with 0600 perms on unix and never logged.
use serde::{Deserialize, Serialize};
use std::path::Path;

#[derive(Default, Clone, Serialize, Deserialize)]
pub struct Config {
    /// Relay server `host:port`.
    pub server: String,
    pub serial: String,
    /// Game data folder (the one with the `.big` files); empty = use whatever Steam detection finds.
    #[serde(default)]
    pub game_dir: String,
    /// Room to join (remembered; the room password is never stored).
    #[serde(default)]
    pub room: String,
    /// Steam account name for downloading game files (the password is never stored).
    #[serde(default)]
    pub steam_user: String,
    /// The first-run setup wizard has been completed (or skipped).
    #[serde(default)]
    pub setup_done: bool,
    /// Don't ask GitHub for a newer release when the launcher starts.
    #[serde(default)]
    pub skip_update_check: bool,
}

impl Config {
    pub fn load(path: &Path) -> Config {
        std::fs::read_to_string(path).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
    }

    pub fn save(&self, path: &Path) -> std::io::Result<()> {
        use std::io::Write;
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir)?;
        }
        let mut opts = std::fs::OpenOptions::new();
        opts.write(true).create(true).truncate(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            opts.mode(0o600); // owner-only from the moment the file exists
        }
        let mut f = opts.open(path)?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            f.set_permissions(std::fs::Permissions::from_mode(0o600))?; // also tightens a pre-existing file
        }
        f.write_all(serde_json::to_string_pretty(self)?.as_bytes())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip_and_permissions() {
        let p = std::env::temp_dir().join(format!("gen-cfg-{}/config.json", std::process::id()));
        assert!(Config::load(&p).serial.is_empty()); // missing file -> default
        let c = Config { server: "play.example:7777".into(), serial: "55BC-E547-64E4-5A53".into(), ..Default::default() };
        c.save(&p).unwrap();
        let back = Config::load(&p);
        assert_eq!((back.server.as_str(), back.serial.as_str()), ("play.example:7777", "55BC-E547-64E4-5A53"));
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            assert_eq!(std::fs::metadata(&p).unwrap().permissions().mode() & 0o777, 0o600);
        }
        // an existing world-readable file is tightened on save
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::set_permissions(&p, std::fs::Permissions::from_mode(0o644)).unwrap();
            c.save(&p).unwrap();
            assert_eq!(std::fs::metadata(&p).unwrap().permissions().mode() & 0o777, 0o600);
        }
        std::fs::remove_dir_all(p.parent().unwrap()).unwrap();
    }
}
