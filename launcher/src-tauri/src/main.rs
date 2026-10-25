// No console window on Windows in release builds.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use generals_core::{agent::{Agent, Status}, assets::{self, AssetJob}, config::Config, engine, launch, mods, steam, update};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::SystemTime;
use tauri::Manager;

const MODS_JSON: &str = include_str!("../../../mods/mods.json");

#[derive(Default)]
struct Relay(Mutex<Option<Agent>>);
#[derive(Default)]
struct Downloads(Mutex<Option<AssetJob>>);
/// A mod the user is currently downloading in their browser: we watch their Downloads folder for it.
struct Watch {
    id: String,
    since: SystemTime,
}
#[derive(Default)]
struct ModWatch(Mutex<Option<Watch>>);
/// The newest release found by the last update check.
#[derive(Default)]
struct Updates(Mutex<Option<update::UpdateInfo>>);

fn base(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    Ok(app.path().data_dir().map_err(|e| e.to_string())?.join("GeneralsZH"))
}

fn mods_dir(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    Ok(base(app)?.join("Mods"))
}

fn config_path(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    Ok(base(app)?.join("config.json"))
}

fn catalog() -> Result<Vec<mods::Mod>, String> {
    mods::load_manifest(MODS_JSON).map_err(|e| e.to_string())
}

/// Open a catalog page in the user's browser. Only https URLs, and only ones from our own catalog.
fn open_url(url: &str) -> Result<(), String> {
    if !url.starts_with("https://") {
        return Err("only https links can be opened".into());
    }
    #[cfg(target_os = "macos")]
    let mut c = std::process::Command::new("open");
    #[cfg(windows)]
    let mut c = { let mut c = std::process::Command::new("rundll32"); c.arg("url.dll,FileProtocolHandler"); c };
    #[cfg(all(unix, not(target_os = "macos")))]
    let mut c = std::process::Command::new("xdg-open");
    c.arg(url).spawn().map(|_| ()).map_err(|e| e.to_string())
}

fn open_folder(path: &Path) -> Result<(), String> {
    std::fs::create_dir_all(path).map_err(|e| e.to_string())?;
    #[cfg(target_os = "macos")]
    let mut c = std::process::Command::new("open");
    #[cfg(windows)]
    let mut c = std::process::Command::new("explorer");
    #[cfg(all(unix, not(target_os = "macos")))]
    let mut c = std::process::Command::new("xdg-open");
    c.arg(path).spawn().map(|_| ()).map_err(|e| e.to_string())
}

// ---- game files ----------------------------------------------------------------------------------

#[tauri::command]
fn detect_games() -> Vec<steam::GameInstall> {
    steam::detect()
}

#[derive(serde::Serialize)]
struct GameCheck {
    ok: bool,
    message: String,
}

/// Is `path` a Zero Hour data folder?
#[tauri::command]
fn check_game_dir(path: String) -> GameCheck {
    let p = Path::new(path.trim());
    if path.trim().is_empty() {
        GameCheck { ok: false, message: "Choose the folder that contains INIZH.big".into() }
    } else if p.join("INIZH.big").is_file() {
        GameCheck { ok: true, message: "Zero Hour found".into() }
    } else if p.join("INI.big").is_file() {
        GameCheck { ok: false, message: "That's the original Generals. Rallypoint needs Zero Hour's folder (it has INIZH.big)".into() }
    } else {
        GameCheck { ok: false, message: "No Zero Hour files in that folder (INIZH.big is missing)".into() }
    }
}

#[tauri::command]
fn assets_start(app: tauri::AppHandle, jobs: tauri::State<Downloads>, user: String, password: String) -> Result<(), String> {
    let path = config_path(&app)?;
    let mut c = Config::load(&path);
    c.steam_user = user.trim().to_string();
    let _ = c.save(&path);
    let job = AssetJob::start(&base(&app)?, user.trim(), password).map_err(|e| e.to_string())?;
    *jobs.0.lock().unwrap() = Some(job);
    Ok(())
}

#[tauri::command]
fn assets_status(app: tauri::AppHandle, jobs: tauri::State<Downloads>) -> assets::AssetState {
    let state = jobs.0.lock().unwrap().as_ref().map(|j| j.state()).unwrap_or_default();
    if state.phase == assets::Phase::Done {
        // Point the launcher at the freshly downloaded files.
        if let (Ok(path), Ok(root)) = (config_path(&app), base(&app)) {
            let mut c = Config::load(&path);
            let zh = assets::zero_hour_dir(&root).to_string_lossy().to_string();
            if c.game_dir != zh {
                c.game_dir = zh;
                let _ = c.save(&path);
            }
        }
    }
    state
}

/// Answer the download's prompt: the password or the Steam Guard code.
#[tauri::command]
fn assets_submit(jobs: tauri::State<Downloads>, text: String) {
    if let Some(j) = jobs.0.lock().unwrap().as_ref() {
        j.submit(&text);
    }
}

/// While the download waits for approval in the Steam Mobile app: ask for a code instead.
#[tauri::command]
fn assets_use_code(jobs: tauri::State<Downloads>) {
    if let Some(j) = jobs.0.lock().unwrap().as_ref() {
        j.use_code();
    }
}

#[tauri::command]
fn assets_cancel(jobs: tauri::State<Downloads>) {
    *jobs.0.lock().unwrap() = None; // dropping the job stops it
}

// ---- config ---------------------------------------------------------------------------------------

/// What the UI may see. The serial (a credential the user never handles) is deliberately not part of it.
#[derive(serde::Serialize)]
struct ConfigView {
    server: String,
    room: String,
    game_dir: String,
    steam_user: String,
    setup_done: bool,
    check_updates: bool,
    version: &'static str,
    os: &'static str,
}

#[tauri::command]
fn get_config(app: tauri::AppHandle) -> Result<ConfigView, String> {
    let c = Config::load(&config_path(&app)?);
    Ok(ConfigView {
        server: c.server,
        room: c.room,
        game_dir: c.game_dir,
        steam_user: c.steam_user,
        setup_done: c.setup_done,
        check_updates: !c.skip_update_check,
        version: update::current_version(),
        os: std::env::consts::OS,
    })
}

#[tauri::command]
fn save_config(app: tauri::AppHandle, server: String, room: String, steam_user: String) -> Result<(), String> {
    let path = config_path(&app)?;
    let mut c = Config::load(&path);
    c.server = server.trim().to_string();
    c.room = room.trim().to_string();
    c.steam_user = steam_user.trim().to_string();
    c.save(&path).map_err(|e| e.to_string())
}

#[tauri::command]
fn save_game_dir(app: tauri::AppHandle, path: String) -> Result<(), String> {
    let cfg = config_path(&app)?;
    let mut c = Config::load(&cfg);
    c.game_dir = path.trim().to_string();
    c.save(&cfg).map_err(|e| e.to_string())
}

#[tauri::command]
fn set_check_updates(app: tauri::AppHandle, on: bool) -> Result<(), String> {
    let path = config_path(&app)?;
    let mut c = Config::load(&path);
    c.skip_update_check = !on;
    c.save(&path).map_err(|e| e.to_string())
}

#[tauri::command]
fn set_setup_done(app: tauri::AppHandle, done: bool) -> Result<(), String> {
    let path = config_path(&app)?;
    let mut c = Config::load(&path);
    c.setup_done = done;
    c.save(&path).map_err(|e| e.to_string())
}

// ---- online ---------------------------------------------------------------------------------------

/// Joins `room` on the relay, creating it with `password` if nobody is in it. Signing up with the server is automatic.
#[tauri::command]
fn relay_connect(app: tauri::AppHandle, relay: tauri::State<Relay>, password: String) -> Result<(), String> {
    let c = Config::load(&config_path(&app)?);
    if c.server.is_empty() {
        return Err("Enter the server address first".into());
    }
    if c.room.is_empty() {
        return Err("Enter a room name first".into());
    }
    let agent = Agent::start(&c.server, &c.serial, &c.room, &password).map_err(|e| e.to_string())?;
    *relay.0.lock().unwrap() = Some(agent); // dropping the old agent leaves its room
    Ok(())
}

#[tauri::command]
fn relay_disconnect(relay: tauri::State<Relay>) {
    *relay.0.lock().unwrap() = None;
}

#[tauri::command]
fn relay_status(app: tauri::AppHandle, relay: tauri::State<Relay>) -> String {
    use generals_proto::Reason::*;
    let guard = relay.0.lock().unwrap();
    // First connect: the server signed us up behind the scenes; keep the result for next time.
    if let (Some(a), Ok(path)) = (guard.as_ref(), config_path(&app)) {
        if let Some(serial) = a.take_issued() {
            let mut c = Config::load(&path);
            c.serial = serial;
            let _ = c.save(&path);
        }
    }
    match guard.as_ref().map(|a| a.status()) {
        None => "Offline".into(),
        Some(Status::Connecting) => "Connecting...".into(),
        Some(Status::Connected(v)) => format!("Connected as {}.{}.{}.{}", v[0], v[1], v[2], v[3]),
        Some(Status::Rejected(r)) => match r {
            x if x == WrongPassword as u8 => "Wrong room password",
            x if x == RoomFull as u8 => "That room is full",
            x if x == BadRoom as u8 => "Room names use letters, numbers, spaces, - _ . (max 32)",
            x if x == RateLimited as u8 => "Too many attempts, try again in a few minutes",
            x if x == Full as u8 => "Server is full",
            x if x == RegistrationClosed as u8 => "This server is private",
            x if x == UnknownSerial as u8 || x == Revoked as u8 || x == InUse as u8 => "This computer can't join right now",
            _ => "Rejected by server",
        }
        .into(),
    }
}

// ---- mods -----------------------------------------------------------------------------------------

#[derive(serde::Serialize)]
struct ModEntry {
    #[serde(flatten)]
    info: mods::Mod,
    installed: Option<mods::Installed>,
}

#[derive(serde::Serialize)]
struct ModList {
    catalog: Vec<ModEntry>,
    /// Installed folders that aren't in the catalog (added from a file or folder).
    other: Vec<mods::Installed>,
}

#[tauri::command]
fn mods_list(app: tauri::AppHandle) -> Result<ModList, String> {
    let installed = mods::list_installed(&mods_dir(&app)?);
    let catalog = catalog()?;
    let other = installed.iter().filter(|i| !catalog.iter().any(|m| m.path == i.folder)).cloned().collect();
    let catalog = catalog.into_iter().map(|m| ModEntry { installed: installed.iter().find(|i| i.folder == m.path).cloned(), info: m }).collect();
    Ok(ModList { catalog, other })
}

/// Open the mod's official download page. We then watch the Downloads folder, but only to *suggest* the file:
/// nothing is installed until the user picks one (or confirms the suggestion).
#[tauri::command]
fn mods_get(app: tauri::AppHandle, watch: tauri::State<ModWatch>, id: String) -> Result<(), String> {
    let m = catalog()?.into_iter().find(|m| m.id == id).ok_or("unknown mod")?;
    if let Some(base) = m.addon_for.as_ref().and_then(|b| catalog().ok()?.into_iter().find(|c| &c.id == b)) {
        if !mods_dir(&app)?.join(&base.path).is_dir() {
            return Err(format!("Install {} first; {} is an add-on for it", base.name, m.name));
        }
    }
    open_url(&m.download_page)?;
    *watch.0.lock().unwrap() = Some(Watch { id, since: SystemTime::now() });
    Ok(())
}

#[derive(serde::Serialize, Default)]
struct WatchStatus {
    /// "idle", "waiting" or "found"
    state: &'static str,
    id: String,
    /// File name of the suggested download.
    message: String,
    path: String,
}

/// Polled by the UI while a download is expected. Only looks; never installs.
#[tauri::command]
fn mods_poll(watch: tauri::State<ModWatch>) -> Result<WatchStatus, String> {
    let (id, since) = match watch.0.lock().unwrap().as_ref() {
        Some(w) => (w.id.clone(), w.since),
        None => return Ok(WatchStatus { state: "idle", ..Default::default() }),
    };
    let m = catalog()?.into_iter().find(|m| m.id == id).ok_or("unknown mod")?;
    Ok(match mods::downloads_dir().and_then(|d| mods::find_download(&d, since, &m.detect)) {
        Some(f) => WatchStatus { state: "found", id, message: f.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default(), path: f.to_string_lossy().into_owned() },
        None => WatchStatus { state: "waiting", id, ..Default::default() },
    })
}

#[tauri::command]
fn downloads_dir() -> Option<String> {
    mods::downloads_dir().filter(|d| d.is_dir()).map(|d| d.to_string_lossy().into_owned())
}

#[tauri::command]
fn mods_cancel_watch(watch: tauri::State<ModWatch>) {
    *watch.0.lock().unwrap() = None;
}

/// Install from a file or folder the user chose or dropped on the window. With `id`, it is installed as that catalog mod
/// (so it lands in the right folder and add-ons are checked against their base mod).
#[tauri::command]
async fn mods_install(app: tauri::AppHandle, watch: tauri::State<'_, ModWatch>, path: String, id: Option<String>) -> Result<mods::Installed, String> {
    let src = PathBuf::from(path.trim());
    let dir = mods_dir(&app)?;
    let folder = match id {
        Some(id) => {
            let cat = catalog()?;
            let m = cat.iter().find(|m| m.id == id).ok_or("unknown mod")?;
            if let Some(base) = m.addon_for.as_ref().and_then(|b| cat.iter().find(|c| &c.id == b)) {
                if !dir.join(&base.path).is_dir() {
                    return Err(format!("{} needs to be installed first", base.name));
                }
            }
            m.path.clone()
        }
        None => mods::folder_for_archive(&src).ok_or("couldn't work out a name for that mod")?,
    };
    let installed = tauri::async_runtime::spawn_blocking(move || mods::install(&src, &dir, &folder)).await.map_err(|e| e.to_string())??;
    *watch.0.lock().unwrap() = None; // whatever we were waiting for has arrived
    Ok(installed)
}

#[tauri::command]
fn mods_remove(app: tauri::AppHandle, folder: String) -> Result<(), String> {
    mods::remove(&mods_dir(&app)?, &folder)
}

#[tauri::command]
fn open_mods_folder(app: tauri::AppHandle) -> Result<(), String> {
    open_folder(&mods_dir(&app)?)
}

#[tauri::command]
fn open_homepage(id: String) -> Result<(), String> {
    open_url(&catalog()?.into_iter().find(|m| m.id == id).ok_or("unknown mod")?.homepage)
}

// ---- updates --------------------------------------------------------------------------------------

/// Ask GitHub for a newer release. `None` means up to date. Errors are for the Settings button; the startup check ignores them.
#[tauri::command]
async fn update_check(updates: tauri::State<'_, Updates>) -> Result<Option<update::UpdateInfo>, String> {
    let found = tauri::async_runtime::spawn_blocking(|| update::check(update::current_version())).await.map_err(|e| e.to_string())??;
    *updates.0.lock().unwrap() = found.clone();
    Ok(found)
}

/// Download the installer for this computer, verify it, and open it.
#[tauri::command]
async fn update_install(updates: tauri::State<'_, Updates>) -> Result<String, String> {
    let info = updates.0.lock().unwrap().clone().ok_or("check for updates first")?;
    tauri::async_runtime::spawn_blocking(move || {
        let file = update::download(&info, &update::downloads_dir())?;
        update::open_installer(&file)
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
fn open_release_page(updates: tauri::State<Updates>) -> Result<(), String> {
    let page = updates.0.lock().unwrap().as_ref().map(|u| u.page.clone()).ok_or("no release to show")?;
    if !page.starts_with("https://github.com/") {
        return Err("unexpected release link".into());
    }
    open_url(&page)
}

// ---- launching ------------------------------------------------------------------------------------

#[tauri::command]
fn launch_game(app: tauri::AppHandle, relay: tauri::State<Relay>, game_dir: PathBuf, mod_folder: Option<String>, width: u32, height: u32, fullscreen: bool, skirmish: bool) -> Result<(), String> {
    let base = base(&app)?;
    let data = app.path().data_dir().map_err(|e| e.to_string())?;
    let exe = engine::find(std::env::var("RALLYPOINT_ENGINE").ok().as_deref(), app.path().resource_dir().ok().as_deref(), &data)
        .ok_or("This build has no game engine for your platform yet")?;
    if !game_dir.join("INIZH.big").is_file() {
        return Err(format!("{} has no Zero Hour data (INIZH.big missing)", game_dir.display()));
    }
    let (mod_path, mod_args) = match mod_folder.filter(|f| !f.is_empty()) {
        Some(f) => {
            let name = mods::sanitize_folder(&f).filter(|n| *n == f).ok_or("invalid mod")?;
            let cat = catalog()?;
            let this = cat.iter().find(|m| m.path == name);
            let args = this.map(|m| m.args.clone()).unwrap_or_default();
            // An add-on is launched together with the mod it extends.
            let folder = match this.and_then(|m| m.addon_for.as_ref()).and_then(|id| cat.iter().find(|b| &b.id == id)) {
                Some(base) => mods::compose(&mods_dir(&app)?, &base.path, &name)?,
                None => mods_dir(&app)?.join(&name),
            };
            (Some(folder.to_string_lossy().to_string()), args)
        }
        None => (None, vec![]),
    };
    let opts = launch::LaunchOptions {
        width,
        height,
        fullscreen,
        mod_path,
        mod_args,
        // Online only once the relay has accepted us; the engine needs our virtual IP.
        relay: match relay.0.lock().unwrap().as_ref() {
            Some(a) => match a.status() {
                Status::Connected(v) => Some((a.local.to_string(), format!("{}.{}.{}.{}", v[0], v[1], v[2], v[3]))),
                _ => None,
            },
            None => None,
        },
        env: {
            let mut env = vec![];
            if skirmish {
                env.push(("GEN_AUTO_SKIRMISH".to_string(), "1".to_string()));
            }
            // Zero Hour also reads the base game's files.
            if let Some(b) = steam::base_game_for(&steam::detect(), &assets::generals_dir(&base)) {
                env.push(("GEN_GENERALS_PATH".to_string(), b.to_string_lossy().to_string()));
            }
            env
        },
    };
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let _ = std::fs::set_permissions(&exe, std::fs::Permissions::from_mode(0o755)); // bundles can lose the exec bit
    }
    launch::command(&exe, &game_dir, &opts).spawn().map_err(|e| format!("{}: {e}", exe.display()))?;
    // Remember the folder that worked.
    let path = config_path(&app)?;
    let mut c = Config::load(&path);
    if c.game_dir != game_dir.to_string_lossy() {
        c.game_dir = game_dir.to_string_lossy().into();
        let _ = c.save(&path);
    }
    Ok(())
}

fn main() {
    tauri::Builder::default()
        .manage(Relay::default())
        .manage(Downloads::default())
        .manage(ModWatch::default())
        .manage(Updates::default())
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![
            detect_games, check_game_dir, assets_start, assets_status, assets_submit, assets_use_code, assets_cancel,
            get_config, save_config, save_game_dir, set_setup_done, set_check_updates,
            update_check, update_install, open_release_page,
            relay_connect, relay_disconnect, relay_status,
            mods_list, mods_get, mods_poll, downloads_dir, mods_cancel_watch, mods_install, mods_remove, open_mods_folder, open_homepage,
            launch_game
        ])
        .run(tauri::generate_context!())
        .expect("tauri");
}
