//! Downloading the game files from Steam with `steamcmd`, Valve's own command-line client (fetched on demand).
//! Steam only publishes Windows depots for these games, so we ask for the Windows platform on every OS.
//!
//! Sign-in never relies on steamcmd's interactive prompts (they depend on terminal behaviour). Instead it is a
//! short non-interactive run whose output we classify; if Steam wants a Steam Guard code we ask the user and run it
//! again with `set_steam_guard_code`. Secrets only ever live in a 0600 script file that is deleted as soon as that
//! short run ends; the long download then reuses steamcmd's cached login and carries no secrets.
use serde::Serialize;
use std::io::{self, Read, Write};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::time::{Duration, Instant};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
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
    /// Apple Silicon without Rosetta 2: steamcmd is an Intel program. Answer with "install" to install it.
    Rosetta,
}

#[derive(Debug, Clone, Serialize, Default)]
pub struct AssetState {
    pub phase: Phase,
    pub percent: f32,
    pub message: String,
    /// Set while steamcmd is waiting for the user: a password or a Steam Guard code (see `AssetJob::submit`).
    pub need: Option<Need>,
    /// Recent steamcmd output with secrets removed, for the "details" panel and bug reports.
    pub log: String,
}

/// Rosetta 2 (Apple's Intel translation layer), which Valve's macOS steamcmd needs on Apple Silicon.
pub struct Rosetta {
    pub missing: Box<dyn Fn() -> bool + Send>,
    pub install: Box<dyn Fn() -> Result<(), String> + Send>,
}

impl Rosetta {
    pub fn system() -> Rosetta {
        Rosetta { missing: Box::new(rosetta_missing), install: Box::new(install_rosetta) }
    }

    /// For platforms and tests where it never matters.
    pub fn not_needed() -> Rosetta {
        Rosetta { missing: Box::new(|| false), install: Box::new(|| Ok(())) }
    }
}

fn rosetta_missing() -> bool {
    cfg!(all(target_os = "macos", target_arch = "aarch64")) && !Path::new("/Library/Apple/usr/libexec/oah/libRosettaRuntime").exists()
}

/// Asks macOS for administrator rights with its own password dialog, then runs Apple's installer. The user has
/// just clicked "Install Rosetta", which is the consent for accepting Apple's license for it.
fn install_rosetta() -> Result<(), String> {
    let out = Command::new("osascript")
        .args(["-e", "do shell script \"/usr/sbin/softwareupdate --install-rosetta --agree-to-license\" with administrator privileges"])
        .output()
        .map_err(|e| format!("Couldn't ask macOS to install Rosetta: {e}"))?;
    if out.status.success() && !rosetta_missing() {
        Ok(())
    } else {
        Err("Rosetta wasn't installed. In Terminal run: softwareupdate --install-rosetta --agree-to-license".into())
    }
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

/// Arguments for the download run. It signs in with the login steamcmd cached during sign-in, so there is no secret here.
pub fn steamcmd_args(user: &str, generals: &Path, zero_hour: &Path) -> Vec<String> {
    let s = |x: &str| x.to_string();
    vec![
        s("+@ShutdownOnFailedCommand"), s("1"),
        s("+@NoPromptForPassword"), s("1"),
        s("+@sSteamCmdForcePlatformType"), s("windows"),
        s("+login"), user.to_string(),
        s("+force_install_dir"), generals.display().to_string(),
        s("+app_update"), GENERALS_APP.to_string(), s("validate"),
        s("+force_install_dir"), zero_hour.display().to_string(),
        s("+app_update"), ZERO_HOUR_APP.to_string(), s("validate"),
        s("+quit"),
    ]
}

/// Contents of the one-off sign-in script. Everything interpolated is validated first, because the file is
/// parsed as commands. Empty `password` = rely on a cached login; empty `code` = no Steam Guard code yet.
pub fn signin_script(user: &str, password: &str, code: &str) -> Result<String, String> {
    if user.is_empty() || user.starts_with(['+', '-', '@']) || user.chars().any(|c| c.is_control() || c.is_whitespace() || c == '"') {
        return Err("That Steam account name isn't valid".into());
    }
    if password.chars().any(|c| c.is_control() || c == '"') {
        return Err("Passwords containing a double quote aren't supported here; sign in once with steamcmd in a terminal instead".into());
    }
    if !code.is_empty() && !(code.len() >= 4 && code.len() <= 10 && code.chars().all(|c| c.is_ascii_alphanumeric())) {
        return Err("A Steam Guard code is letters and numbers only".into());
    }
    let mut s = String::from("@ShutdownOnFailedCommand 1\n@NoPromptForPassword 1\n@sSteamCmdForcePlatformType windows\n");
    if !code.is_empty() {
        s += &format!("set_steam_guard_code {code}\n");
    }
    s += &if password.is_empty() { format!("login {user}\n") } else { format!("login {user} \"{password}\"\n") };
    s += "quit\n";
    Ok(s)
}

/// How a sign-in run ended.
#[derive(Debug, PartialEq)]
pub enum SignIn {
    Ok,
    NeedPassword,
    /// steamcmd couldn't run: an Intel binary on Apple Silicon without Rosetta.
    NeedRosetta,
    /// Steam wants a Steam Guard code; `wrong` = we sent one and it was refused.
    NeedGuard { wrong: bool },
    Failed(String),
}

/// Classify the full output of a sign-in run (stdin was closed, so any prompt shows up as text followed by exit).
pub fn classify_signin(output: &str, gave_password: bool) -> SignIn {
    let low = output.to_ascii_lowercase();
    if low.contains("waiting for user info...ok") || low.contains("logged in ok") {
        return SignIn::Ok;
    }
    if low.contains("bad cpu type in executable") {
        return SignIn::NeedRosetta;
    }
    if low.contains("rate limit exceeded") {
        return SignIn::Failed("Steam is rate limiting logins, try again later".into());
    }
    if low.contains("code mismatch") || low.contains("invalid login auth code") || low.contains("invalidloginauthcode") || low.contains("twofactorcodemismatch") {
        return SignIn::NeedGuard { wrong: true };
    }
    if low.contains("invalid password") || low.contains("invalidpassword") {
        return SignIn::Failed("Steam rejected the username or password".into());
    }
    if low.contains("steam guard") || low.contains("two-factor") || low.contains("logon denied") || low.contains("authentication code") {
        return SignIn::NeedGuard { wrong: false };
    }
    if !gave_password && (low.contains("password:") || low.contains("no cached credentials") || low.contains("login failure")) {
        return SignIn::NeedPassword;
    }
    SignIn::Failed(output.lines().rev().map(str::trim).find(|l| !l.is_empty()).map(|l| format!("Steam said: {l}")).unwrap_or_else(|| "Steam didn't accept the sign-in".into()))
}

pub fn scrub(text: &str, secrets: &[&str]) -> String {
    secrets.iter().filter(|s| !s.is_empty()).fold(text.to_string(), |t, s| t.replace(s, "***"))
}

/// What a line of download output means.
#[derive(Debug, PartialEq)]
pub enum Seen {
    Progress(f32),
    /// steamcmd is waiting for the user to approve the sign-in in the Steam Mobile app (no code to type).
    ApproveOnPhone,
    Failure(String),
    Nothing,
}

pub fn scan_line(line: &str) -> Seen {
    let l = line.trim();
    if let Some(i) = l.find("progress: ") {
        if let Some(p) = l[i + 10..].split(|c: char| !c.is_ascii_digit() && c != '.').next().and_then(|n| n.parse::<f32>().ok()) {
            return Seen::Progress(p.clamp(0.0, 100.0));
        }
    }
    let low = l.to_ascii_lowercase();
    if low.contains("confirm the login in the steam mobile app") || low.contains("waiting for confirmation") {
        return Seen::ApproveOnPhone;
    }
    if low.contains("no subscription") || low.contains("not owned") {
        return Seen::Failure("This Steam account doesn't own the game".into());
    }
    if low.contains("rate limit exceeded") {
        return Seen::Failure("Steam is rate limiting logins, try again later".into());
    }
    if low.contains("invalid password") || low.contains("login failure") {
        return Seen::Failure("Steam signed you out; try again".into());
    }
    if low.starts_with("error!") {
        return Seen::Failure(l.to_string());
    }
    Seen::Nothing
}

type Shared<T> = Arc<Mutex<T>>;

pub struct AssetJob {
    state: Shared<AssetState>,
    input: Mutex<Option<Sender<String>>>,
    child: Shared<Option<Child>>,
    cancelled: Arc<AtomicBool>,
}

struct Ctx {
    state: Shared<AssetState>,
    child: Shared<Option<Child>>,
    cancelled: Arc<AtomicBool>,
    log: Vec<String>,
}

impl Ctx {
    fn set(&mut self, phase: Phase, percent: f32, message: &str, need: Option<Need>) {
        let log = self.log.join("\n");
        *self.state.lock().unwrap() = AssetState { phase, percent, message: message.into(), need, log };
    }

    fn keep(&mut self, text: &str) {
        self.log.extend(text.lines().map(str::trim).filter(|l| !l.is_empty()).map(String::from));
        let n = self.log.len().saturating_sub(40);
        self.log.drain(..n);
    }
}

/// Run `cmd` with no stdin, merging stdout and stderr, calling `on_line` for each line as it arrives.
/// Returns the full output and whether it was stopped (timeout or cancel).
fn run_collect(mut cmd: Command, timeout: Duration, ctx: &Ctx, mut on_line: impl FnMut(&str)) -> io::Result<(String, bool)> {
    let mut child = cmd.stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped()).spawn()?;
    let (tx, rx) = mpsc::channel::<String>();
    let pipe = |r: Box<dyn Read + Send>, tx: Sender<String>| {
        std::thread::spawn(move || {
            let (mut r, mut buf) = (r, [0u8; 4096]);
            while let Ok(n) = r.read(&mut buf) {
                if n == 0 || tx.send(String::from_utf8_lossy(&buf[..n]).into_owned()).is_err() {
                    break;
                }
            }
        });
    };
    pipe(Box::new(child.stdout.take().unwrap()), tx.clone());
    pipe(Box::new(child.stderr.take().unwrap()), tx);
    *ctx.child.lock().unwrap() = Some(child);

    let (mut all, mut partial, start, mut stopped) = (String::new(), String::new(), Instant::now(), false);
    loop {
        match rx.recv_timeout(Duration::from_millis(200)) {
            Ok(chunk) => {
                all.push_str(&chunk);
                partial.push_str(&chunk);
                while let Some(i) = partial.find(['\n', '\r']) {
                    let line: String = partial.drain(..=i).collect();
                    on_line(&line);
                }
            }
            Err(RecvTimeoutError::Timeout) => {
                if ctx.cancelled.load(Ordering::Relaxed) || start.elapsed() > timeout {
                    stopped = true;
                    if let Some(c) = ctx.child.lock().unwrap().as_mut() {
                        let _ = c.kill();
                    }
                }
            }
            Err(RecvTimeoutError::Disconnected) => break, // both pipes closed: the process is done
        }
    }
    if !partial.is_empty() {
        on_line(&partial);
    }
    if let Some(mut c) = ctx.child.lock().unwrap().take() {
        let _ = c.wait();
    }
    Ok((all, stopped))
}

fn steamcmd_command(dir: &Path) -> Command {
    let mut c = if cfg!(windows) { Command::new(steamcmd_exe(dir)) } else { let mut c = Command::new("sh"); c.arg(steamcmd_exe(dir)); c };
    c.current_dir(dir);
    c
}

/// Removes the sign-in script (which may hold the password) when dropped, whatever happens.
struct ScriptFile(PathBuf);
impl Drop for ScriptFile {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.0);
    }
}

fn write_private(path: &Path, contents: &str) -> io::Result<()> {
    let mut o = std::fs::OpenOptions::new();
    o.write(true).create(true).truncate(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        o.mode(0o600);
    }
    o.open(path)?.write_all(contents.as_bytes())
}

fn sign_in(ctx: &mut Ctx, dir: &Path, user: &str, password: &str, code: &str) -> Result<SignIn, String> {
    let script = signin_script(user, password, code)?;
    let path = dir.join("rallypoint-signin.txt");
    write_private(&path, &script).map_err(|e| format!("Couldn't prepare the sign-in: {e}"))?;
    let _guard = ScriptFile(path.clone());
    let mut cmd = steamcmd_command(dir);
    cmd.arg("+runscript").arg(&path);
    let mut phone = false;
    let (out, stopped) = run_collect(cmd, Duration::from_secs(180), ctx, |l| phone |= scan_line(l) == Seen::ApproveOnPhone)
        .map_err(|e| format!("Couldn't start steamcmd ({e}). On Apple Silicon it needs Rosetta: softwareupdate --install-rosetta"))?;
    let out = scrub(&out, &[password, code]);
    ctx.keep(&out);
    if stopped {
        return Ok(SignIn::Failed(if phone { "Timed out waiting for you to approve the sign-in on your phone" } else { "Signing in to Steam timed out" }.into()));
    }
    Ok(classify_signin(&out, !password.is_empty()))
}

fn download(ctx: &mut Ctx, dir: &Path, user: &str, root: &Path) -> Result<(), String> {
    let (gen, zh) = (generals_dir(root), zero_hour_dir(root));
    let mut cmd = steamcmd_command(dir);
    cmd.args(steamcmd_args(user, &gen, &zh));
    let (mut failure, state) = (None::<String>, ctx.state.clone());
    let (out, _) = run_collect(cmd, Duration::from_secs(6 * 3600), ctx, |l| match scan_line(l) {
        Seen::Progress(p) => {
            *state.lock().unwrap() = AssetState { phase: Phase::Downloading, percent: p, message: "Downloading game files…".into(), need: None, log: String::new() };
        }
        Seen::Failure(m) => failure = Some(m),
        _ => {}
    })
    .map_err(|e| format!("Couldn't start steamcmd: {e}"))?;
    ctx.keep(&out);
    if zh.join("INIZH.big").is_file() && gen.join("INI.big").is_file() {
        Ok(())
    } else {
        Err(failure.unwrap_or_else(|| "Steam didn't finish the download".into()))
    }
}

/// Offer to install Rosetta and wait for the user's answer. `false` = stop the job (cancelled or failed).
fn ensure_rosetta(ctx: &mut Ctx, rx: &Receiver<String>, rosetta: &Rosetta) -> bool {
    for _ in 0..2 {
        ctx.set(Phase::LoggingIn, 0.0, "Steam's downloader needs Rosetta 2 on this Mac. Installing it asks for your Mac password.", Some(Need::Rosetta));
        if rx.recv().is_err() {
            return false; // cancelled
        }
        ctx.set(Phase::Preparing, 0.0, "Installing Rosetta… (enter your Mac password when asked)", None);
        if let Err(m) = (rosetta.install)() {
            ctx.set(Phase::Failed, 0.0, &m, None);
            return false;
        }
        if !(rosetta.missing)() {
            return true;
        }
    }
    ctx.set(Phase::Failed, 0.0, "Rosetta still isn't installed. In Terminal run: softwareupdate --install-rosetta --agree-to-license", None);
    false
}

fn worker(root: PathBuf, user: String, mut password: String, rx: Receiver<String>, mut ctx: Ctx, rosetta: Rosetta) {
    let dir = steamcmd_dir(&root);
    let _ = std::fs::remove_file(dir.join("rallypoint-signin.txt")); // stale file from a crash
    if let Err(e) = ensure_steamcmd(&dir) {
        return ctx.set(Phase::Failed, 0.0, &format!("Couldn't get steamcmd: {e}"), None);
    }
    if std::fs::create_dir_all(zero_hour_dir(&root)).and(std::fs::create_dir_all(generals_dir(&root))).is_err() {
        return ctx.set(Phase::Failed, 0.0, "Couldn't create the game folder", None);
    }
    if (rosetta.missing)() && !ensure_rosetta(&mut ctx, &rx, &rosetta) {
        return;
    }
    let mut code = String::new();
    for _attempt in 0..6 {
        ctx.set(Phase::LoggingIn, 0.0, "Signing in to Steam… (if you use the Steam Mobile app, approve the sign-in there)", None);
        let result = match sign_in(&mut ctx, &dir, &user, &password, &code) {
            Ok(r) => r,
            Err(m) => return ctx.set(Phase::Failed, 0.0, &m, None),
        };
        if ctx.cancelled.load(Ordering::Relaxed) {
            return;
        }
        let (need, message) = match result {
            SignIn::Ok => {
                return match download(&mut ctx, &dir, &user, &root) {
                    Ok(()) => ctx.set(Phase::Done, 100.0, "Game files are ready", None),
                    Err(m) => ctx.set(Phase::Failed, 0.0, &m, None),
                };
            }
            SignIn::Failed(m) => return ctx.set(Phase::Failed, 0.0, &m, None),
            SignIn::NeedRosetta => {
                // the up-front check missed it (e.g. Rosetta was removed): offer the install now
                if !ensure_rosetta(&mut ctx, &rx, &rosetta) {
                    return;
                }
                continue;
            }
            SignIn::NeedPassword => (Need::Password, "Enter your Steam password"),
            SignIn::NeedGuard { wrong: false } => (Need::Guard, "Enter your Steam Guard code (check your email or the Steam Mobile app)"),
            SignIn::NeedGuard { wrong: true } => (Need::Guard, "That code didn't work. Enter the newest Steam Guard code"),
        };
        ctx.set(Phase::LoggingIn, 0.0, message, Some(need));
        match rx.recv() {
            Ok(text) if need == Need::Password => password = text,
            Ok(_) if need == Need::Rosetta => {}
            Ok(text) => code = text.trim().to_string(),
            Err(_) => return, // cancelled
        }
    }
    ctx.set(Phase::Failed, 0.0, "Too many attempts. Wait a few minutes and try again", None);
}

impl AssetJob {
    /// Starts the download in the background. `password` may be empty if steamcmd already has a cached login.
    pub fn start(root: &Path, user: &str, password: String) -> io::Result<AssetJob> {
        Self::start_with(root, user, password, Rosetta::system())
    }

    pub fn start_with(root: &Path, user: &str, password: String, rosetta: Rosetta) -> io::Result<AssetJob> {
        // Names are checked again when the sign-in script is built; this gives the user an immediate answer.
        if signin_script(user, "", "").is_err() {
            return Err(io::Error::other("enter your Steam account name"));
        }
        let (state, child, cancelled): (Shared<AssetState>, Shared<Option<Child>>, Arc<AtomicBool>) = Default::default();
        let (tx, rx) = mpsc::channel();
        let mut ctx = Ctx { state: state.clone(), child: child.clone(), cancelled: cancelled.clone(), log: vec![] };
        ctx.set(Phase::Preparing, 0.0, "Getting Steam's downloader…", None);
        let (root, user) = (root.to_path_buf(), user.to_string());
        std::thread::spawn(move || worker(root, user, password, rx, ctx, rosetta));
        Ok(AssetJob { state, input: Mutex::new(Some(tx)), child, cancelled })
    }

    pub fn state(&self) -> AssetState {
        self.state.lock().unwrap().clone()
    }

    /// Answer what the job is waiting for (the password or the Steam Guard code).
    pub fn submit(&self, text: &str) {
        if self.state.lock().unwrap().need.take().is_some() {
            if let Some(tx) = self.input.lock().unwrap().as_ref() {
                let _ = tx.send(text.to_string());
            }
        }
    }

    pub fn cancel(&self) {
        self.cancelled.store(true, Ordering::Relaxed);
        *self.input.lock().unwrap() = None; // wakes a worker that is waiting for input
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
    fn download_args_carry_no_secrets_and_install_both_games() {
        let s = steamcmd_args("alice", Path::new("/g/Generals"), Path::new("/g/ZeroHour")).join(" ");
        assert!(s.contains("+@sSteamCmdForcePlatformType windows"));
        assert!(s.contains("+login alice +force_install_dir /g/Generals +app_update 2229870 validate"));
        assert!(s.contains("+force_install_dir /g/ZeroHour +app_update 2732960 validate +quit"));
        assert!(!s.contains("set_steam_guard_code"));
    }

    #[test]
    fn signin_script_is_validated_because_it_is_parsed_as_commands() {
        assert_eq!(signin_script("alice", "pw d", "AB12C").unwrap(), "@ShutdownOnFailedCommand 1\n@NoPromptForPassword 1\n@sSteamCmdForcePlatformType windows\nset_steam_guard_code AB12C\nlogin alice \"pw d\"\nquit\n");
        assert!(signin_script("alice", "", "").unwrap().contains("\nlogin alice\nquit"), "empty password = cached login");
        for bad_user in ["", "a b", "+quit", "-x", "@x", "a\"b", "a\nquit"] {
            assert!(signin_script(bad_user, "", "").is_err(), "{bad_user:?}");
        }
        assert!(signin_script("a", "p\"q", "").is_err());
        assert!(signin_script("a", "p\nquit", "").is_err(), "no command injection through the password");
        for bad_code in ["12", "12 34", "ab\nquit", "ab\"cd", "waytoolongcode123"] {
            assert!(signin_script("a", "", bad_code).is_err(), "{bad_code:?}");
        }
    }

    #[test]
    fn classifies_signin_output() {
        use SignIn::*;
        assert_eq!(classify_signin("Logging in user 'a' to Steam Public...OK\nWaiting for client config...OK\nWaiting for user info...OK\n", true), Ok);
        let guard = "This computer has not been authenticated for your account using Steam Guard.\nPlease check your email for the message from Steam, and enter the Steam Guard\n code from that message.\nSteam Guard code:\nFAILED (Account Logon Denied)";
        assert_eq!(classify_signin(guard, true), NeedGuard { wrong: false });
        assert_eq!(classify_signin("Two-factor code:FAILED (Two-factor code mismatch)", true), NeedGuard { wrong: true });
        assert_eq!(classify_signin("FAILED login with result code Invalid Login Auth Code", true), NeedGuard { wrong: true });
        assert_eq!(classify_signin("Two-factor code:\n", true), NeedGuard { wrong: false });
        assert!(matches!(classify_signin("FAILED (Invalid Password)", true), Failed(m) if m.contains("password")));
        assert_eq!(classify_signin("password:\nFAILED (No cached credentials)", false), NeedPassword);
        assert!(matches!(classify_signin("ERROR! Rate Limit Exceeded", true), Failed(m) if m.contains("rate")));
        assert!(matches!(classify_signin("something odd\nESync failed\n", true), Failed(m) if m.contains("ESync failed")));
        assert!(matches!(classify_signin("", true), Failed(_)));
        assert_eq!(classify_signin("/Users/a/Library/Application Support/GeneralsZH/steamcmd/steamcmd.sh: line 37: /x/steamcmd: Bad CPU type in executable", false), NeedRosetta);
    }

    #[test]
    fn reads_progress_phone_and_failures() {
        assert_eq!(scan_line(" Update state (0x61) downloading, progress: 12.34 (123456 / 1000000)"), Seen::Progress(12.34));
        assert_eq!(scan_line("Please confirm the login in the Steam Mobile app on your phone."), Seen::ApproveOnPhone);
        assert!(matches!(scan_line("ERROR! Failed to install app '2732960' (No subscription)"), Seen::Failure(m) if m.contains("doesn't own")));
        assert_eq!(scan_line("Logging in user 'alice' to Steam Public...OK"), Seen::Nothing);
    }

    #[test]
    fn scrub_removes_secrets() {
        assert_eq!(scrub("pw hunter2 code 12345 hunter2", &["hunter2", "12345", ""]), "pw *** code *** ***");
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

    /// End to end against a fake steamcmd that behaves like the real one with stdin closed: Steam Guard
    /// accounts are refused until a `set_steam_guard_code` line carries the right code.
    #[cfg(unix)]
    mod fake_steamcmd {
        use super::*;

        const FAKE: &str = r#"#!/bin/sh
# usage: steamcmd.sh +runscript FILE   |   steamcmd.sh +login USER ... +app_update ID validate ...
if [ "$1" = "+runscript" ]; then
  script=$(cat "$2")
  if echo "$script" | grep -q '^login norosetta' && [ ! -f "$(dirname "$0")/rosetta-ok" ]; then
    echo "$0: line 37: steamcmd: Bad CPU type in executable"; exit 126
  fi
  user=$(echo "$script" | sed -n 's/^login \([^ ]*\).*/\1/p')
  pass=$(echo "$script" | sed -n 's/^login [^ ]* "\(.*\)"$/\1/p')
  code=$(echo "$script" | sed -n 's/^set_steam_guard_code //p')
  if [ "$pass" = "bad" ]; then echo "FAILED (Invalid Password)"; exit 5; fi
  if [ "$user" = "guarded" ]; then
    if [ -z "$code" ]; then
      printf 'This computer has not been authenticated for your account using Steam Guard.\nSteam Guard code:'; echo; echo "FAILED (Account Logon Denied)" >&2; exit 5
    fi
    if [ "$code" != "12345" ]; then echo "FAILED (Two-factor code mismatch)"; exit 5; fi
  fi
  if [ "$user" = "cached" ] && [ -n "$pass" ]; then echo "unexpected password"; exit 6; fi
  echo "Logging in user '$user' to Steam Public...OK"; echo "Waiting for user info...OK"; exit 0
fi
dir=""
while [ $# -gt 0 ]; do
  case "$1" in
    +force_install_dir) dir="$2"; shift ;;
    +app_update) if [ "$2" = "2732960" ]; then touch "$dir/INIZH.big"; else touch "$dir/INI.big"; fi
                 echo " Update state (0x61) downloading, progress: 50.00 (1 / 2)"; shift ;;
  esac
  shift
done
exit 0
"#;

        fn root(name: &str) -> PathBuf {
            let r = std::env::temp_dir().join(format!("gen-assets-{name}-{}", std::process::id()));
            let _ = std::fs::remove_dir_all(&r);
            std::fs::create_dir_all(steamcmd_dir(&r)).unwrap();
            std::fs::write(steamcmd_dir(&r).join("steamcmd.sh"), FAKE).unwrap();
            r
        }

        fn until(job: &AssetJob, ok: impl Fn(&AssetState) -> bool) -> AssetState {
            for _ in 0..200 {
                let s = job.state();
                if ok(&s) {
                    return s;
                }
                std::thread::sleep(Duration::from_millis(50));
            }
            panic!("timed out; last state {:?}", job.state());
        }

        #[test]
        fn steam_guard_account_signs_in_after_the_code_is_entered() {
            let r = root("guard");
            let job = AssetJob::start_with(&r, "guarded", "secret".into(), Rosetta::not_needed()).unwrap();
            let s = until(&job, |s| s.need.is_some());
            assert_eq!(s.need, Some(Need::Guard));
            assert!(!s.log.contains("secret"));
            job.submit("99999"); // wrong code is refused and asked for again
            let s = until(&job, |s| s.need.is_some() && s.message.contains("didn't work"));
            assert_eq!(s.need, Some(Need::Guard));
            job.submit("12345");
            let s = until(&job, |s| s.phase == Phase::Done || s.phase == Phase::Failed);
            assert_eq!(s.phase, Phase::Done, "{s:?}");
            assert!(zero_hour_dir(&r).join("INIZH.big").is_file() && generals_dir(&r).join("INI.big").is_file());
            assert!(!steamcmd_dir(&r).join("rallypoint-signin.txt").exists(), "secret script must be deleted");
            std::fs::remove_dir_all(r).unwrap();
        }

        #[test]
        fn wrong_password_fails_without_asking_for_a_code() {
            let r = root("badpw");
            let job = AssetJob::start_with(&r, "alice", "bad".into(), Rosetta::not_needed()).unwrap();
            let s = until(&job, |s| s.phase == Phase::Failed);
            assert!(s.message.contains("password"), "{s:?}");
            assert!(!steamcmd_dir(&r).join("rallypoint-signin.txt").exists());
            std::fs::remove_dir_all(r).unwrap();
        }

        #[test]
        fn cached_login_needs_no_password() {
            let r = root("cached");
            let job = AssetJob::start_with(&r, "cached", String::new(), Rosetta::not_needed()).unwrap();
            let s = until(&job, |s| s.phase == Phase::Done || s.phase == Phase::Failed);
            assert_eq!(s.phase, Phase::Done, "{s:?}");
            std::fs::remove_dir_all(r).unwrap();
        }

        fn fake_rosetta(missing: bool, installs: Arc<std::sync::atomic::AtomicUsize>, marker: Option<PathBuf>) -> (Rosetta, Arc<AtomicBool>) {
            let flag = Arc::new(AtomicBool::new(missing));
            let (f1, f2) = (flag.clone(), flag.clone());
            let rosetta = Rosetta {
                missing: Box::new(move || f1.load(Ordering::Relaxed)),
                install: Box::new(move || {
                    installs.fetch_add(1, Ordering::Relaxed);
                    f2.store(false, Ordering::Relaxed);
                    if let Some(m) = &marker {
                        std::fs::write(m, "").unwrap();
                    }
                    Ok(())
                }),
            };
            (rosetta, flag)
        }

        #[test]
        fn missing_rosetta_is_offered_up_front_then_the_job_continues() {
            let r = root("rosetta-upfront");
            let installs = Arc::new(std::sync::atomic::AtomicUsize::new(0));
            let (ros, _) = fake_rosetta(true, installs.clone(), None);
            let job = AssetJob::start_with(&r, "cached", String::new(), ros).unwrap();
            let s = until(&job, |s| s.need.is_some());
            assert_eq!(s.need, Some(Need::Rosetta));
            assert_eq!(installs.load(Ordering::Relaxed), 0, "nothing installs until the user says so");
            job.submit("install");
            let s = until(&job, |s| s.phase == Phase::Done || s.phase == Phase::Failed);
            assert_eq!(s.phase, Phase::Done, "{s:?}");
            assert_eq!(installs.load(Ordering::Relaxed), 1);
            std::fs::remove_dir_all(r).unwrap();
        }

        #[test]
        fn bad_cpu_type_from_steamcmd_triggers_the_same_offer() {
            // the up-front check says all is well, but steamcmd itself can't run
            let r = root("rosetta-late");
            let installs = Arc::new(std::sync::atomic::AtomicUsize::new(0));
            let marker = steamcmd_dir(&r).join("rosetta-ok");
            let (mut ros, flag) = fake_rosetta(false, installs.clone(), Some(marker));
            let f = flag.clone();
            ros.missing = Box::new(move || f.load(Ordering::Relaxed)); // stays false until install flips it
            flag.store(false, Ordering::Relaxed);
            let job = AssetJob::start_with(&r, "norosetta", String::new(), ros).unwrap();
            let s = until(&job, |s| s.need.is_some());
            assert_eq!(s.need, Some(Need::Rosetta), "{s:?}");
            job.submit("install");
            let s = until(&job, |s| s.phase == Phase::Done || s.phase == Phase::Failed);
            assert_eq!(s.phase, Phase::Done, "{s:?}");
            assert_eq!(installs.load(Ordering::Relaxed), 1);
            std::fs::remove_dir_all(r).unwrap();
        }

        #[test]
        fn a_failed_rosetta_install_is_reported_with_the_manual_command() {
            let r = root("rosetta-fail");
            let ros = Rosetta { missing: Box::new(|| true), install: Box::new(|| Err("Rosetta wasn't installed. In Terminal run: softwareupdate --install-rosetta --agree-to-license".into())) };
            let job = AssetJob::start_with(&r, "cached", String::new(), ros).unwrap();
            until(&job, |s| s.need.is_some());
            job.submit("install");
            let s = until(&job, |s| s.phase == Phase::Failed);
            assert!(s.message.contains("softwareupdate --install-rosetta"));
            std::fs::remove_dir_all(r).unwrap();
        }

        #[test]
        fn cancel_stops_a_job_waiting_for_a_code() {
            let r = root("cancel");
            let job = AssetJob::start_with(&r, "guarded", "secret".into(), Rosetta::not_needed()).unwrap();
            until(&job, |s| s.need.is_some());
            job.cancel();
            std::thread::sleep(Duration::from_millis(300));
            assert!(job.state().phase != Phase::Done);
            std::fs::remove_dir_all(r).unwrap();
        }
    }
}
