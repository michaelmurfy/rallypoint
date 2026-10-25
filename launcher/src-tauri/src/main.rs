use generals_core::{agent::{Agent, Status}, assets::{self, AssetJob}, config::Config, engine, launch, mods, steam};
use std::path::PathBuf;
use std::sync::Mutex;
use tauri::Manager;

const MODS_JSON: &str = include_str!("../../../mods/mods.json");

#[derive(Default)]
struct Relay(Mutex<Option<Agent>>);
#[derive(Default)]
struct Downloads(Mutex<Option<AssetJob>>);

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

/// What the UI may see. The serial (a credential the user never handles) is deliberately not part of it.
#[derive(serde::Serialize)]
struct ConfigView {
    server: String,
    room: String,
    game_dir: String,
    steam_user: String,
}

#[tauri::command]
fn get_config(app: tauri::AppHandle) -> Result<ConfigView, String> {
    let c = Config::load(&config_path(&app)?);
    Ok(ConfigView { server: c.server, room: c.room, game_dir: c.game_dir, steam_user: c.steam_user })
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

/// Download the game files from Steam (macOS/Linux, or anywhere Steam isn't installed). `password` may be empty
/// if steamcmd already has a cached login.
#[tauri::command]
fn assets_start(app: tauri::AppHandle, jobs: tauri::State<Downloads>, user: String, password: String) -> Result<(), String> {
    let path = config_path(&app)?;
    let mut c = Config::load(&path);
    c.steam_user = user.trim().to_string();
    let _ = c.save(&path);
    let job = AssetJob::start(&base(&app)?, &user, password).map_err(|e| e.to_string())?;
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

/// Answer steamcmd's prompt: the password or the Steam Guard code.
#[tauri::command]
fn assets_submit(jobs: tauri::State<Downloads>, text: String) {
    if let Some(j) = jobs.0.lock().unwrap().as_ref() {
        j.submit(&text);
    }
}

#[tauri::command]
fn assets_cancel(jobs: tauri::State<Downloads>) {
    *jobs.0.lock().unwrap() = None; // dropping the job stops steamcmd
}

#[tauri::command]
fn launch_game(app: tauri::AppHandle, relay: tauri::State<Relay>, game_dir: PathBuf, mod_id: Option<String>, width: u32, height: u32, fullscreen: bool, skirmish: bool) -> Result<(), String> {
    let base = base(&app)?;
    let data = app.path().data_dir().map_err(|e| e.to_string())?;
    let exe = engine::find(std::env::var("RALLYPOINT_ENGINE").ok().as_deref(), app.path().resource_dir().ok().as_deref(), &data)
        .ok_or("This build has no game engine for your platform yet")?;
    if !game_dir.join("INIZH.big").is_file() {
        return Err(format!("{} has no Zero Hour data (INIZH.big missing)", game_dir.display()));
    }
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
        // Straight into a 1v1 skirmish against the AI; plain Play opens the full menu (Solo > Skirmish).
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
    // Remember a manually chosen folder.
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
        .invoke_handler(tauri::generate_handler![detect_games, installed_mods, launch_game, get_config, save_config, relay_connect, relay_disconnect, relay_status, assets_start, assets_status, assets_submit, assets_cancel])
        .run(tauri::generate_context!())
        .expect("tauri");
}
