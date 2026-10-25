use generals_core::{launch, mods, steam};
use std::path::PathBuf;
use tauri::Manager;

const MODS_JSON: &str = include_str!("../../../mods/mods.json");

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

#[tauri::command]
fn launch_game(app: tauri::AppHandle, game_dir: PathBuf, mod_id: Option<String>, width: u32, height: u32, fullscreen: bool) -> Result<(), String> {
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
    };
    let exe = base.join("runtime").join(if cfg!(windows) { "generalszh.exe" } else { "generalszh" });
    launch::command(&exe, &game_dir, &opts).spawn().map(|_| ()).map_err(|e| format!("{}: {e}", exe.display()))
}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![detect_games, installed_mods, launch_game])
        .run(tauri::generate_context!())
        .expect("tauri");
}
