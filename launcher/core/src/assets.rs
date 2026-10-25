//! Downloading the game files from Steam with `steamcmd`, Valve's own command-line client (fetched on demand).
//! Steam only publishes Windows depots for these games, so we ask for the Windows platform on every OS.
//! The user's Steam password is only held in memory for the job, answered to steamcmd's prompt on stdin
//! (never on a command line), and never logged.
use serde::Serialize;
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::{Arc, Mutex};

pub const GENERALS_APP: u32 = 2229870;
pub const ZERO_HOUR_APP: u32 = 2732960;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Default)]
pub enum Phase {
    #[default]
    Idle,
    Preparing,
    LoggingIn,
    Downloading,
    Done,
    Failed,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub enum Need {
    Password,
    Guard,
}

#[derive(Debug, Clone, Serialize, Default)]
pub struct AssetState {
    pub phase: Phase,
    pub percent: f32,
    pub message: String,
    /// Set while steamcmd is waiting for the user: a password or a Steam Guard code (see `AssetJob::submit`).
    pub need: Option<Need>,
}

/// Where steamcmd and the games live under `root` (the launcher's data directory).
pub fn steamcmd_dir(root: &Path) -> PathBuf {
    root.join("steamcmd")
}
pub fn generals_dir(root: &Path) -> PathBuf {
    root.join("game/Generals")
}
pub fn zero_hour_dir(root: &Path) -> PathBuf {
    root.join("game/ZeroHour")
}

fn steamcmd_exe(dir: &Path) -> PathBuf {
    dir.join(if cfg!(windows) { "steamcmd.exe" } else { "steamcmd.sh" })
}

/// Official installer archive for this OS.
pub fn download_url() -> &'static str {
    if cfg!(target_os = "macos") {
        "https://steamcdn-a.akamaihd.net/client/installer/steamcmd_osx.tar.gz"
    } else if cfg!(windows) {
        "https://steamcdn-a.akamaihd.net/client/installer/steamcmd.zip"
    } else {
        "https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz"
    }
}

/// Arguments for one steamcmd session that logs in once and installs both games.
pub fn steamcmd_args(user: &str, generals: &Path, zero_hour: &Path) -> Vec<String> {
    let s = |x: &str| x.to_string();
    vec![
        s("+@ShutdownOnFailedCommand"), s("1"),
        s("+@sSteamCmdForcePlatformType"), s("windows"),
        s("+login"), user.to_string(),
        s("+force_install_dir"), generals.display().to_string(),
        s("+app_update"), GENERALS_APP.to_string(), s("validate"),
        s("+force_install_dir"), zero_hour.display().to_string(),
        s("+app_update"), ZERO_HOUR_APP.to_string(), s("validate"),
        s("+quit"),
    ]
}

/// What a piece of steamcmd output means.
#[derive(Debug, PartialEq)]
pub enum Seen {
    Prompt(Need),
    Progress(f32),
    Failure(String),
    Nothing,
}

/// A complete line of output.
pub fn scan_line(line: &str) -> Seen {
    let l = line.trim();
    if let Some(i) = l.find("progress: ") {
        if let Some(p) = l[i + 10..].split(|c: char| !c.is_ascii_digit() && c != '.').next().and_then(|n| n.parse::<f32>().ok()) {
            return Seen::Progress(p.clamp(0.0, 100.0));
        }
    }
    let low = l.to_ascii_lowercase();
    if low.contains("invalid password") || low.contains("login failure") {
        return Seen::Failure("Steam rejected the username or password".into());
    }
    if low.contains("no subscription") || low.contains("not owned") {
        return Seen::Failure("This Steam account doesn't own the game".into());
    }
    if low.contains("rate limit exceeded") {
        return Seen::Failure("Steam is rate limiting logins, try again later".into());
    }
    if low.starts_with("error!") {
        return Seen::Failure(l.to_string());
    }
    Seen::Nothing
}

/// The unfinished last line of output: steamcmd prompts without a trailing newline.
pub fn scan_prompt(partial: &str) -> Option<Need> {
    let low = partial.trim_end().to_ascii_lowercase();
    if !low.ends_with(':') {
        return None;
    }
    if low.ends_with("password:") {
        Some(Need::Password)
    } else if low.contains("guard code") || low.contains("two-factor code") || low.contains("authentication code") {
        Some(Need::Guard)
    } else {
        None
    }
}

pub struct AssetJob {
    state: Arc<Mutex<AssetState>>,
    stdin: Arc<Mutex<Option<ChildStdin>>>,
    child: Arc<Mutex<Option<Child>>>,
}

impl AssetJob {
    /// Starts the download in the background. `password` may be empty if steamcmd already has a cached login.
    pub fn start(root: &Path, user: &str, password: String) -> io::Result<AssetJob> {
        // A name starting with + or - would be read by steamcmd as a command or option.
        if user.trim().is_empty() || user.starts_with(['+', '-']) || user.chars().any(|c| c.is_control() || c.is_whitespace()) {
            return Err(io::Error::other("enter your Steam account name"));
        }
        let job = AssetJob { state: Arc::default(), stdin: Arc::default(), child: Arc::default() };
        job.set(Phase::Preparing, 0.0, "Getting Steam's downloader…", None);
        let (state, stdin, child, root, user) = (job.state.clone(), job.stdin.clone(), job.child.clone(), root.to_path_buf(), user.to_string());
        std::thread::spawn(move || {
            let fail = |m: String| *state.lock().unwrap() = AssetState { phase: Phase::Failed, percent: 0.0, message: m, need: None };
            let dir = steamcmd_dir(&root);
            if let Err(e) = ensure_steamcmd(&dir) {
                return fail(format!("Couldn't get steamcmd: {e}"));
            }
            let (zh, gen) = (zero_hour_dir(&root), generals_dir(&root));
            if std::fs::create_dir_all(&zh).and(std::fs::create_dir_all(&gen)).is_err() {
                return fail("Couldn't create the game folder".into());
            }
            let mut cmd = if cfg!(windows) { Command::new(steamcmd_exe(&dir)) } else { let mut c = Command::new("sh"); c.arg(steamcmd_exe(&dir)); c };
            let spawned = cmd.args(steamcmd_args(&user, &gen, &zh)).current_dir(&dir).stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::null()).spawn();
            let mut c = match spawned {
                Ok(c) => c,
                Err(e) => return fail(format!("Couldn't start steamcmd ({e}). On Apple Silicon it needs Rosetta: softwareupdate --install-rosetta")),
            };
            let mut out = c.stdout.take().unwrap();
            *stdin.lock().unwrap() = c.stdin.take();
            *child.lock().unwrap() = Some(c);
            *state.lock().unwrap() = AssetState { phase: Phase::LoggingIn, percent: 0.0, message: "Signing in to Steam…".into(), need: None };

            let (mut pending, mut buf, mut password, mut failure) = (String::new(), [0u8; 4096], Some(password).filter(|p| !p.is_empty()), None::<String>);
            loop {
                let n = match out.read(&mut buf) {
                    Ok(0) | Err(_) => break,
                    Ok(n) => n,
                };
                pending.push_str(&String::from_utf8_lossy(&buf[..n]));
                // complete lines first, then look at the unfinished tail for a prompt
                while let Some(i) = pending.find(['\n', '\r']) {
                    let line: String = pending.drain(..=i).collect();
                    match scan_line(&line) {
                        Seen::Progress(p) => *state.lock().unwrap() = AssetState { phase: Phase::Downloading, percent: p, message: "Downloading game files…".into(), need: None },
                        Seen::Failure(m) => failure = Some(m),
                        _ => {}
                    }
                }
                match scan_prompt(&pending) {
                    Some(Need::Password) if password.is_some() => {
                        write_line(&stdin, &password.take().unwrap());
                        pending.clear();
                    }
                    Some(need) => {
                        *state.lock().unwrap() = AssetState { phase: Phase::LoggingIn, percent: 0.0, message: if need == Need::Guard { "Enter your Steam Guard code".into() } else { "Enter your Steam password".into() }, need: Some(need) };
                        pending.clear();
                    }
                    None => {}
                }
            }
            if let Some(mut c) = child.lock().unwrap().take() {
                let _ = c.wait();
            }
            *stdin.lock().unwrap() = None;
            let done = zh.join("INIZH.big").is_file();
            *state.lock().unwrap() = if done {
                AssetState { phase: Phase::Done, percent: 100.0, message: "Game files are ready".into(), need: None }
            } else {
                AssetState { phase: Phase::Failed, percent: 0.0, message: failure.unwrap_or_else(|| "Steam didn't finish the download".into()), need: None }
            };
        });
        Ok(job)
    }

    fn set(&self, phase: Phase, percent: f32, message: &str, need: Option<Need>) {
        *self.state.lock().unwrap() = AssetState { phase, percent, message: message.into(), need };
    }

    pub fn state(&self) -> AssetState {
        self.state.lock().unwrap().clone()
    }

    /// Answer steamcmd's current prompt (the password or the Steam Guard code).
    pub fn submit(&self, text: &str) {
        if self.state.lock().unwrap().need.take().is_some() {
            write_line(&self.stdin, text.trim());
        }
    }

    pub fn cancel(&self) {
        if let Some(c) = self.child.lock().unwrap().as_mut() {
            let _ = c.kill();
        }
    }
}

impl Drop for AssetJob {
    fn drop(&mut self) {
        self.cancel();
    }
}

fn write_line(stdin: &Arc<Mutex<Option<ChildStdin>>>, text: &str) {
    if let Some(s) = stdin.lock().unwrap().as_mut() {
        let _ = writeln!(s, "{text}");
        let _ = s.flush();
    }
}

/// Download and unpack steamcmd if it isn't there yet. Uses the system `curl` and `tar` (both ship with
/// macOS, Windows 10+ and mainstream Linux), over HTTPS from Valve's CDN.
fn ensure_steamcmd(dir: &Path) -> io::Result<()> {
    if steamcmd_exe(dir).is_file() {
        return Ok(());
    }
    std::fs::create_dir_all(dir)?;
    let archive = dir.join("steamcmd-installer");
    let ok = |s: std::process::ExitStatus| if s.success() { Ok(()) } else { Err(io::Error::other("download or unpack failed")) };
    ok(Command::new("curl").args(["-fsSL", "--proto", "=https", "--proto-redir", "=https", "-o"]).arg(&archive).arg(download_url()).status()?)?;
    ok(Command::new("tar").arg("-xf").arg(&archive).arg("-C").arg(dir).status()?)?;
    let _ = std::fs::remove_file(&archive);
    if steamcmd_exe(dir).is_file() { Ok(()) } else { Err(io::Error::other("steamcmd missing after unpack")) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn args_install_both_games_for_windows_platform() {
        let a = steamcmd_args("alice", Path::new("/g/Generals"), Path::new("/g/ZeroHour"));
        let s = a.join(" ");
        assert!(s.contains("+@sSteamCmdForcePlatformType windows"));
        assert!(s.contains("+login alice +force_install_dir /g/Generals +app_update 2229870 validate"));
        assert!(s.contains("+force_install_dir /g/ZeroHour +app_update 2732960 validate +quit"));
        assert!(!s.to_lowercase().contains("password"), "never put a password on the command line");
    }

    #[test]
    fn reads_progress_and_failures() {
        let l = " Update state (0x61) downloading, progress: 12.34 (123456 / 1000000)";
        assert_eq!(scan_line(l), Seen::Progress(12.34));
        assert_eq!(scan_line("Update state (0x5) verifying install, progress: 100.00 (1 / 1)"), Seen::Progress(100.0));
        assert!(matches!(scan_line("FAILED (Invalid Password)"), Seen::Failure(m) if m.contains("password")));
        assert!(matches!(scan_line("ERROR! Failed to install app '2732960' (No subscription)"), Seen::Failure(m) if m.contains("doesn't own")));
        assert!(matches!(scan_line("ERROR! something else"), Seen::Failure(_)));
        assert_eq!(scan_line("Logging in user 'alice' to Steam Public...OK"), Seen::Nothing);
    }

    #[test]
    fn detects_prompts_without_newlines() {
        assert_eq!(scan_prompt("password: "), Some(Need::Password));
        assert_eq!(scan_prompt("Logging in user 'a' to Steam Public...\npassword:"), Some(Need::Password));
        assert_eq!(scan_prompt("This computer has not been authenticated for your account using Steam Guard.\nPlease check your email for the message from Steam, and enter the Steam Guard\n code from that message.\nYou can also enter this code at any time using 'set_steam_guard_code'\n    at the console.\nSteam Guard code:"), Some(Need::Guard));
        assert_eq!(scan_prompt("Two-factor code:"), Some(Need::Guard));
        assert_eq!(scan_prompt("Update state (0x61) downloading"), None);
        assert_eq!(scan_prompt("Steam>"), None);
    }

    #[test]
    fn rejects_bad_account_names_before_running_anything() {
        for bad in ["", "  ", "a b", "x\ny", "+quit", "-x", "+runscript"] {
            assert!(AssetJob::start(Path::new("/nonexistent"), bad, String::new()).is_err(), "{bad:?}");
        }
    }

    #[test]
    fn download_url_is_https_from_valves_cdn() {
        assert!(download_url().starts_with("https://steamcdn-a.akamaihd.net/client/installer/"));
    }
}
