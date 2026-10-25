//! Build the engine command line. Resolution flags are the engine's existing `-xres/-yres`;
//! arbitrary-size and zoom support land with the Phase 2 engine patches.
use crate::proc::quiet;
use std::path::Path;
use std::process::Command;

#[derive(Debug, Clone, Default, serde::Deserialize)]
pub struct LaunchOptions {
    pub width: u32,
    pub height: u32,
    pub fullscreen: bool,
    /// Full path to a mod folder/.big, if any.
    pub mod_path: Option<String>,
    #[serde(default)]
    pub mod_args: Vec<String>,
    /// Tunnel agent address and our virtual IP; the patched engine reads these from the environment.
    #[serde(default)]
    pub relay: Option<(String, String)>,
    /// Extra environment for the engine (e.g. `GEN_AUTO_SKIRMISH=1`).
    #[serde(default)]
    pub env: Vec<(String, String)>,
    /// GenTool-style higher camera. Off unless the user asks, so online games keep the retail view.
    #[serde(default)]
    pub raised_camera: bool,
    /// Where to write what the engine prints (its network and error lines); replaced on every launch.
    #[serde(default)]
    pub log: Option<std::path::PathBuf>,
}

pub fn args(o: &LaunchOptions) -> Vec<String> {
    let mut a = vec![
        if o.fullscreen { "-fullscreen" } else { "-win" }.to_string(),
        "-xres".into(),
        o.width.to_string(),
        "-yres".into(),
        o.height.to_string(),
    ];
    if let Some(m) = &o.mod_path {
        a.extend(["-mod".into(), m.clone()]);
    }
    a.extend(o.mod_args.iter().cloned());
    // EA and Generals logo movies. The engine's own -nologo; the menu scene stays.
    a.push("-nologo".into());
    a
}

/// Loading picture for a mod folder. The engine otherwise paints `Install_Final.bmp` from the game
/// folder, which is Zero Hour's. Shockwave ships `Install_Final_shw.bmp` beside its archives.
pub fn splash_bitmap(mod_dir: &Path) -> Option<std::path::PathBuf> {
    for name in ["Install_Final.bmp", "Install_Final_shw.bmp"] {
        let path = mod_dir.join(name);
        if path.is_file() {
            return Some(path);
        }
    }
    None
}

/// Is the network ready for the game? No relay at all means offline play, which is fine. A relay that has
/// not accepted us yet would start the game on the wrong network (and fail with a socket error in the LAN
/// lobby), so that is refused. Returns the agent address and our virtual IP to hand to the engine.
pub fn relay_for_launch(agent: Option<(String, crate::agent::Status)>) -> Result<Option<(String, String)>, String> {
    use crate::agent::Status;
    match agent {
        None => Ok(None),
        Some((local, Status::Connected(v))) => Ok(Some((local, format!("{}.{}.{}.{}", v[0], v[1], v[2], v[3])))),
        Some((_, Status::Connecting)) => Err("Still connecting to the server. Try again in a moment.".into()),
        Some((_, Status::Rejected(_))) => Err("You are not connected to the server (see the Online tab). Fix that, or disconnect to play offline.".into()),
    }
}

/// Runs the engine from the game dir so it finds the `.big` files.
pub fn command(engine: &Path, game_dir: &Path, o: &LaunchOptions) -> Command {
    let mut c = Command::new(engine);
    quiet(&mut c).current_dir(game_dir).args(args(o)); // no stray console if the engine is a console-subsystem build
    c.envs(o.env.iter().map(|(k, v)| (k, v)));
    // The macOS engine only accepts resolutions it advertises; naming the chosen one makes any size (4K, ultrawide,
    // a custom value) valid. Other platforms ignore it.
    if o.width >= 640 && o.height >= 480 {
        c.env("GEN_EXTRA_RES", format!("{}x{}", o.width, o.height));
    }
    if let Some((agent, vip)) = &o.relay {
        c.env("GEN_RELAY", agent).env("GEN_RELAY_VIP", vip);
    }
    // Weather.ini is only in Zero Hour's archives. If the process is not started in that folder, the engine
    // reads this and loads them. A trailing separator is added on the engine side.
    c.env("GEN_ZH_PATH", game_dir);
    // The engine paints Install_Final.bmp from the game folder unless this names a mod's picture.
    if let Some(dir) = o.mod_path.as_deref() {
        if let Some(bmp) = splash_bitmap(Path::new(dir)) {
            c.env("GEN_SPLASH", bmp);
        }
    }
    if o.raised_camera {
        c.env("GEN_MAX_CAMERA_HEIGHT", "600");
        c.env("GEN_CAMERA_PITCH", "37");
    }
    if let Some(f) = o.log.as_ref().and_then(|p| open_log(p, o)) {
        if let Ok(g) = f.try_clone() {
            c.stdout(g);
        }
        c.stderr(f);
    }
    c
}

/// Fresh log file with a first line saying how the engine was started (never the room password or serial).
fn open_log(path: &Path, o: &LaunchOptions) -> Option<std::fs::File> {
    use std::io::Write;
    if let Some(d) = path.parent() {
        std::fs::create_dir_all(d).ok()?;
    }
    let mut f = std::fs::File::create(path).ok()?;
    let splash = o.mod_path.as_deref().and_then(|dir| splash_bitmap(Path::new(dir))).and_then(|p| p.file_name().map(|n| n.to_string_lossy().into_owned())).unwrap_or_else(|| "retail".into());
    let _ = writeln!(f, "[launcher] {}x{} fullscreen={} relay={} splash={}", o.width, o.height, o.fullscreen, o.relay.as_ref().map(|(a, v)| format!("agent {a}, virtual IP {v}")).unwrap_or_else(|| "off".into()), splash);
    Some(f)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builds_args_with_mod() {
        let o = LaunchOptions { width: 3840, height: 1600, mod_path: Some("/m/Shockwave".into()), mod_args: vec!["-noshellmap".into()], ..Default::default() };
        assert_eq!(args(&o), ["-win", "-xres", "3840", "-yres", "1600", "-mod", "/m/Shockwave", "-noshellmap", "-nologo"]);
    }

    #[test]
    fn extra_env_is_passed() {
        let o = LaunchOptions { env: vec![("GEN_AUTO_SKIRMISH".into(), "1".into())], ..Default::default() };
        let c = command(Path::new("e"), Path::new("."), &o);
        assert!(c.get_envs().any(|(k, v)| k == "GEN_AUTO_SKIRMISH" && v == Some("1".as_ref())));
    }

    #[test]
    fn chosen_resolution_is_named_for_the_engine() {
        let o = LaunchOptions { width: 3440, height: 1440, fullscreen: true, ..Default::default() };
        assert_eq!(args(&o)[..5], ["-fullscreen", "-xres", "3440", "-yres", "1440"]);
        let c = command(Path::new("e"), Path::new("."), &o);
        assert!(c.get_envs().any(|(k, v)| k == "GEN_EXTRA_RES" && v == Some("3440x1440".as_ref())));
        // nonsense sizes are not advertised
        let c = command(Path::new("e"), Path::new("."), &LaunchOptions { width: 10, height: 10, ..Default::default() });
        assert!(!c.get_envs().any(|(k, _)| k == "GEN_EXTRA_RES"));
    }

    #[test]
    fn engine_output_goes_to_the_log_with_a_header() {
        let t = std::env::temp_dir().join(format!("gen-log-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        let log = t.join("sub").join("engine.log");
        let o = LaunchOptions { width: 1920, height: 1080, relay: Some(("127.0.0.1:5000".into(), "10.77.0.2".into())), log: Some(log.clone()), ..Default::default() };
        let _ = command(Path::new("e"), Path::new("."), &o);
        let text = std::fs::read_to_string(&log).unwrap();
        assert_eq!(text.trim(), "[launcher] 1920x1080 fullscreen=false relay=agent 127.0.0.1:5000, virtual IP 10.77.0.2 splash=retail");
        let _ = std::fs::remove_dir_all(&t);
    }

    #[test]
    fn game_only_starts_on_a_ready_network() {
        use crate::agent::Status;
        assert_eq!(relay_for_launch(None), Ok(None));
        assert_eq!(relay_for_launch(Some(("127.0.0.1:9".into(), Status::Connected([10, 77, 0, 2])))), Ok(Some(("127.0.0.1:9".into(), "10.77.0.2".into()))));
        assert!(relay_for_launch(Some(("x".into(), Status::Connecting))).unwrap_err().contains("Still connecting"));
        assert!(relay_for_launch(Some(("x".into(), Status::Rejected(3)))).unwrap_err().contains("not connected"));
    }

    #[test]
    fn relay_sets_env() {
        let o = LaunchOptions { width: 1, height: 1, relay: Some(("127.0.0.1:5000".into(), "10.77.0.2".into())), ..Default::default() };
        let c = command(Path::new("e"), Path::new("."), &o);
        assert!(c.get_envs().any(|(k, v)| k == "GEN_RELAY" && v == Some("127.0.0.1:5000".as_ref())));
        assert!(c.get_envs().any(|(k, v)| k == "GEN_RELAY_VIP" && v == Some("10.77.0.2".as_ref())));
    }

    #[test]
    fn names_the_zero_hour_folder_and_raises_the_camera_only_when_asked() {
        let c = command(Path::new("e"), Path::new("/games/zh"), &LaunchOptions::default());
        assert!(c.get_envs().any(|(k, v)| k == "GEN_ZH_PATH" && v == Some("/games/zh".as_ref())));
        assert!(!c.get_envs().any(|(k, _)| k == "GEN_MAX_CAMERA_HEIGHT"));
        let c = command(Path::new("e"), Path::new("/games/zh"), &LaunchOptions { raised_camera: true, ..Default::default() });
        assert!(c.get_envs().any(|(k, v)| k == "GEN_MAX_CAMERA_HEIGHT" && v == Some("600".as_ref())));
        assert!(c.get_envs().any(|(k, v)| k == "GEN_CAMERA_PITCH" && v == Some("37".as_ref())));
    }

    #[test]
    fn mod_splash_is_the_mods_bitmap_and_shockwaves_name_when_that_is_what_it_ships() {
        let t = std::env::temp_dir().join(format!("gen-splash-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        std::fs::create_dir_all(&t).unwrap();
        assert!(splash_bitmap(&t).is_none(), "no picture means the engine keeps Zero Hour's");
        std::fs::write(t.join("Install_Final_shw.bmp"), b"BM").unwrap();
        assert_eq!(splash_bitmap(&t).unwrap().file_name().unwrap(), "Install_Final_shw.bmp");
        std::fs::write(t.join("Install_Final.bmp"), b"BM").unwrap();
        assert_eq!(splash_bitmap(&t).unwrap().file_name().unwrap(), "Install_Final.bmp");
        let o = LaunchOptions { mod_path: Some(t.to_string_lossy().into()), ..Default::default() };
        let c = command(Path::new("e"), Path::new("."), &o);
        let expected = t.join("Install_Final.bmp");
        assert!(c.get_envs().any(|(k, v)| k == "GEN_SPLASH" && v == Some(expected.as_os_str())));
        let plain = command(Path::new("e"), Path::new("."), &LaunchOptions::default());
        assert!(!plain.get_envs().any(|(k, _)| k == "GEN_SPLASH"));
        let _ = std::fs::remove_dir_all(&t);
    }
}
