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
    fn relay_sets_env() {
        let o = LaunchOptions { width: 1, height: 1, relay: Some(("127.0.0.1:5000".into(), "10.77.0.2".into())), ..Default::default() };
        let c = command(Path::new("e"), Path::new("."), &o);
        let envs: Vec<_> = c.get_envs().map(|(k, v)| (k.to_str().unwrap().to_string(), v.unwrap().to_str().unwrap().to_string())).collect();
        assert_eq!(envs, [("GEN_RELAY".to_string(), "127.0.0.1:5000".to_string()), ("GEN_RELAY_VIP".to_string(), "10.77.0.2".to_string())]);
    }
}
