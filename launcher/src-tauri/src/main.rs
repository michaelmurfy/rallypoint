use generals_core::{agent::{Agent, Status}, config::Config, launch, mods, steam};
use std::path::PathBuf;
use std::sync::Mutex;
use tauri::Manager;

const MODS_JSON: &str = include_str!("../../../mods/mods.json");

#[derive(Default)]
struct Relay(Mutex<Option<Agent>>);

fn base(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    Ok(app.path().data_dir().map_err(|e| e.to_string())?.join("GeneralsZH"))
}

#[tauri::command]
fn detect_games() -> Vec<steam::GameInstall> {
    steam::detect()
}

#[tauri::command]
fn installed_mods(app: tauri::AppHandle) -> Result<Vec<mods::Mod>, String> {
    let all = mods::load_manifest(MODS_JSON).map_err(|e| e.to_string())?;
    Ok(mods::installed(&all, &base(&app)?.join("Mods")))
}

fn config_path(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    Ok(base(app)?.join("config.json"))
}

/// The serial never leaves the backend in the clear: the UI only gets the masked form.
#[derive(serde::Serialize)]
struct ConfigView {
    server: String,
    serial_masked: String,
}

#[tauri::command]
fn get_config(app: tauri::AppHandle) -> Result<ConfigView, String> {
    let c = Config::load(&config_path(&app)?);
    Ok(ConfigView { serial_masked: c.masked_serial(), server: c.server })
}

/// An empty `serial` keeps the stored one.
#[tauri::command]
fn save_config(app: tauri::AppHandle, server: String, serial: String) -> Result<(), String> {
    let path = config_path(&app)?;
    let mut c = Config::load(&path);
    c.server = server.trim().to_string();
    if !serial.trim().is_empty() {
        c.serial = serial.trim().to_string();
    }
    c.save(&path).map_err(|e| e.to_string())
}

#[tauri::command]
fn relay_connect(app: tauri::AppHandle, relay: tauri::State<Relay>) -> Result<(), String> {
    let c = Config::load(&config_path(&app)?);
    if c.server.is_empty() {
        return Err("Set the server first".into());
    }
    let agent = Agent::start(&c.server, &c.serial).map_err(|e| e.to_string())?;
    *relay.0.lock().unwrap() = Some(agent); // dropping the old agent stops it
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
    // First connect without a serial: the server issued one, keep it for next time.
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
            x if x == UnknownSerial as u8 => "Server does not know this serial",
            x if x == Revoked as u8 => "This serial has been revoked",
            x if x == InUse as u8 => "This serial is already in use",
            x if x == Full as u8 => "Server is full",
            x if x == RegistrationClosed as u8 => "Server needs an admin-issued serial",
            x if x == RateLimited as u8 => "Too many sign-ups from your address, try later",
            _ => "Rejected by server",
        }
        .into(),
    }
}

#[tauri::command]
fn launch_game(app: tauri::AppHandle, relay: tauri::State<Relay>, game_dir: PathBuf, mod_id: Option<String>, width: u32, height: u32, fullscreen: bool) -> Result<(), String> {
    let base = base(&app)?;
    let m = match mod_id {
        Some(id) => mods::load_manifest(MODS_JSON).map_err(|e| e.to_string())?.into_iter().find(|m| m.id == id),
        None => None,
    };
    let opts = launch::LaunchOptions {
        width,
        height,
        fullscreen,
        mod_path: m.as_ref().map(|m| m.full_path(&base.join("Mods")).to_string_lossy().into()),
        mod_args: m.map(|m| m.args).unwrap_or_default(),
        // Online only once the relay has accepted us; the engine needs our virtual IP.
        relay: match relay.0.lock().unwrap().as_ref() {
            Some(a) => match a.status() {
                Status::Connected(v) => Some((a.local.to_string(), format!("{}.{}.{}.{}", v[0], v[1], v[2], v[3]))),
                _ => None,
            },
            None => None,
        },
    };
    let exe = base.join("runtime").join(if cfg!(windows) { "generalszh.exe" } else { "generalszh" });
    launch::command(&exe, &game_dir, &opts).spawn().map(|_| ()).map_err(|e| format!("{}: {e}", exe.display()))
}

fn main() {
    tauri::Builder::default()
        .manage(Relay::default())
        .invoke_handler(tauri::generate_handler![detect_games, installed_mods, launch_game, get_config, save_config, relay_connect, relay_disconnect, relay_status])
        .run(tauri::generate_context!())
        .expect("tauri");
}
