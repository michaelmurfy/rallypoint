//! Launcher settings. The serial is a credential: stored with 0600 perms on unix, never logged, only shown masked.
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
}

impl Config {
    pub fn load(path: &Path) -> Config {
        std::fs::read_to_string(path).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
    }

    pub fn save(&self, path: &Path) -> std::io::Result<()> {
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir)?;
        }
        std::fs::write(path, serde_json::to_string_pretty(self)?)?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o600))?;
        }
        Ok(())
    }

    /// `****-****-****-A5E3`: last four characters only.
    pub fn masked_serial(&self) -> String {
        let n = self.serial.chars().count();
        if n <= 4 {
            return "*".repeat(n);
        }
        let tail: String = self.serial.chars().skip(n - 4).collect();
        format!("****-****-****-{tail}")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip_and_mask() {
        let p = std::env::temp_dir().join(format!("gen-cfg-{}/config.json", std::process::id()));
        assert!(Config::load(&p).serial.is_empty()); // missing file -> default
        let c = Config { server: "play.example:7777".into(), serial: "55BC-E547-64E4-5A53".into(), ..Default::default() };
        c.save(&p).unwrap();
        let back = Config::load(&p);
        assert_eq!((back.server.as_str(), back.serial.as_str()), ("play.example:7777", "55BC-E547-64E4-5A53"));
        assert_eq!(back.masked_serial(), "****-****-****-5A53");
        assert_eq!(Config { serial: "ab".into(), ..Default::default() }.masked_serial(), "**");
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            assert_eq!(std::fs::metadata(&p).unwrap().permissions().mode() & 0o777, 0o600);
        }
        std::fs::remove_dir_all(p.parent().unwrap()).unwrap();
    }
}
