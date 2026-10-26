// No console window on Windows in release builds.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use generals_core::{bundle, agent::{Agent, Status}, assets::{self, AssetJob}, config::Config, diagnose, engine, graphics, syncdiff, icons, launch, maps, mods, steam, maporg, mapview, selfupdate, update};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::SystemTime;
use tauri::Manager;

const MODS_JSON: &str = include_str!("../../../mods/mods.json");
const MAP_PACKS_JSON: &str = include_str!("../../../mods/mappacks.json");

#[derive(Default)]
struct Relay(Mutex<Option<Agent>>);
#[derive(Default)]
struct Downloads(Mutex<Option<AssetJob>>);
/// A mod the user is currently downloading in their browser: we watch their Downloads folder for it.
struct Watch {
    id: String,
    since: SystemTime,
}
/// The server (lower case `host:port`) the running agent was started for.
#[derive(Default)]
struct RelayServer(Mutex<String>);
#[derive(Default)]
struct ModWatch(Mutex<Option<Watch>>);
#[derive(Default)]
struct MapJobs(Mutex<Option<maps::MapJob>>);
/// The newest release found by the last update check.
#[derive(Default)]
struct Updates(Mutex<Option<update::UpdateInfo>>);

fn base(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    Ok(app.path().data_dir().map_err(|e| e.to_string())?.join("GeneralsZH"))
}

/// What the engine printed on its last run.
fn engine_log(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    Ok(base(app)?.join("engine.log"))
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

/// Checks behind "You have encountered a serious error": game files, base game, last crash report.
#[tauri::command]
fn diagnose(app: tauri::AppHandle, game_dir: String) -> Result<diagnose::Report, String> {
    let base = base(&app)?;
    let data = app.path().data_dir().map_err(|e| e.to_string())?;
    let engine = engine::find(std::env::var("RALLYPOINT_ENGINE").ok().as_deref(), app.path().resource_dir().ok().as_deref(), &data);
    let base_game = steam::base_game_for(&steam::detect(), &assets::generals_dir(&base));
    let docs = app.path().document_dir().map_err(|e| e.to_string())?;
    Ok(diagnose::run(Path::new(game_dir.trim()), base_game.as_deref(), engine.as_deref(), &docs, Some(&engine_log(&app)?)))
}

#[tauri::command]
fn open_game_data_folder(app: tauri::AppHandle) -> Result<(), String> {
    let d = app.path().document_dir().map_err(|e| e.to_string())?.join(maps::USER_DATA_LEAF);
    std::fs::create_dir_all(&d).map_err(|e| e.to_string())?;
    open_folder(&d)
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
    sync_logs: bool,
    camera_height: u32,
    /// The lobby name the player chose (empty: none), and the one used then (this computer's name).
    player_name: String,
    default_player_name: String,
    version: &'static str,
    os: &'static str,
}

#[tauri::command]
fn get_config(app: tauri::AppHandle) -> Result<ConfigView, String> {
    let c = Config::load(&config_path(&app)?);
    let camera_height = c.camera();
    Ok(ConfigView {
        server: c.server,
        room: c.room,
        game_dir: c.game_dir,
        steam_user: c.steam_user,
        setup_done: c.setup_done,
        check_updates: !c.skip_update_check,
        sync_logs: c.sync_logs,
        camera_height,
        player_name: c.player_name,
        default_player_name: generals_core::config::machine_name(),
        version: update::current_version(),
        os: std::env::consts::OS,
    })
}

/// Set the name other players see in the game's lobby; an empty name goes back to this computer's name.
#[tauri::command]
fn set_player_name(app: tauri::AppHandle, name: String) -> Result<String, String> {
    let path = config_path(&app)?;
    let mut c = Config::load(&path);
    c.player_name = generals_core::config::clean_player_name(&name);
    c.save(&path).map_err(|e| e.to_string())?;
    Ok(c.lobby_name())
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

/// Where the engine writes its per-frame sync logs while recording is on.
fn sync_logs_dir(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    Ok(base(app)?.join("sync-logs"))
}

#[tauri::command]
fn set_sync_logs(app: tauri::AppHandle, on: bool) -> Result<(), String> {
    let path = config_path(&app)?;
    let mut c = Config::load(&path);
    c.sync_logs = on;
    c.save(&path).map_err(|e| e.to_string())
}

#[tauri::command]
fn open_sync_logs_folder(app: tauri::AppHandle) -> Result<(), String> {
    let d = sync_logs_dir(&app)?;
    std::fs::create_dir_all(&d).map_err(|e| e.to_string())?;
    open_folder(&d)
}

/// Everything the developer needs to look into a crash or a desync, in one zip in the Downloads folder.
#[tauri::command]
async fn make_debug_bundle(app: tauri::AppHandle, relay: tauri::State<'_, Relay>) -> Result<String, String> {
    let base = base(&app)?;
    let cfg = Config::load(&config_path(&app)?);
    let relay_text = match relay.0.lock().unwrap().as_ref() {
        Some(a) => format!("{:?}", a.status()),
        None => "not connected".to_string(),
    };
    let src = bundle::Sources {
        game_data: app.path().document_dir().map_err(|e| e.to_string())?.join(maps::USER_DATA_LEAF),
        game_dir: Some(PathBuf::from(&cfg.game_dir)).filter(|p| p.is_dir()),
        base,
        version: env!("CARGO_PKG_VERSION").to_string(),
        extra: vec![("relay".into(), relay_text)],
    };
    let out_dir = mods::downloads_dir().filter(|d| d.is_dir()).unwrap_or_else(|| src.base.clone());
    let zip = tauri::async_runtime::spawn_blocking(move || bundle::create(&src, &out_dir)).await.map_err(|e| e.to_string())??;
    // Show it in its folder.
    #[cfg(target_os = "macos")]
    let _ = std::process::Command::new("open").arg("-R").arg(&zip).spawn();
    #[cfg(windows)]
    {
        // Explorer wants `/select,"path"` as one argument with the quotes inside it: a path with a space, passed the usual
        // way, is split up and Explorer falls back to the Documents folder.
        use std::os::windows::process::CommandExt;
        let _ = std::process::Command::new("explorer").raw_arg(format!("/select,\"{}\"", zip.display().to_string().replace('/', "\\"))).spawn();
    }
    #[cfg(all(unix, not(target_os = "macos")))]
    if let Some(d) = zip.parent() { let _ = open_folder(d); }
    Ok(zip.to_string_lossy().into_owned())
}

/// Compare this machine's sync logs with another player's (a folder, or a zip of one): where did the games first differ?
#[tauri::command]
async fn sync_compare(app: tauri::AppHandle, other: String) -> Result<syncdiff::Outcome, String> {
    let (mine, work) = (sync_logs_dir(&app)?, base(&app)?.join("tmp").join("sync-compare"));
    tauri::async_runtime::spawn_blocking(move || syncdiff::compare(&mine, Path::new(other.trim()), &work)).await.map_err(|e| e.to_string())?
}

#[tauri::command]
fn set_camera_height(app: tauri::AppHandle, height: u32) -> Result<(), String> {
    let path = config_path(&app)?;
    let mut c = Config::load(&path);
    c.camera_height = height.clamp(graphics::STANDARD_CAMERA, graphics::MAX_CAMERA);
    c.save(&path).map_err(|e| e.to_string())
}

/// Put the graphics fix's quality settings into the game's `Options.ini` (the original is kept once).
#[tauri::command]
fn graphics_apply_high(app: tauri::AppHandle) -> Result<(), String> {
    graphics::apply_high_quality(&app.path().document_dir().map_err(|e| e.to_string())?.join(maps::USER_DATA_LEAF))
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
fn relay_connect(app: tauri::AppHandle, relay: tauri::State<Relay>, relay_server: tauri::State<RelayServer>, password: String) -> Result<(), String> {
    let c = Config::load(&config_path(&app)?);
    if c.server.is_empty() {
        return Err("Enter the server address first".into());
    }
    if c.room.is_empty() {
        return Err("Enter a room name first".into());
    }
    let pinned = c.server_keys.get(&c.server.to_lowercase()).and_then(|k| generals_proto::secure::from_hex(k));
    // No password typed: use the one saved for this server and room, if any.
    let password = if password.is_empty() { c.room_password(&c.server, &c.room).unwrap_or("").to_string() } else { password };
    let agent = Agent::start(&c.server, c.serial_for(&c.server), &c.room, &password, pinned).map_err(|e| e.to_string())?;
    *relay_server.0.lock().unwrap() = c.server.to_lowercase(); // what the agent was started for, whatever the settings say later
    *relay.0.lock().unwrap() = Some(agent); // dropping the old agent leaves its room
    Ok(())
}

/// Remember (or, with an empty password, forget) the room password for a saved server. The page never reads it back.
#[tauri::command]
fn room_password_save(app: tauri::AppHandle, server: String, room: String, password: String) -> Result<(), String> {
    let path = config_path(&app)?;
    let mut c = Config::load(&path);
    c.set_room_password(&server, &room, &password);
    c.save(&path).map_err(|e| e.to_string())
}

/// Which of these (server, room) pairs have a remembered password.
#[tauri::command]
fn room_passwords_saved(app: tauri::AppHandle, pairs: Vec<(String, String)>) -> Result<Vec<bool>, String> {
    let c = Config::load(&config_path(&app)?);
    Ok(pairs.iter().map(|(s, r)| c.room_password(s, r).is_some()).collect())
}

/// Where the connection stands and what to check: shown while connecting, and after the server goes quiet.
#[tauri::command]
fn relay_connection(relay: tauri::State<Relay>) -> Option<generals_core::agent::Connection> {
    relay.0.lock().unwrap().as_ref().map(|a| a.connection())
}

/// Forget the saved identity of the configured server, so the next connection trusts whatever answers (use after
/// the server's owner says its key changed).
#[tauri::command]
fn relay_forget_key(app: tauri::AppHandle) -> Result<(), String> {
    let path = config_path(&app)?;
    let mut c = Config::load(&path);
    c.server_keys.remove(&c.server.to_lowercase());
    c.save(&path).map_err(|e| e.to_string())
}

#[tauri::command]
fn relay_disconnect(relay: tauri::State<Relay>) {
    *relay.0.lock().unwrap() = None;
}

/// Latency, packet loss and the relay's own load figures; `None` unless connected.
#[tauri::command]
fn relay_diagnostics(relay: tauri::State<Relay>) -> Option<generals_core::agent::Diagnostics> {
    relay.0.lock().unwrap().as_ref().and_then(|a| a.diagnostics())
}

#[tauri::command]
fn relay_status(app: tauri::AppHandle, relay: tauri::State<Relay>, relay_server: tauri::State<RelayServer>) -> String {
    use generals_proto::Reason::*;
    let guard = relay.0.lock().unwrap();
    // First connect: the server signed us up behind the scenes; keep the result for next time.
    if let (Some(a), Ok(path)) = (guard.as_ref(), config_path(&app)) {
        let (serial, key) = (a.take_issued(), a.take_learned_key());
        if serial.is_some() || key.is_some() {
            let mut c = Config::load(&path);
            let server = relay_server.0.lock().unwrap().clone();
            if let Some(serial) = serial {
                c.set_serial(&server, serial);
            }
            // First visit to this server: remember who it is, so a different one answering later is caught.
            if let Some(key) = key {
                c.server_keys.insert(server, generals_proto::secure::to_hex(&key));
            }
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
            x if x == ServerKeyChanged as u8 => "This server's identity changed",
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
fn mods_get( watch: tauri::State<ModWatch>, id: String) -> Result<(), String> {
    let m = catalog()?.into_iter().find(|m| m.id == id).ok_or("unknown mod")?;
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
            // An add-on may be installed before or after its base; launching it is what needs both.
            let m = catalog()?.into_iter().find(|m| m.id == id).ok_or("unknown mod")?;
            m.path
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

/// Open the mod's official download page, without watching for anything: the player points to the files.
#[tauri::command]
fn open_download_page(id: String) -> Result<(), String> {
    let m = catalog()?.into_iter().find(|m| m.id == id).ok_or("unknown mod")?;
    open_url(if m.download_page.is_empty() { &m.homepage } else { &m.download_page })
}

#[tauri::command]
fn open_homepage(id: String) -> Result<(), String> {
    open_url(&catalog()?.into_iter().find(|m| m.id == id).ok_or("unknown mod")?.homepage)
}

// ---- maps -----------------------------------------------------------------------------------------

/// Where the game looks for maps: `<Documents>/Command and Conquer Generals Zero Hour Data/Maps`.
fn maps_dir(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    Ok(app.path().document_dir().map_err(|e| e.to_string())?.join(maps::USER_DATA_LEAF).join("Maps"))
}

/// Where switched-off maps wait: `<app data>/GeneralsZH/MapLibrary`.
fn map_library(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    Ok(base(app)?.join("MapLibrary"))
}

#[derive(serde::Serialize)]
struct MapsInfo {
    dir: String,
    count: usize,
    packs: Vec<maps::MapPack>,
}

#[tauri::command]
fn maps_info(app: tauri::AppHandle) -> Result<MapsInfo, String> {
    let dir = maps_dir(&app)?;
    let packs = maps::load_packs(MAP_PACKS_JSON).map_err(|e| e.to_string())?;
    Ok(MapsInfo { count: maps::count_maps(&dir), dir: dir.to_string_lossy().into_owned(), packs })
}

#[tauri::command]
fn maps_install_pack(app: tauri::AppHandle, jobs: tauri::State<MapJobs>, id: String) -> Result<(), String> {
    let pack = maps::load_packs(MAP_PACKS_JSON).map_err(|e| e.to_string())?.into_iter().find(|p| p.id == id).ok_or("unknown map pack")?;
    if pack.manual {
        return Err("This pack is downloaded from its own page: open the page, then choose the file you downloaded".into());
    }
    let job = maps::MapJob::start(pack, maps_dir(&app)?, map_library(&app)?, base(&app)?.join("tmp"));
    *jobs.0.lock().unwrap() = Some(job);
    Ok(())
}

#[tauri::command]
fn maps_status(jobs: tauri::State<MapJobs>) -> maps::MapState {
    jobs.0.lock().unwrap().as_ref().map(|j| j.state()).unwrap_or_default()
}

#[tauri::command]
fn maps_cancel(jobs: tauri::State<MapJobs>) {
    *jobs.0.lock().unwrap() = None;
}

/// Add maps from an archive or folder the user chose or dropped. `pack` is the catalog pack it belongs to, if any: a big
/// pack is added switched off, for the organiser. Anything else is added switched on under "Other maps".
#[tauri::command]
async fn maps_install_path(app: tauri::AppHandle, path: String, pack: Option<String>) -> Result<usize, String> {
    let (dir, lib, work) = (maps_dir(&app)?, map_library(&app)?, base(&app)?.join("tmp"));
    std::fs::create_dir_all(&work).map_err(|e| e.to_string())?;
    let known = match pack {
        Some(id) => Some(maps::load_packs(MAP_PACKS_JSON).map_err(|e| e.to_string())?.into_iter().find(|p| p.id == id).ok_or("unknown map pack")?),
        None => None,
    };
    let (name, enabled) = known.map(|p| (p.name, !p.start_disabled)).unwrap_or_else(|| (maporg::OTHER.to_string(), true));
    tauri::async_runtime::spawn_blocking(move || maps::install_from_path(&PathBuf::from(path.trim()), &dir, &lib, &work, &name, enabled)).await.map_err(|e| e.to_string())?
}

/// Every map, on or off, with its pack.
#[tauri::command]
async fn maps_organiser(app: tauri::AppHandle) -> Result<Vec<maporg::MapEntry>, String> {
    let (dir, lib) = (maps_dir(&app)?, map_library(&app)?);
    tauri::async_runtime::spawn_blocking(move || maporg::list(&dir, &lib)).await.map_err(|e| e.to_string())
}

#[tauri::command]
async fn maps_set_enabled(app: tauri::AppHandle, names: Vec<String>, enabled: bool) -> Result<usize, String> {
    let (dir, lib) = (maps_dir(&app)?, map_library(&app)?);
    tauri::async_runtime::spawn_blocking(move || maporg::set_enabled(&dir, &lib, &names, enabled)).await.map_err(|e| e.to_string())?
}

#[tauri::command]
async fn maps_delete(app: tauri::AppHandle, names: Vec<String>) -> Result<usize, String> {
    let (dir, lib) = (maps_dir(&app)?, map_library(&app)?);
    tauri::async_runtime::spawn_blocking(move || maporg::delete(&dir, &lib, &names)).await.map_err(|e| e.to_string())?
}

/// How many players each map is for (its start positions), read once per map and remembered.
#[tauri::command]
async fn maps_players(app: tauri::AppHandle) -> Result<std::collections::BTreeMap<String, u8>, String> {
    let (dir, lib) = (maps_dir(&app)?, map_library(&app)?);
    tauri::async_runtime::spawn_blocking(move || {
        let maps: Vec<_> = maporg::list(&dir, &lib).into_iter().map(|m| { let p = if m.enabled { dir.join(&m.name) } else { lib.join("disabled").join(&m.name) }; (m.name, p) }).collect();
        mapview::counts(&maps, &lib.join("players.json"))
    })
    .await
    .map_err(|e| e.to_string())
}

/// A map's preview picture and start positions, whether it is switched on or off.
#[tauri::command]
async fn map_view(app: tauri::AppHandle, name: String) -> Result<mapview::MapView, String> {
    if !maporg::is_safe_name(&name) {
        return Err("not a map name".into());
    }
    let (on, off) = (maps_dir(&app)?.join(&name), map_library(&app)?.join("disabled").join(&name));
    let dir = [on, off].into_iter().find(|d| d.is_dir()).ok_or("that map isn't installed")?;
    tauri::async_runtime::spawn_blocking(move || mapview::view(&name, &dir)).await.map_err(|e| e.to_string())?
}

/// Open the pack's own page (for packs that cannot be fetched automatically).
#[tauri::command]
fn maps_open_pack_page(id: String) -> Result<(), String> {
    let pack = maps::load_packs(MAP_PACKS_JSON).map_err(|e| e.to_string())?.into_iter().find(|p| p.id == id).ok_or("unknown map pack")?;
    if !pack.homepage.starts_with("https://") {
        return Err("unexpected link".into());
    }
    open_url(&pack.homepage)
}

// ---- updates --------------------------------------------------------------------------------------

/// Ask GitHub for a newer release. `None` means up to date. Errors are for the Settings button; the startup check ignores them.
#[tauri::command]
async fn update_check(updates: tauri::State<'_, Updates>) -> Result<Option<update::UpdateInfo>, String> {
    let found = tauri::async_runtime::spawn_blocking(|| update::check(update::current_version())).await.map_err(|e| e.to_string())??;
    *updates.0.lock().unwrap() = found.clone();
    Ok(found)
}

/// Download and verify the update, then replace the running app and restart it. Returns only when it could not
/// restart by itself (the installer was opened for the user instead).
#[tauri::command]
async fn update_install(app: tauri::AppHandle, updates: tauri::State<'_, Updates>) -> Result<String, String> {
    let info = updates.0.lock().unwrap().clone().ok_or("check for updates first")?;
    let data = base(&app)?;
    let (data2, info2) = (data.clone(), info.clone());
    let started = tauri::async_runtime::spawn_blocking(move || {
        let file = update::download(&info2, &update::downloads_dir())?;
        let work = data2.join("update");
        if selfupdate::apply(&file, &work)? {
            selfupdate::save_marker(&data2, &selfupdate::WhatsNew { version: info2.latest.clone(), notes: info2.notes.clone() })?;
            Ok(None)
        } else {
            update::open_installer(&file).map(Some)
        }
    })
    .await
    .map_err(|e| e.to_string())??;
    match started {
        Some(msg) => Ok(msg),
        None => {
            app.exit(0);
            Ok("Restarting…".into())
        }
    }
}

/// Release notes to show once after an update.
#[tauri::command]
fn whats_new_take(app: tauri::AppHandle) -> Result<Option<selfupdate::WhatsNew>, String> {
    Ok(selfupdate::take_marker(&base(&app)?, update::current_version()))
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
async fn icons_install(app: tauri::AppHandle) -> Result<mods::Installed, String> {
    let dir = mods_dir(&app)?;
    let work = base(&app)?.join("tmp");
    tauri::async_runtime::spawn_blocking(move || icons::install(&dir, &work)).await.map_err(|e| e.to_string())?
}

#[tauri::command]
fn launch_game(app: tauri::AppHandle, relay: tauri::State<Relay>, game_dir: PathBuf, mod_folders: Option<Vec<String>>, width: u32, height: u32, fullscreen: bool, skirmish: bool, raised_camera: Option<bool>) -> Result<(), String> {
    let base = base(&app)?;
    let data = app.path().data_dir().map_err(|e| e.to_string())?;
    let exe = engine::find(std::env::var("RALLYPOINT_ENGINE").ok().as_deref(), app.path().resource_dir().ok().as_deref(), &data)
        .ok_or("This build has no game engine for your platform yet")?;
    if !game_dir.join("INIZH.big").is_file() {
        return Err(format!("{} has no Zero Hour data (INIZH.big missing)", game_dir.display()));
    }
    let cat = catalog()?;
    let ordered = mods::with_bases(&cat, &mod_folders.unwrap_or_default());
    if let Some((a, b)) = mods::conflict(&cat, &ordered) {
        return Err(format!("{a} and {b} can't be played together. Untick one of them."));
    }
    let mut mod_args = vec![];
    for name in &ordered {
        if let Some(m) = cat.iter().find(|m| &m.path == name) {
            mod_args.extend(m.args.iter().cloned());
        }
    }
    let mod_path = mods::stack(&mods_dir(&app)?, &ordered)?.map(|p| p.to_string_lossy().to_string());
    // Connected to a server: the engine gets our virtual IP. Still connecting or rejected: don't start on the wrong network.
    let relay_cfg = launch::relay_for_launch(relay.0.lock().unwrap().as_ref().map(|a| (a.local.to_string(), a.status())))?;
    let relay_cfg_on = relay_cfg.is_some();
    let opts = launch::LaunchOptions {
        log: engine_log(&app).ok(),
        sync_logs: if Config::load(&config_path(&app)?).sync_logs {
            let dir = sync_logs_dir(&app)?;
            launch::prepare_sync_logs(&dir).map_err(|e| e.to_string())?;
            Some(dir)
        } else {
            None
        },
        width,
        height,
        fullscreen,
        mod_path,
        mod_args,
        relay: relay_cfg,
        env: {
            let mut env = vec![];
            if skirmish {
                env.push(("GEN_AUTO_SKIRMISH".to_string(), "1".to_string()));
            }
            env.push(graphics::camera_env(Config::load(&config_path(&app)?).camera()));
            // The name other players see in the lobby: the player's own, else this computer's.
            env.push(("GEN_PLAYER_NAME".to_string(), Config::load(&config_path(&app)?).lobby_name()));
            // The agent only listens to an engine that presents this (other local processes can reach its port).
            if relay_cfg_on {
                if let Some(t) = relay.0.lock().unwrap().as_ref().map(|a| a.token_hex()) {
                    env.push(("GEN_RELAY_TOKEN".to_string(), t));
                }
            }
            // Zero Hour also reads the base game's files.
            if let Some(b) = steam::base_game_for(&steam::detect(), &assets::generals_dir(&base)) {
                env.push(("GEN_GENERALS_PATH".to_string(), b.to_string_lossy().to_string()));
            }
            env
        },
        raised_camera: raised_camera.unwrap_or(false),
    };
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let _ = std::fs::set_permissions(&exe, std::fs::Permissions::from_mode(0o755)); // bundles can lose the exec bit
    }
    // Games of earlier launches are over: the agent forgets their sockets, so this one's are admitted.
    if let Some(a) = relay.0.lock().unwrap().as_ref() {
        a.new_engine();
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
        .manage(RelayServer::default())
        .manage(ModWatch::default())
        .manage(Updates::default())
        .manage(MapJobs::default())
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![
            detect_games, check_game_dir, diagnose, open_game_data_folder, assets_start, assets_status, assets_submit, assets_use_code, assets_cancel,
            get_config, save_config, set_player_name, set_camera_height, graphics_apply_high, set_sync_logs, open_sync_logs_folder, make_debug_bundle, sync_compare, save_game_dir, set_setup_done, set_check_updates,
            update_check, update_install, whats_new_take, open_release_page,
            maps_info, maps_install_pack, maps_status, maps_cancel, maps_install_path, maps_organiser, maps_players, map_view, maps_set_enabled, maps_delete, maps_open_pack_page,
            relay_connect, relay_disconnect, room_password_save, room_passwords_saved, relay_connection, relay_forget_key, relay_status, relay_diagnostics,
            mods_list, mods_get, mods_poll, downloads_dir, mods_cancel_watch, mods_install, mods_remove, open_mods_folder, open_homepage, open_download_page, icons_install,
            launch_game
        ])
        .run(tauri::generate_context!())
        .expect("tauri");
}
