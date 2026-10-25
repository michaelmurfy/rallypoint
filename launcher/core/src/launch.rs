//! Build the engine command line. Resolution flags are the engine's existing `-xres/-yres`;
//! arbitrary-size and zoom support land with the Phase 2 engine patches.
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
    a
}

/// Runs the engine from the game dir so it finds the `.big` files.
pub fn command(engine: &Path, game_dir: &Path, o: &LaunchOptions) -> Command {
    let mut c = Command::new(engine);
    c.current_dir(game_dir).args(args(o));
    c.envs(o.env.iter().map(|(k, v)| (k, v)));
    // The macOS engine only accepts resolutions it advertises; naming the chosen one makes any size (4K, ultrawide,
    // a custom value) valid. Other platforms ignore it.
    if o.width >= 640 && o.height >= 480 {
        c.env("GEN_EXTRA_RES", format!("{}x{}", o.width, o.height));
    }
    if let Some((agent, vip)) = &o.relay {
        c.env("GEN_RELAY", agent).env("GEN_RELAY_VIP", vip);
    }
    c
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builds_args_with_mod() {
        let o = LaunchOptions { width: 3840, height: 1600, mod_path: Some("/m/Shockwave".into()), mod_args: vec!["-noshellmap".into()], ..Default::default() };
        assert_eq!(args(&o), ["-win", "-xres", "3840", "-yres", "1600", "-mod", "/m/Shockwave", "-noshellmap"]);
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
    fn relay_sets_env() {
        let o = LaunchOptions { width: 1, height: 1, relay: Some(("127.0.0.1:5000".into(), "10.77.0.2".into())), ..Default::default() };
        let c = command(Path::new("e"), Path::new("."), &o);
        let envs: Vec<_> = c.get_envs().map(|(k, v)| (k.to_str().unwrap().to_string(), v.unwrap().to_str().unwrap().to_string())).collect();
        assert_eq!(envs, [("GEN_RELAY".to_string(), "127.0.0.1:5000".to_string()), ("GEN_RELAY_VIP".to_string(), "10.77.0.2".to_string())]);
    }
}
