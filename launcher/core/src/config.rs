//! Launcher settings. A serial is a credential the user never sees: stored with 0600 perms on unix and never logged.
//! A room password is stored only when the player asks for it to be remembered with a saved server.
//! Each server issues its own, and one is only ever sent to the server that issued it.
use serde::{Deserialize, Serialize};
use std::path::Path;

/// Longest name the game's lobby takes (`g_lanPlayerNameLength`).
pub const PLAYER_NAME_MAX: usize = 12;

/// A lobby name the game can show: printable ASCII only (the game reads it as plain bytes), trimmed, at most 12 characters.
pub fn clean_player_name(s: &str) -> String {
    let kept: String = s.chars().filter(|c| c.is_ascii() && !c.is_ascii_control()).collect();
    kept.trim().chars().take(PLAYER_NAME_MAX).collect::<String>().trim().to_string()
}

/// This computer's name as a default lobby name (`Michaels-MacBook-Pro.local` becomes `Michaels-MacBook-Pro`).
pub fn machine_name() -> String {
    let raw = std::env::var("COMPUTERNAME")
        .ok()
        .or_else(|| std::process::Command::new("hostname").output().ok().filter(|o| o.status.success()).map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string()))
        .unwrap_or_default();
    let name = clean_player_name(raw.split('.').next().unwrap_or(""));
    if name.is_empty() { "Player".into() } else { name }
}

#[derive(Default, Clone, Serialize, Deserialize)]
pub struct Config {
    /// Relay server `host:port`.
    pub server: String,
    /// The serial each server issued, keyed by `host:port` as typed, lower case. A serial sent to a different
    /// server would let its operator join the first server as this player.
    #[serde(default)]
    pub serials: std::collections::BTreeMap<String, String>,
    /// Before serials were kept per server there was only this one; `load` moves it into `serials` and clears it.
    #[serde(default, skip_serializing_if = "String::is_empty")]
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
    /// Record the engine's per-frame sync logs, for finding out why a multiplayer game went out of sync.
    #[serde(default)]
    pub sync_logs: bool,
    /// How far out the camera may zoom (`graphics::FAR_CAMERA` unless the player changed it).
    #[serde(default = "default_camera")]
    pub camera_height: u32,
    /// The name other players see in the game's LAN lobby. Empty means this computer's name.
    #[serde(default)]
    pub player_name: String,
    /// Room passwords the player chose to remember, keyed by `server|room` (lower case). Never sent anywhere but to
    /// that server's own handshake, and never given back to the page: it only learns that one is saved.
    #[serde(default)]
    pub room_passwords: std::collections::BTreeMap<String, String>,
    /// Identity keys of relay servers seen before (hex, keyed by `host:port` as typed, lower case). A server
    /// that later answers with a different key is refused until the player chooses to trust it.
    #[serde(default)]
    pub server_keys: std::collections::BTreeMap<String, String>,
}

fn default_camera() -> u32 {
    crate::graphics::FAR_CAMERA
}

impl Config {
    /// The camera limit to use; a missing or zero value means the recommended one.
    pub fn camera(&self) -> u32 {
        if self.camera_height == 0 { crate::graphics::FAR_CAMERA } else { self.camera_height }
    }

    pub fn load(path: &Path) -> Config {
        let mut c: Config = std::fs::read_to_string(path).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default();
        c.migrate_serial();
        c
    }

    /// The old single serial belongs to the server it was issued by, which is not recorded. It is only kept when the
    /// current server is one whose identity was saved (a server we have signed up with); otherwise it is dropped,
    /// which costs one automatic sign-up, instead of being sent to a server that never issued it.
    fn migrate_serial(&mut self) {
        let old = std::mem::take(&mut self.serial);
        let key = self.server.to_lowercase();
        if !old.is_empty() && !key.is_empty() && self.server_keys.contains_key(&key) {
            self.serials.entry(key).or_insert(old);
        }
    }

    /// The lobby name to start the game with: the one the player set, else this computer's name.
    pub fn lobby_name(&self) -> String {
        let own = clean_player_name(&self.player_name);
        if own.is_empty() { machine_name() } else { own }
    }

    fn room_key(server: &str, room: &str) -> String {
        format!("{}|{}", server.trim().to_lowercase(), room.trim().to_lowercase())
    }

    /// The remembered password for this server and room, if the player saved one.
    pub fn room_password(&self, server: &str, room: &str) -> Option<&str> {
        self.room_passwords.get(&Self::room_key(server, room)).map(String::as_str).filter(|p| !p.is_empty())
    }

    pub fn set_room_password(&mut self, server: &str, room: &str, password: &str) {
        let key = Self::room_key(server, room);
        if password.is_empty() {
            self.room_passwords.remove(&key);
        } else {
            self.room_passwords.insert(key, password.to_string());
        }
    }

    /// The serial to join `server` with; empty means sign up first.
    pub fn serial_for(&self, server: &str) -> &str {
        self.serials.get(&server.to_lowercase()).map(String::as_str).unwrap_or("")
    }

    pub fn set_serial(&mut self, server: &str, serial: String) {
        self.serials.insert(server.to_lowercase(), serial);
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
        assert!(Config::load(&p).serial_for("play.example:7777").is_empty()); // missing file -> default
        let mut c = Config { server: "play.example:7777".into(), ..Default::default() };
        c.set_serial("Play.Example:7777", "55BC-E547-64E4-5A53".into());
        c.save(&p).unwrap();
        let back = Config::load(&p);
        assert_eq!((back.server.as_str(), back.serial_for("PLAY.example:7777")), ("play.example:7777", "55BC-E547-64E4-5A53"));
        assert_eq!(back.serial_for("other.example:7777"), "", "a serial is only for the server that issued it");
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

    #[test]
    fn the_old_single_serial_moves_only_to_a_server_we_know() {
        let dir = std::env::temp_dir().join(format!("gen-cfg-mig-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let p = dir.join("config.json");
        std::fs::create_dir_all(&dir).unwrap();
        // the current server has a saved identity: the serial is kept for it, and the legacy field disappears on save
        std::fs::write(&p, r#"{"server":"A.example:7777","serial":"AAAA-AAAA","server_keys":{"a.example:7777":"00"}}"#).unwrap();
        let c = Config::load(&p);
        assert_eq!(c.serial_for("a.example:7777"), "AAAA-AAAA");
        c.save(&p).unwrap();
        assert!(!std::fs::read_to_string(&p).unwrap().contains("\"serial\":"));
        // a server we never signed up with must not receive it
        std::fs::write(&p, r#"{"server":"b.example:7777","serial":"AAAA-AAAA"}"#).unwrap();
        let c = Config::load(&p);
        assert_eq!((c.serial_for("b.example:7777"), c.serial.as_str()), ("", ""));
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn remembers_a_room_password_per_server_and_room_only_when_asked() {
        let mut c = Config::default();
        assert_eq!(c.room_password("play.example:7777", "lan"), None);
        c.set_room_password("Play.Example:7777", " LAN ", "hunter2");
        assert_eq!(c.room_password("play.example:7777", "lan"), Some("hunter2"));
        assert_eq!(c.room_password("play.example:7777", "other"), None, "not shared between rooms");
        assert_eq!(c.room_password("other.example:7777", "lan"), None, "nor between servers");
        c.set_room_password("play.example:7777", "lan", "");
        assert_eq!(c.room_password("play.example:7777", "lan"), None, "an empty password forgets it");
    }

    #[test]
    fn lobby_names_are_plain_short_and_default_to_the_computer() {
        assert_eq!(clean_player_name("  Mike \u{1F600}the\tGreat  "), "Mike theGrea", "emoji and tabs are dropped, 12 characters kept");
        assert_eq!(clean_player_name(&"x".repeat(40)).len(), PLAYER_NAME_MAX);
        assert_eq!(clean_player_name("   "), "");
        let mut c = Config::default();
        assert!(!c.lobby_name().is_empty(), "falls back to the computer's name");
        c.player_name = "Col. Mike".into();
        assert_eq!(c.lobby_name(), "Col. Mike");
    }
}
