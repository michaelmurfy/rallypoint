//! Downloading the game files from Steam with DepotDownloader (open source, native on macOS/Windows/Linux, so no
//! Rosetta). Steam only publishes Windows depots for these games, so we ask for `-os windows` everywhere.
//! The tool is fetched on demand from its GitHub release and checked against a pinned SHA-256 before it is used.
//! The Steam password goes to its stdin when it asks (never a command line) and is never logged; after the first
//! sign-in `-remember-password` lets later runs go without it.
use serde::Serialize;
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use crate::proc::quiet;
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender, TryRecvError};
use std::sync::{Arc, Mutex};
use std::time::{Duration, SystemTime};

pub const GENERALS_APP: u32 = 2229870;
pub const ZERO_HOUR_APP: u32 = 2732960;

const DD_VERSION: &str = "DepotDownloader_3.4.0";

/// One pinned release asset. SHA-256s were computed by CI (see .github/workflows/tool-checksums.yml).
pub struct Build {
    pub slug: &'static str,
    pub sha256: &'static str,
    pub bytes: u64,
}

pub fn build() -> Option<Build> {
    let b = |slug, sha256, bytes| Some(Build { slug, sha256, bytes });
    match (std::env::consts::OS, std::env::consts::ARCH) {
        ("macos", "aarch64") => b("macos-arm64", "60e80c7c496f3f9a079cd3c62036b35d088c27bc0149baf38f009eb57a52f6a5", 32245964),
        ("macos", "x86_64") => b("macos-x64", "3214b689564d73e9342a8a4aef693de6ad3d293801b0f300a4466f60ec75befb", 33808980),
        ("windows", "x86_64") => b("windows-x64", "41c9e9f0df54b3ad02e67a11726756e5c73283bd7c2e1b04acfa5ae4c2ed3767", 33474005),
        ("linux", "x86_64") => b("linux-x64", "a999dec66b4850fc961bd50366696d23c2d0fad7b18790e6a5647b2f19097a53", 33442357),
        ("linux", "aarch64") => b("linux-arm64", "d9fb612ccebc1db8eeea3b4045d2221ec70431381393ce908fb72f01d4f9c812", 32026406),
        _ => None,
    }
}

pub fn download_url(b: &Build) -> String {
    format!("https://github.com/SteamRE/DepotDownloader/releases/download/{DD_VERSION}/DepotDownloader-{}.zip", b.slug)
}

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
    /// Set while waiting for the user: a password or a Steam Guard code (see `AssetJob::submit`).
    pub need: Option<Need>,
    /// Waiting for the user to approve the sign-in in the Steam Mobile app (push notification). A code can be used instead.
    pub phone: bool,
    /// Recent tool output with secrets removed, for the "details" panel and bug reports.
    pub log: String,
}

/// Where the tool and the games live under `root` (the launcher's data directory).
pub fn tool_dir(root: &Path) -> PathBuf {
    root.join("tools").join(DD_VERSION)
}
pub fn generals_dir(root: &Path) -> PathBuf {
    root.join("game/Generals")
}
pub fn zero_hour_dir(root: &Path) -> PathBuf {
    root.join("game/ZeroHour")
}
/// The folder the tool believes is the user's data folder. DepotDownloader keeps the sign-in it was told to remember
/// (`-remember-password`: a long-lived login token, not the password) in .NET isolated storage under the user's data
/// folder, where it would stay for good and where other programs of this user can read it. On macOS and Linux that folder
/// comes from `XDG_DATA_HOME`, so the tool is given one of ours and it is emptied when the job ends. Windows has no such
/// override (the folder comes from the system), so there the file is found and removed instead (see `wipe_login`).
pub fn steam_home(root: &Path) -> PathBuf {
    root.join("tools").join("steam-home")
}

/// Every `account.config` (the tool's saved sign-in) below `dir`, which is an IsolatedStorage folder.
fn account_configs(dir: &Path, found: &mut Vec<PathBuf>, depth: usize) {
    for e in std::fs::read_dir(dir).into_iter().flatten().flatten() {
        let p = e.path();
        match e.file_type() {
            Ok(t) if t.is_dir() && depth < 6 => account_configs(&p, found, depth + 1),
            Ok(t) if t.is_file() && e.file_name() == "account.config" => found.push(p),
            _ => {}
        }
    }
}

fn isolated_storage_roots() -> Vec<PathBuf> {
    let mut v = vec![];
    if cfg!(windows) {
        if let Some(d) = std::env::var_os("LOCALAPPDATA") {
            v.push(PathBuf::from(d).join("IsolatedStorage"));
        }
    } else if let Some(h) = std::env::var_os("HOME") {
        v.push(PathBuf::from(&h).join(".local/share/IsolatedStorage"));
        v.push(PathBuf::from(h).join("Library/Application Support/IsolatedStorage"));
    }
    v
}

/// Remove the sign-in this job saved. `since`: when the job began; in the shared Windows folder only a file written
/// since then is touched, so a sign-in the user keeps for their own use of the tool is left alone.
pub fn wipe_login(root: &Path, since: SystemTime) -> usize {
    let mut gone = 0;
    if steam_home(root).exists() && std::fs::remove_dir_all(steam_home(root)).is_ok() {
        gone += 1;
    }
    if cfg!(windows) {
        let mut found = vec![];
        isolated_storage_roots().iter().for_each(|r| account_configs(r, &mut found, 0));
        for f in found {
            if std::fs::metadata(&f).and_then(|m| m.modified()).is_ok_and(|t| t >= since) && std::fs::remove_file(&f).is_ok() {
                gone += 1;
            }
        }
    }
    gone
}

/// "Forget my Steam sign-in": also removes sign-ins saved by earlier versions, which kept them for good.
pub fn forget_saved_logins(root: &Path) -> usize {
    forget_in(root, &isolated_storage_roots())
}

fn forget_in(root: &Path, shared: &[PathBuf]) -> usize {
    let mut gone = wipe_login(root, SystemTime::now());
    let mut found = vec![];
    shared.iter().for_each(|r| account_configs(r, &mut found, 0));
    for f in found {
        if std::fs::remove_file(&f).is_ok() {
            gone += 1;
        }
    }
    gone
}

fn tool_exe(dir: &Path) -> PathBuf {
    dir.join(if cfg!(windows) { "DepotDownloader.exe" } else { "DepotDownloader" })
}

/// Arguments for one app. No password: it comes in on stdin if the tool asks.
pub fn dd_args(user: &str, app: u32, dir: &Path, no_mobile: bool) -> Vec<String> {
    let s = |x: &str| x.to_string();
    let mut a = vec![s("-app"), app.to_string(), s("-os"), s("windows"), s("-dir"), dir.display().to_string(), s("-username"), user.to_string(), s("-remember-password")];
    // By default Steam sends a push notification to approve in the Steam Mobile app. `-no-mobile` asks for a code instead.
    if no_mobile {
        a.push(s("-no-mobile"));
    }
    a.push(s("-validate"));
    a
}

/// Account names go to the tool as an argument, so keep them boring.
pub fn valid_user(user: &str) -> bool {
    !user.is_empty() && user.len() <= 64 && !user.starts_with('-') && user.chars().all(|c| c.is_ascii_alphanumeric() || "_.-@".contains(c))
}

/// What a piece of tool output means.
#[derive(Debug, PartialEq)]
pub enum Seen {
    Progress(f32),
    /// A specific, user-meaningful reason (wrong password, game not owned, ...).
    Failure(String),
    /// A generic error line that usually follows the specific one; only used if nothing better was seen.
    Generic(String),
    /// The tool is waiting for approval in the Steam Mobile app.
    Approve,
    /// A prompt for the user (no trailing newline).
    Prompt(Need, /* previous code was wrong */ bool),
    Nothing,
}

pub fn scan_line(line: &str) -> Seen {
    let l = line.trim();
    // "  12.34% depot/file"
    if let Some((num, _)) = l.split_once('%') {
        if let Ok(p) = num.trim().parse::<f32>() {
            return Seen::Progress(p.clamp(0.0, 100.0));
        }
    }
    let low = l.to_ascii_lowercase();
    if low.contains("steam mobile app") || low.contains("confirm your sign in") || low.contains("confirm the login") {
        return Seen::Approve;
    }
    if low.contains("invalidpassword") {
        return Seen::Failure("Steam rejected the username or password".into());
    }
    if low.contains("ratelimit") || low.contains("loginthrottle") || low.contains("logindeniedthrottle") {
        return Seen::Failure("Steam is rate limiting logins, try again in a while".into());
    }
    if low.contains("is not available from this account") || low.contains("no licenses") || low.contains("does not have a license") {
        return Seen::Failure("This Steam account doesn't own the game".into());
    }
    if low.contains("accountlogondenied") || low.contains("twofactorcodemismatch") || low.contains("invalidloginauthcode") || low.contains("expired") {
        return Seen::Failure("Steam didn't accept that code. Try the sign-in again".into());
    }
    if low.starts_with("error:") || low.contains("unable to get steam3 credentials") {
        return Seen::Generic(l.to_string());
    }
    Seen::Nothing
}

/// The unfinished last line of output: the tool prompts without a trailing newline.
pub fn scan_prompt(partial: &str) -> Option<(Need, bool)> {
    let low = partial.to_ascii_lowercase();
    if low.contains("enter account password") {
        Some((Need::Password, false))
    } else if low.contains("steam guard") && low.trim_end().ends_with(':') {
        Some((Need::Guard, low.contains("previously provided") || low.contains("incorrect")))
    } else {
        None
    }
}

pub fn scrub(text: &str, secrets: &[&str]) -> String {
    secrets.iter().filter(|s| s.len() >= 3).fold(text.to_string(), |t, s| t.replace(s, "***"))
}

type Shared<T> = Arc<Mutex<T>>;

pub struct AssetJob {
    state: Shared<AssetState>,
    input: Mutex<Option<Sender<String>>>,
    child: Shared<Option<Child>>,
    cancelled: Arc<AtomicBool>,
    use_code: Arc<AtomicBool>,
}

struct Ctx {
    state: Shared<AssetState>,
    child: Shared<Option<Child>>,
    cancelled: Arc<AtomicBool>,
    use_code: Arc<AtomicBool>,
    log: Vec<String>,
    /// Where the tool keeps its remembered sign-in during this job (see `steam_home`).
    home: PathBuf,
}

impl Ctx {
    fn set(&mut self, phase: Phase, percent: f32, message: &str, need: Option<Need>) {
        let log = self.log.join("\n");
        *self.state.lock().unwrap() = AssetState { phase, percent, message: message.into(), need, phone: false, log };
    }

    fn set_phone(&mut self, percent: f32) {
        let log = self.log.join("\n");
        *self.state.lock().unwrap() = AssetState {
            phase: Phase::LoggingIn,
            percent,
            message: "Approve this sign-in in the Steam Mobile app on your phone".into(),
            need: None,
            phone: true,
            log,
        };
    }

    fn keep(&mut self, text: &str) {
        self.log.extend(text.lines().map(str::trim).filter(|l| !l.is_empty()).map(String::from));
        let n = self.log.len().saturating_sub(40);
        self.log.drain(..n);
    }

    fn fail(&mut self, message: &str) {
        self.set(Phase::Failed, 0.0, message, None);
    }
}

/// Download, verify and unpack DepotDownloader if it isn't there yet.
fn ensure_tool(root: &Path) -> Result<PathBuf, String> {
    let dir = tool_dir(root);
    let exe = tool_exe(&dir);
    if exe.is_file() {
        return Ok(exe);
    }
    let b = build().ok_or("Steam downloads aren't available on this kind of computer")?;
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let zip = dir.join("download.zip");
    let status = quiet(&mut Command::new("curl"))
        .args(["-fsSL", "--proto", "=https", "--proto-redir", "=https", "-o"])
        .arg(&zip)
        .arg(download_url(&b))
        .status()
        .map_err(|e| format!("curl isn't available ({e})"))?;
    if !status.success() {
        return Err("Couldn't download Steam's downloader. Check your internet connection".into());
    }
    let got = crate::extract::hex(&crate::extract::sha256_file(&zip).map_err(|e| e.to_string())?);
    if got != b.sha256 {
        let _ = std::fs::remove_file(&zip);
        return Err("The downloaded tool didn't match its expected checksum, so it was discarded".into());
    }
    let staging = dir.join("unpack");
    let _ = std::fs::remove_dir_all(&staging);
    std::fs::create_dir_all(&staging).map_err(|e| e.to_string())?;
    crate::extract::extract(&zip, &staging)?;
    let _ = std::fs::remove_file(&zip);
    let unpacked = tool_exe(&staging);
    if !unpacked.is_file() {
        return Err("The downloaded tool was missing its program file".into());
    }
    std::fs::rename(&unpacked, &exe).map_err(|e| e.to_string())?;
    let _ = std::fs::remove_dir_all(&staging);
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&exe, std::fs::Permissions::from_mode(0o755)).map_err(|e| e.to_string())?;
    }
    Ok(exe)
}

fn write_line(stdin: &mut Option<ChildStdin>, text: &str) {
    if let Some(s) = stdin.as_mut() {
        let _ = writeln!(s, "{text}");
        let _ = s.flush();
    }
}

enum Run {
    Finished(bool),
    Stopped,
    /// The user chose to type a Steam Guard code instead of approving on their phone.
    SwitchToCode,
}

/// One DepotDownloader run for one app. Handles its prompts: the password from `password` (once), otherwise the
/// user is asked through the job state and the answer arrives on `inbox`.
fn run_app(ctx: &mut Ctx, exe: &Path, user: &str, app: u32, dir: &Path, password: &mut Option<String>, code: &mut String, inbox: &Receiver<String>, label: &str, base: f32, no_mobile: bool) -> Result<(Run, Option<String>), String> {
    let mut cmd = Command::new(exe);
    quiet(&mut cmd).args(dd_args(user, app, dir, no_mobile));
    if !cfg!(windows) {
        // a private data folder for the tool's remembered sign-in, emptied when the job ends
        let _ = std::fs::create_dir_all(&ctx.home);
        cmd.env("XDG_DATA_HOME", &ctx.home);
    }
    let mut child = cmd
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|e| format!("Couldn't start Steam's downloader: {e}"))?;
    let mut stdin = child.stdin.take();
    let (tx, rx) = mpsc::channel::<String>();
    for r in [Box::new(child.stdout.take().unwrap()) as Box<dyn Read + Send>, Box::new(child.stderr.take().unwrap())] {
        let tx = tx.clone();
        std::thread::spawn(move || {
            let (mut r, mut buf) = (r, [0u8; 4096]);
            while let Ok(n) = r.read(&mut buf) {
                if n == 0 || tx.send(String::from_utf8_lossy(&buf[..n]).into_owned()).is_err() {
                    break;
                }
            }
        });
    }
    drop(tx);
    *ctx.child.lock().unwrap() = Some(child);

    let (mut partial, mut failure, mut waiting, mut code_rejected) = (String::new(), None::<String>, None::<Need>, false);
    let (started, mut saw_progress, mut phone, mut switch) = (std::time::Instant::now(), false, false, false);
    ctx.set(Phase::LoggingIn, base, "Signing in to Steam…", None);
    let mut stopped = false;
    loop {
        match rx.recv_timeout(Duration::from_millis(100)) {
            Ok(chunk) => {
                let secrets = [password.as_deref().unwrap_or(""), code.as_str()];
                ctx.keep(&scrub(&chunk, &secrets));
                partial.push_str(&chunk);
                while let Some(i) = partial.find(['\n', '\r']) {
                    let line: String = partial.drain(..=i).collect();
                    // the tool says a code was wrong on its own line, before it asks again
                    // (stdout and stderr are separate pipes, so this can arrive before or after the next prompt)
                    if line.to_ascii_lowercase().contains("previously provided") {
                        code_rejected = true;
                        if waiting == Some(Need::Guard) {
                            ctx.set(Phase::LoggingIn, base, "That code didn't work. Enter the newest Steam Guard code", Some(Need::Guard));
                        }
                    }
                    match scan_line(&line) {
                        Seen::Progress(p) => {
                            saw_progress = true;
                            phone = false;
                            ctx.set(Phase::Downloading, base + p / 2.0, &format!("Downloading {label}…"), None)
                        }
                        Seen::Approve if !no_mobile => {
                            phone = true;
                            ctx.set_phone(base);
                        }
                        Seen::Failure(m) => failure = Some(m),
                        Seen::Generic(m) => failure = failure.take().or(Some(m)), // the specific reason comes first and wins
                        _ => {}
                    }
                }
                if waiting.is_none() {
                    match scan_prompt(&partial) {
                        Some((Need::Password, _)) if password.is_some() => {
                            write_line(&mut stdin, &password.take().unwrap());
                            partial.clear();
                        }
                        Some((need, wrong)) => {
                            let wrong = wrong || (need == Need::Guard && code_rejected);
                            let msg = match (need, wrong) {
                                (Need::Password, _) => "Enter your Steam password",
                                (Need::Guard, true) => "That code didn't work. Enter the newest Steam Guard code",
                                (Need::Guard, false) if partial.to_ascii_lowercase().contains("email") => "Steam emailed you a code. Enter it",
                                (Need::Guard, false) => "Enter the Steam Guard code from your authenticator app",
                            };
                            phone = false;
                            ctx.set(Phase::LoggingIn, base, msg, Some(need));
                            waiting = Some(need);
                            partial.clear();
                        }
                        None => {}
                    }
                }
            }
            Err(RecvTimeoutError::Timeout) => {}
            Err(RecvTimeoutError::Disconnected) => break, // both pipes closed: the process is done
        }
        // The tool's wording for the push notice may differ from what we match, so after a few quiet seconds of
        // signing in, say what is probably happening.
        if !no_mobile && !phone && !saw_progress && waiting.is_none() && started.elapsed() > Duration::from_secs(6) {
            phone = true;
            ctx.set_phone(base);
        }
        if phone && ctx.use_code.swap(false, Ordering::Relaxed) {
            switch = true;
            if let Some(c) = ctx.child.lock().unwrap().as_mut() {
                let _ = c.kill();
            }
            break;
        }
        if ctx.cancelled.load(Ordering::Relaxed) {
            stopped = true;
            if let Some(c) = ctx.child.lock().unwrap().as_mut() {
                let _ = c.kill();
            }
            break;
        }
        if let Some(need) = waiting {
            match inbox.try_recv() {
                Ok(text) => {
                    if need == Need::Guard {
                        *code = text.trim().to_string();
                        code_rejected = false;
                    }
                    write_line(&mut stdin, text.trim());
                    ctx.set(Phase::LoggingIn, base, "Signing in to Steam…", None);
                    waiting = None;
                }
                Err(TryRecvError::Disconnected) => {
                    stopped = true;
                    break;
                }
                Err(TryRecvError::Empty) => {}
            }
        }
    }
    let status = ctx.child.lock().unwrap().take().map(|mut c| {
        if stopped {
            let _ = c.kill();
        }
        c.wait()
    });
    if switch {
        return Ok((Run::SwitchToCode, failure));
    }
    if stopped {
        return Ok((Run::Stopped, failure));
    }
    Ok((Run::Finished(matches!(status, Some(Ok(s)) if s.success())), failure))
}

fn worker(root: PathBuf, user: String, password: String, rx: Receiver<String>, ctx: Ctx) {
    let began = SystemTime::now();
    worker_inner(root.clone(), user, password, rx, ctx);
    // however it ended (done, failed, cancelled): the remembered sign-in is not kept
    wipe_login(&root, began);
}

fn worker_inner(root: PathBuf, user: String, password: String, rx: Receiver<String>, mut ctx: Ctx) {
    ctx.set(Phase::Preparing, 0.0, "Getting Steam's downloader…", None);
    let exe = match ensure_tool(&root) {
        Ok(e) => e,
        Err(m) => return ctx.fail(&m),
    };
    let (gen, zh) = (generals_dir(&root), zero_hour_dir(&root));
    if std::fs::create_dir_all(&gen).and(std::fs::create_dir_all(&zh)).is_err() {
        return ctx.fail("Couldn't create the game folder");
    }
    let original = Some(password).filter(|p| !p.is_empty()); // kept in memory so switching to a code can sign in again
    let mut password = original.clone();
    let mut code = String::new();
    let mut no_mobile = false;
    for (i, (app, dir, label, marker)) in [(GENERALS_APP, &gen, "Generals", "INI.big"), (ZERO_HOUR_APP, &zh, "Zero Hour", "INIZH.big")].into_iter().enumerate() {
        loop {
            match run_app(&mut ctx, &exe, &user, app, dir, &mut password, &mut code, &rx, label, i as f32 * 50.0, no_mobile) {
                Err(m) => return ctx.fail(&m),
                Ok((Run::Stopped, _)) => return,
                Ok((Run::SwitchToCode, _)) => {
                    no_mobile = true; // from now on ask for a code
                    password = original.clone(); // the interrupted sign-in used it up
                    continue;
                }
                Ok((Run::Finished(ok), failure)) => {
                    if !(ok && dir.join(marker).is_file()) {
                        return ctx.fail(&failure.unwrap_or_else(|| format!("Steam didn't finish downloading {label}")));
                    }
                    break;
                }
            }
        }
    }
    ctx.set(Phase::Done, 100.0, "Game files are ready", None);
}

impl AssetJob {
    /// Starts the download in the background. `password` may be empty if a remembered login exists.
    pub fn start(root: &Path, user: &str, password: String) -> io::Result<AssetJob> {
        if !valid_user(user) {
            return Err(io::Error::other("enter your Steam account name"));
        }
        let (state, child, cancelled, use_code): (Shared<AssetState>, Shared<Option<Child>>, Arc<AtomicBool>, Arc<AtomicBool>) = Default::default();
        let (tx, rx) = mpsc::channel();
        let ctx = Ctx { state: state.clone(), child: child.clone(), cancelled: cancelled.clone(), use_code: use_code.clone(), log: vec![], home: steam_home(root) };
        let (root, user) = (root.to_path_buf(), user.to_string());
        std::thread::spawn(move || worker(root, user, password, rx, ctx));
        Ok(AssetJob { state, input: Mutex::new(Some(tx)), child, cancelled, use_code })
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

    /// While waiting for phone approval: type a Steam Guard code instead.
    pub fn use_code(&self) {
        self.use_code.store(true, Ordering::Relaxed);
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn args_never_carry_a_password() {
        let a = dd_args("alice", ZERO_HOUR_APP, Path::new("/g/ZH"), false).join(" ");
        assert!(a.contains("-app 2732960 -os windows -dir /g/ZH -username alice -remember-password -validate"));
        assert!(!a.contains("-no-mobile"), "phone approval is the default");
        assert!(dd_args("alice", ZERO_HOUR_APP, Path::new("/g/ZH"), true).contains(&"-no-mobile".to_string()));
        assert!(!dd_args("alice", ZERO_HOUR_APP, Path::new("/g/ZH"), true).iter().any(|x| x == "-password"));
    }

    #[test]
    fn the_saved_sign_in_is_removed_after_a_job_and_old_ones_on_request() {
        let t = std::env::temp_dir().join(format!("gen-steam-forget-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        let shared = t.join("IsolatedStorage");
        let old = shared.join("abc/def/AppFiles");
        std::fs::create_dir_all(&old).unwrap();
        std::fs::write(old.join("account.config"), "token").unwrap();
        std::fs::write(old.join("other.txt"), "keep").unwrap();
        std::fs::create_dir_all(steam_home(&t).join("IsolatedStorage/x")).unwrap();
        std::fs::write(steam_home(&t).join("IsolatedStorage/x/account.config"), "token").unwrap();
        // after a job: our private folder is gone; the shared folder is only touched on Windows, and only for files newer than the job
        wipe_login(&t, SystemTime::now());
        assert!(!steam_home(&t).exists());
        assert!(old.join("other.txt").exists());
        if !cfg!(windows) {
            assert!(old.join("account.config").exists(), "a file the job did not write is left alone");
        }
        // the explicit action removes sign-ins saved by earlier versions too, and nothing else
        assert!(forget_in(&t, &[shared.clone()]) >= 1);
        assert!(!old.join("account.config").exists() && old.join("other.txt").exists());
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn account_names_are_restricted() {
        for ok in ["alice", "a_b.c-d", "me@example.com"] {
            assert!(valid_user(ok), "{ok}");
        }
        for bad in ["", "-app", "a b", "a\nb", "a\"b", &"x".repeat(65)] {
            assert!(!valid_user(bad), "{bad:?}");
        }
        assert!(AssetJob::start(Path::new("/nonexistent"), "-app", String::new()).is_err());
    }

    #[test]
    fn reads_progress_and_failures() {
        assert_eq!(scan_line(" 12.34% depot/file.big"), Seen::Progress(12.34));
        assert_eq!(scan_line("100.00% Data/INI.big"), Seen::Progress(100.0));
        assert!(matches!(scan_line("Failed to authenticate with Steam: Authentication failed with result InvalidPassword."), Seen::Failure(m) if m.contains("password")));
        assert!(matches!(scan_line("App 2732960 (Zero Hour) is not available from this account."), Seen::Failure(m) if m.contains("doesn't own")));
        assert!(matches!(scan_line("Authentication failed with result RateLimitExceeded"), Seen::Failure(m) if m.contains("rate")));
        assert_eq!(scan_line("Connecting to Steam3... Done!"), Seen::Nothing);
        assert_eq!(scan_line("STEAM GUARD! Use the Steam Mobile App to confirm your sign in..."), Seen::Approve);
        // the generic lines the real tool prints after the specific reason
        assert!(matches!(scan_line("Unable to get steam3 credentials."), Seen::Generic(_)));
        assert!(matches!(scan_line("Error: InitializeSteam failed"), Seen::Generic(_)));
    }

    #[test]
    fn detects_prompts_without_newlines() {
        assert_eq!(scan_prompt("Enter account password for \"alice\": "), Some((Need::Password, false)));
        assert_eq!(scan_prompt("STEAM GUARD! Please enter your 2 factor auth code from your authenticator app: "), Some((Need::Guard, false)));
        assert_eq!(scan_prompt("The previously provided 2-factor auth code is incorrect.\nSTEAM GUARD! Please enter your 2 factor auth code from your authenticator app: "), Some((Need::Guard, true)));
        assert_eq!(scan_prompt("STEAM GUARD! Please enter the auth code sent to the email at a***@b.com: "), Some((Need::Guard, false)));
        assert_eq!(scan_prompt("Logging 'alice' into Steam3..."), None);
        assert_eq!(scan_prompt(" 12.34% depot/file"), None);
    }

    #[test]
    fn scrub_removes_secrets() {
        assert_eq!(scrub("pw hunter2 code 12345 hunter2 ab", &["hunter2", "12345", "ab", ""]), "pw *** code *** *** ab");
    }

    #[test]
    fn pinned_downloads_are_https_github_releases() {
        for (slug, sha, bytes) in [("macos-arm64", "60e80c7c496f3f9a079cd3c62036b35d088c27bc0149baf38f009eb57a52f6a5", 32245964u64)] {
            let b = Build { slug, sha256: sha, bytes };
            assert_eq!(download_url(&b), "https://github.com/SteamRE/DepotDownloader/releases/download/DepotDownloader_3.4.0/DepotDownloader-macos-arm64.zip");
            assert_eq!(b.sha256.len(), 64);
        }
        if let Some(b) = build() {
            assert_eq!(b.sha256.len(), 64);
            assert!(download_url(&b).starts_with("https://github.com/SteamRE/DepotDownloader/releases/download/"));
        }
    }

    /// Opt-in check against the real tool and Steam, with a made-up account (it can only fail to log in):
    /// RALLYPOINT_REAL_DD=/path/to/DepotDownloader cargo test -p generals-core -- --ignored real_tool
    #[cfg(unix)]
    #[test]
    #[ignore]
    fn real_tool_rejects_a_made_up_login_cleanly() {
        let Ok(real) = std::env::var("RALLYPOINT_REAL_DD") else { return };
        let r = std::env::temp_dir().join(format!("gen-dd-real-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&r);
        std::fs::create_dir_all(tool_dir(&r)).unwrap();
        std::fs::copy(&real, tool_exe(&tool_dir(&r))).unwrap();
        let job = AssetJob::start(&r, "rallypoint-nonexistent-test", "not-the-password-123".into()).unwrap();
        let mut s = job.state();
        for _ in 0..600 {
            s = job.state();
            if s.phase == Phase::Failed || s.phase == Phase::Done || s.need.is_some() {
                break;
            }
            std::thread::sleep(Duration::from_millis(100));
        }
        assert_eq!(s.phase, Phase::Failed, "{s:?}");
        assert!(s.message.contains("rejected the username or password"), "{s:?}");
        assert!(!s.log.contains("not-the-password-123"), "password leaked into the log");
        assert!(s.log.contains("Enter account password"), "the real prompt was seen: {}", s.log);
        std::fs::remove_dir_all(r).unwrap();
    }

    /// End to end against a fake DepotDownloader that prompts the way the real one does, with prompts that have
    /// no trailing newline, a password read from stdin, and a remembered login that skips the prompts next time.
    #[cfg(unix)]
    mod fake_tool {
        use super::*;

        const FAKE: &str = r#"#!/bin/sh
app=""; dir=""; user=""
while [ $# -gt 0 ]; do
  case "$1" in -app) app="$2"; shift;; -dir) dir="$2"; shift;; -username) user="$2"; shift;; -no-mobile) nomobile=1;; esac
  shift
done
marker="$(dirname "$0")/logged-in-$user"
if [ ! -f "$marker" ] && [ "$user" != "cached" ]; then
  printf 'Enter account password for "%s": ' "$user"
  read pw; echo
  echo "Connecting to Steam3... Done!"
  if [ "$pw" = "bad" ]; then
    echo "Failed to authenticate with Steam: Authentication failed with result InvalidPassword." >&2
    echo "Unable to get steam3 credentials." >&2; echo "Error: InitializeSteam failed" >&2; exit 1
  fi
  if [ "$user" = "guarded" ]; then
    printf 'STEAM GUARD! Please enter your 2 factor auth code from your authenticator app: '
    read code; echo
    while [ "$code" != "12345" ]; do
      echo "The previously provided 2-factor auth code is incorrect." >&2
      printf 'STEAM GUARD! Please enter your 2 factor auth code from your authenticator app: '
      read code; echo
    done
  fi
  if [ "$user" = "phone" ]; then echo "STEAM GUARD! Use the Steam Mobile App to confirm your sign in..." >&2; sleep 1; fi
  if [ "$user" = "phonecode" ]; then
    if [ -z "$nomobile" ]; then echo "STEAM GUARD! Use the Steam Mobile App to confirm your sign in..." >&2; exec sleep 30; fi
    printf 'STEAM GUARD! Please enter your 2 factor auth code from your authenticator app: '
    read code; echo
    [ "$code" = "12345" ] || exit 1
  fi
  touch "$marker"
fi
if [ "$user" = "unowned" ]; then echo "App $app (X) is not available from this account." >&2; exit 1; fi
echo "Got session"
echo " 25.00% depot/a"; echo " 75.00% depot/b"; echo "100.00% depot/c"
if [ "$app" = "2732960" ]; then touch "$dir/INIZH.big"; else touch "$dir/INI.big"; fi
exit 0
"#;

        fn root(name: &str) -> PathBuf {
            let r = std::env::temp_dir().join(format!("gen-dd-{name}-{}", std::process::id()));
            let _ = std::fs::remove_dir_all(&r);
            std::fs::create_dir_all(tool_dir(&r)).unwrap();
            let exe = tool_exe(&tool_dir(&r));
            std::fs::write(&exe, FAKE).unwrap();
            use std::os::unix::fs::PermissionsExt;
            std::fs::set_permissions(&exe, std::fs::Permissions::from_mode(0o755)).unwrap();
            r
        }

        fn until(job: &AssetJob, ok: impl Fn(&AssetState) -> bool) -> AssetState {
            for _ in 0..300 {
                let s = job.state();
                if ok(&s) {
                    return s;
                }
                std::thread::sleep(Duration::from_millis(50));
            }
            panic!("timed out; last state {:?}", job.state());
        }

        #[test]
        fn password_prompt_is_answered_from_stdin_and_both_games_download() {
            let r = root("plain");
            let job = AssetJob::start(&r, "alice", "hunter22".into()).unwrap();
            let s = until(&job, |s| s.phase == Phase::Done || s.phase == Phase::Failed);
            assert_eq!(s.phase, Phase::Done, "{s:?}");
            assert!(zero_hour_dir(&r).join("INIZH.big").is_file() && generals_dir(&r).join("INI.big").is_file());
            assert!(!s.log.contains("hunter22"));
            std::fs::remove_dir_all(r).unwrap();
        }

        #[test]
        fn steam_guard_code_is_asked_for_and_a_wrong_one_is_re_asked() {
            let r = root("guard");
            let job = AssetJob::start(&r, "guarded", "secret".into()).unwrap();
            let s = until(&job, |s| s.need.is_some());
            assert_eq!(s.need, Some(Need::Guard));
            job.submit("99999");
            let s = until(&job, |s| s.need.is_some() && s.message.contains("didn't work"));
            assert_eq!(s.need, Some(Need::Guard));
            job.submit("12345");
            let s = until(&job, |s| s.phase == Phase::Done || s.phase == Phase::Failed);
            assert_eq!(s.phase, Phase::Done, "{s:?}");
            assert!(!s.log.contains("secret") && !s.log.contains("12345"));
            std::fs::remove_dir_all(r).unwrap();
        }

        #[test]
        fn phone_approval_is_shown_and_the_download_continues_once_approved() {
            let r = root("phone");
            let job = AssetJob::start(&r, "phone", "secret12".into()).unwrap();
            let s = until(&job, |s| s.phone || s.phase == Phase::Done);
            assert!(s.phone && s.need.is_none(), "{s:?}");
            assert!(s.message.contains("Steam Mobile app"));
            let s = until(&job, |s| s.phase == Phase::Done || s.phase == Phase::Failed);
            assert_eq!(s.phase, Phase::Done, "{s:?}");
            assert!(!s.phone);
            std::fs::remove_dir_all(r).unwrap();
        }

        #[test]
        fn a_code_can_be_typed_instead_of_approving_on_the_phone() {
            let r = root("phonecode");
            let job = AssetJob::start(&r, "phonecode", "secret12".into()).unwrap();
            until(&job, |s| s.phone);
            job.use_code();
            let s = until(&job, |s| s.need.is_some());
            assert_eq!(s.need, Some(Need::Guard), "{s:?}");
            assert!(!s.phone);
            job.submit("12345");
            let s = until(&job, |s| s.phase == Phase::Done || s.phase == Phase::Failed);
            assert_eq!(s.phase, Phase::Done, "{s:?}");
            std::fs::remove_dir_all(r).unwrap();
        }

        #[test]
        fn missing_password_is_asked_for_in_the_ui() {
            let r = root("askpw");
            let job = AssetJob::start(&r, "alice", String::new()).unwrap();
            let s = until(&job, |s| s.need.is_some());
            assert_eq!(s.need, Some(Need::Password));
            job.submit("hunter22");
            let s = until(&job, |s| s.phase == Phase::Done || s.phase == Phase::Failed);
            assert_eq!(s.phase, Phase::Done, "{s:?}");
            std::fs::remove_dir_all(r).unwrap();
        }

        #[test]
        fn wrong_password_and_unowned_game_fail_with_clear_messages() {
            let r = root("bad");
            let s = until(&AssetJob::start(&r, "alice", "bad".into()).unwrap(), |s| s.phase == Phase::Failed);
            assert!(s.message.contains("password"), "{s:?}");
            let r2 = root("unowned");
            let s = until(&AssetJob::start(&r2, "unowned", "pw12345".into()).unwrap(), |s| s.phase == Phase::Failed);
            assert!(s.message.contains("doesn't own"), "{s:?}");
            std::fs::remove_dir_all(r).unwrap();
            std::fs::remove_dir_all(r2).unwrap();
        }

        #[test]
        fn remembered_login_needs_no_prompts() {
            let r = root("cached");
            let s = until(&AssetJob::start(&r, "cached", String::new()).unwrap(), |s| s.phase == Phase::Done || s.phase == Phase::Failed || s.need.is_some());
            assert_eq!(s.phase, Phase::Done, "{s:?}");
            std::fs::remove_dir_all(r).unwrap();
        }

        #[test]
        fn cancel_stops_a_job_waiting_for_a_code() {
            let r = root("cancel");
            let job = AssetJob::start(&r, "guarded", "secret".into()).unwrap();
            until(&job, |s| s.need.is_some());
            job.cancel();
            std::thread::sleep(Duration::from_millis(400));
            assert!(job.state().phase != Phase::Done);
            std::fs::remove_dir_all(r).unwrap();
        }
    }
}
