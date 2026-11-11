//! Graphics settings from <https://github.com/trailblazingfive/cnc-generals-graphics-fix>: the quality
//! options it puts in `Options.ini`, and the longer camera it sets with `MaxCameraHeight` in `GameData.ini`.
//! The engine ignores `MaxCameraHeight` in `Options.ini`, so the camera goes through an environment
//! variable (`GEN_MAX_CAMERA_HEIGHT`, engine patch 0008) instead of editing the game's own files.
use std::path::Path;

/// Retail camera limit; the fix raises it to 510.
pub const STANDARD_CAMERA: u32 = 300;
pub const FAR_CAMERA: u32 = 510;
pub const MAX_CAMERA: u32 = 1500;

/// The fix's `Options.ini` quality settings. Volumes, scroll speed, resolution and network addresses
/// are the player's own and are never touched.
pub const HIGH_QUALITY: [(&str, &str); 14] = [
    ("AntiAliasing", "3"),
    ("BuildingOcclusion", "yes"),
    ("DynamicLOD", "yes"),
    ("ExtraAnimations", "yes"),
    ("HeatEffects", "yes"),
    ("IdealStaticGameLOD", "High"),
    ("MaxParticleCount", "5000"),
    ("ShowSoftWaterEdge", "yes"),
    ("ShowTrees", "yes"),
    ("StaticGameLOD", "Custom"),
    ("TextureReduction", "0"),
    ("UseCloudMap", "yes"),
    ("UseLightMap", "yes"),
    ("UseShadowDecals", "yes"),
];
/// Shadows are listed apart only to keep the table above readable.
const SHADOWS: (&str, &str) = ("UseShadowVolumes", "yes");

/// `Options.ini` text with `set` applied: existing keys are changed in place (case-insensitive),
/// missing ones are added at the end, and every other line, comment and the line ending stays as it was.
pub fn merge_options(text: &str, set: &[(&str, &str)]) -> String {
    let nl = if text.contains("\r\n") { "\r\n" } else { "\n" };
    let mut done = vec![false; set.len()];
    let mut lines: Vec<String> = text
        .lines()
        .map(|l| {
            if let Some((k, _)) = l.split_once('=') {
                if let Some(i) = set.iter().position(|(key, _)| key.eq_ignore_ascii_case(k.trim())) {
                    done[i] = true;
                    return format!("{} = {}", set[i].0, set[i].1);
                }
            }
            l.to_string()
        })
        .collect();
    for (i, (k, v)) in set.iter().enumerate() {
        if !done[i] {
            lines.push(format!("{k} = {v}"));
        }
    }
    lines.join(nl) + nl
}

/// Apply the high-quality settings to `<data dir>/Options.ini`, keeping a one-time copy of the original.
pub fn apply_high_quality(data_dir: &Path) -> Result<(), String> {
    std::fs::create_dir_all(data_dir).map_err(|e| e.to_string())?;
    let path = data_dir.join("Options.ini");
    let old = std::fs::read_to_string(&path).unwrap_or_default();
    let backup = data_dir.join("Options.ini.rallypoint-backup");
    if !old.is_empty() && !backup.exists() {
        std::fs::write(&backup, &old).map_err(|e| e.to_string())?;
    }
    let mut set = HIGH_QUALITY.to_vec();
    set.push(SHADOWS);
    std::fs::write(&path, merge_options(&old, &set)).map_err(|e| e.to_string())
}

/// The environment variable that gives the engine its camera limit.
pub fn camera_env(height: u32) -> (String, String) {
    ("GEN_MAX_CAMERA_HEIGHT".into(), height.clamp(STANDARD_CAMERA, MAX_CAMERA).to_string())
}

/// The engine's renderer: "enhanced" runs the native Vulkan renderer with the added effects (`GENERALS_GFX=vulkan`). It
/// exists only in the 64-bit Windows engine; anything else (and every other engine) keeps the classic Direct3D 8 path.
pub fn renderer_env(renderer: &str, engine: &std::path::Path) -> Option<(String, String)> {
    let x64 = engine.parent().and_then(|d| d.file_name()).is_some_and(|n| n == "x64");
    (renderer == "enhanced" && x64).then(|| ("GENERALS_GFX".into(), "vulkan".into()))
}

/// The Enhanced renderer's effect settings the launcher offers: (Options.ini key, allowed values, default). The first
/// value of a yes/no setting is "yes". These are the engine's own defaults, so an unset key changes nothing.
pub const EFFECTS: &[(&str, &[&str], &str)] = &[
    ("PostProcessing", &["yes", "no"], "yes"),
    ("Bloom", &["yes", "no"], "yes"),
    ("AmbientOcclusion", &["yes", "no"], "yes"),
    ("AmbientOcclusionQuality", &["half", "full"], "half"),
    ("AntiAliasingFXAA", &["yes", "no"], "yes"),
    ("SoftShadows", &["yes", "no"], "yes"),
    ("ShadowQuality", &["low", "medium", "high"], "medium"),
    ("DynamicLights", &["yes", "no"], "yes"),
    ("PixelLighting", &["yes", "no"], "yes"),
    ("AnisotropicFiltering", &["0", "4", "8", "16"], "16"),
    ("CloudShadows", &["yes", "no"], "yes"),
    ("Clouds", &["yes", "no"], "no"),
    ("UpdatedWater", &["yes", "no"], "yes"),
    ("WaterReflections", &["yes", "no"], "yes"),
    ("Shockwaves", &["yes", "no"], "yes"),
    ("ScorchRelief", &["yes", "no"], "yes"),
];

/// Checks one effect setting; `Err` names what is wrong.
pub fn check_effect(key: &str, value: &str) -> Result<(), String> {
    match EFFECTS.iter().find(|(k, _, _)| *k == key) {
        Some((_, allowed, _)) if allowed.contains(&value) => Ok(()),
        Some(_) => Err(format!("{value} is not a value for {key}")),
        None => Err(format!("unknown effect setting {key}")),
    }
}

/// `GEN_GFX` for the engine: every known setting the player changed, as `Key=value;Key=value` (they win over Options.ini).
/// Unknown keys or values in the config are skipped, so nothing else reaches the engine's environment.
pub fn effects_env(settings: &std::collections::BTreeMap<String, String>) -> Option<(String, String)> {
    let v: Vec<String> = EFFECTS
        .iter()
        .filter_map(|(k, _, _)| settings.get(*k).filter(|v| check_effect(k, v).is_ok()).map(|v| format!("{k}={v}")))
        .collect();
    (!v.is_empty()).then(|| ("GEN_GFX".into(), v.join(";")))
}

/// `GEN_ASSETS` for the Enhanced renderer: the engine bundle's `assets` folder (the generated scorch atlas and its height map),
/// unless the player turned the scorch craters off or the folder is not there.
pub fn assets_env(engine: &std::path::Path, effects: &std::collections::BTreeMap<String, String>) -> Option<(String, String)> {
    let dir = engine.parent()?.join("assets");
    let off = effects.get("ScorchRelief").is_some_and(|v| v == "no");
    (!off && dir.is_dir()).then(|| ("GEN_ASSETS".into(), dir.to_string_lossy().into_owned()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn assets_only_with_the_folder_and_craters_on() {
        let t = std::env::temp_dir().join(format!("gen-assets-{}", std::process::id()));
        let exe = t.join("x64").join("generalszh.exe");
        let mut fx = std::collections::BTreeMap::new();
        assert_eq!(assets_env(&exe, &fx), None);
        std::fs::create_dir_all(t.join("x64/assets")).unwrap();
        assert_eq!(assets_env(&exe, &fx).map(|e| e.0), Some("GEN_ASSETS".to_string()));
        fx.insert("ScorchRelief".to_string(), "no".to_string());
        assert_eq!(assets_env(&exe, &fx), None);
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn effect_settings_are_checked_and_joined() {
        assert!(check_effect("Bloom", "no").is_ok());
        assert!(check_effect("ShadowQuality", "ultra").is_err());
        assert!(check_effect("Bloom;X", "no").is_err());
        let mut m = std::collections::BTreeMap::new();
        assert_eq!(effects_env(&m), None);
        m.insert("Bloom".to_string(), "no".to_string());
        m.insert("ShadowQuality".to_string(), "high".to_string());
        m.insert("Evil".to_string(), "x;y=z".to_string());
        m.insert("Shockwaves".to_string(), "maybe".to_string());
        assert_eq!(effects_env(&m), Some(("GEN_GFX".into(), "Bloom=no;ShadowQuality=high".into())));
    }

    #[test]
    fn enhanced_renderer_only_with_the_64_bit_engine() {
        let x64 = std::path::Path::new("/app/engine/x64/generalszh.exe");
        let x86 = std::path::Path::new("/app/engine/generalszh.exe");
        assert_eq!(renderer_env("enhanced", x64), Some(("GENERALS_GFX".into(), "vulkan".into())));
        assert_eq!(renderer_env("enhanced", x86), None);
        assert_eq!(renderer_env("", x64), None);
        assert_eq!(renderer_env("classic", x64), None);
    }

    #[test]
    fn merge_changes_in_place_adds_missing_and_keeps_the_rest() {
        let old = "; mine\r\nMusicVolume = 0\r\nshowtrees = no\r\nResolution = 1920 1080\r\n";
        let out = merge_options(old, &[("ShowTrees", "yes"), ("HeatEffects", "yes")]);
        assert_eq!(out, "; mine\r\nMusicVolume = 0\r\nShowTrees = yes\r\nResolution = 1920 1080\r\nHeatEffects = yes\r\n");
    }

    #[test]
    fn high_quality_never_touches_player_settings() {
        for (k, _) in HIGH_QUALITY.iter().chain([&SHADOWS]) {
            for mine in ["MusicVolume", "SFXVolume", "VoiceVolume", "Resolution", "IPAddress", "GameSpyIPAddress", "ScrollFactor", "Gamma", "MaxCameraHeight"] {
                assert!(!k.eq_ignore_ascii_case(mine), "{k}");
            }
        }
    }

    #[test]
    fn applies_to_a_fresh_folder_and_backs_up_once() {
        let t = std::env::temp_dir().join(format!("gfx-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&t);
        std::fs::create_dir_all(&t).unwrap();
        std::fs::write(t.join("Options.ini"), "MusicVolume = 3\nShowTrees = no\n").unwrap();
        apply_high_quality(&t).unwrap();
        apply_high_quality(&t).unwrap();
        let got = std::fs::read_to_string(t.join("Options.ini")).unwrap();
        assert!(got.contains("MusicVolume = 3") && got.contains("ShowTrees = yes") && got.contains("UseShadowVolumes = yes"));
        assert_eq!(got.matches("ShowTrees").count(), 1);
        assert_eq!(std::fs::read_to_string(t.join("Options.ini.rallypoint-backup")).unwrap(), "MusicVolume = 3\nShowTrees = no\n");
        let _ = std::fs::remove_dir_all(&t);
    }

    #[test]
    fn camera_is_clamped() {
        assert_eq!(camera_env(0).1, "300");
        assert_eq!(camera_env(510).1, "510");
        assert_eq!(camera_env(99999).1, "1500");
    }
}
