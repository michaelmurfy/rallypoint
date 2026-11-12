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

/// The engine's renderer: "enhanced" runs the Enhanced renderer with the added effects. On Windows that is the native Vulkan
/// renderer of the 64-bit engine (`GENERALS_GFX=vulkan`; the 32-bit engine has none), on macOS the Metal backend's Enhanced
/// mode (`GENERALS_GFX=metal`). Anything else keeps the classic path.
pub fn renderer_env(renderer: &str, engine: &std::path::Path, os: &str) -> Option<(String, String)> {
    if renderer != "enhanced" {
        return None;
    }
    match os {
        "macos" => Some(("GENERALS_GFX".into(), "metal".into())),
        "windows" if engine.parent().and_then(|d| d.file_name()).is_some_and(|n| n == "x64") => Some(("GENERALS_GFX".into(), "vulkan".into())),
        _ => None,
    }
}

/// Whether this platform has an Enhanced renderer at all (the Settings switch is shown only then).
pub fn renderer_available(os: &str, both_windows_engines: bool) -> bool {
    os == "macos" || (os == "windows" && both_windows_engines)
}

/// The Enhanced renderer's effect settings the launcher offers: (Options.ini key, allowed values, default, also on macOS).
/// These are the engine's own defaults, so an unset key changes nothing. The macOS Enhanced mode (the Metal backend) has
/// a subset: post processing, bloom, ambient occlusion, FXAA, dynamic lights, the water's depth colour, shockwaves, craters.
pub const EFFECTS: &[(&str, &[&str], &str, bool)] = &[
    ("PostProcessing", &["yes", "no"], "yes", true),
    ("Bloom", &["yes", "no"], "yes", true),
    ("AmbientOcclusion", &["yes", "no"], "yes", true),
    ("AmbientOcclusionQuality", &["half", "full"], "half", false),
    ("AntiAliasingFXAA", &["yes", "no"], "yes", true),
    ("SoftShadows", &["yes", "no"], "yes", false),
    ("ShadowQuality", &["low", "medium", "high"], "medium", false),
    ("DynamicLights", &["yes", "no"], "yes", true),
    ("PixelLighting", &["yes", "no"], "yes", false),
    ("AnisotropicFiltering", &["0", "4", "8", "16"], "16", false),
    ("CloudShadows", &["yes", "no"], "yes", false),
    ("Clouds", &["yes", "no"], "no", false),
    ("UpdatedWater", &["yes", "no"], "yes", true),
    ("WaterReflections", &["yes", "no"], "yes", false),
    ("Shockwaves", &["yes", "no"], "yes", true),
    ("ScorchRelief", &["yes", "no"], "yes", true),
    // engine patch 0081, both renderers
    ("BloomStrength", &["subtle", "normal", "strong"], "normal", true),
    ("ColorLook", &["standard", "vivid", "cinematic"], "standard", true),
    ("Sharpen", &["off", "low", "high"], "off", true),
    ("Vignette", &["yes", "no"], "no", true),
    ("SoftParticles", &["yes", "no"], "yes", true),
    ("SmokeShading", &["yes", "no"], "yes", true),
];

/// Every effect setting this platform shows, with the player's value or the default (what the Settings page lists).
pub fn effects_view(settings: &std::collections::BTreeMap<String, String>, mac_only: bool) -> Vec<(String, String)> {
    EFFECTS
        .iter()
        .filter(|e| !mac_only || e.3)
        .map(|(k, _, d, _)| (k.to_string(), settings.get(*k).filter(|v| check_effect(k, v).is_ok()).cloned().unwrap_or_else(|| d.to_string())))
        .collect()
}

/// Effect presets: (name, the settings that differ from the defaults). "balanced" is every default.
pub const PRESETS: &[(&str, &[(&str, &str)])] = &[
    (
        "performance",
        &[
            ("BloomStrength", "subtle"),
            ("AmbientOcclusion", "no"),
            ("SoftShadows", "no"),
            ("ShadowQuality", "low"),
            ("DynamicLights", "no"),
            ("PixelLighting", "no"),
            ("AnisotropicFiltering", "4"),
            ("CloudShadows", "no"),
            ("WaterReflections", "no"),
            ("SmokeShading", "no"),
        ],
    ),
    ("balanced", &[]),
    (
        "cinematic",
        &[
            ("BloomStrength", "strong"),
            ("ColorLook", "cinematic"),
            ("Sharpen", "low"),
            ("Vignette", "yes"),
            ("ShadowQuality", "high"),
            ("AmbientOcclusionQuality", "full"),
        ],
    ),
];

/// The settings to store for a preset (only what differs from the defaults); `None` for an unknown name.
pub fn preset(name: &str) -> Option<std::collections::BTreeMap<String, String>> {
    let (_, set) = PRESETS.iter().find(|(n, _)| *n == name)?;
    Some(set.iter().map(|(k, v)| (k.to_string(), v.to_string())).collect())
}

/// Which preset the stored settings amount to, or "custom". A stored value equal to the default counts as unset,
/// and settings this platform does not show (`mac_only`: the macOS subset) are left out of the comparison.
pub fn current_preset(settings: &std::collections::BTreeMap<String, String>, mac_only: bool) -> &'static str {
    let value = |set: &dyn Fn(&str) -> Option<String>| -> Vec<String> {
        EFFECTS.iter().filter(|e| !mac_only || e.3).map(|(k, _, d, _)| set(k).unwrap_or_else(|| d.to_string())).collect()
    };
    let mine = value(&|k| settings.get(k).filter(|v| check_effect(k, v).is_ok()).cloned());
    PRESETS
        .iter()
        .find(|(_, set)| value(&|k| set.iter().find(|(key, _)| *key == k).map(|(_, v)| v.to_string())) == mine)
        .map_or("custom", |(n, _)| *n)
}

/// Checks one effect setting; `Err` names what is wrong.
pub fn check_effect(key: &str, value: &str) -> Result<(), String> {
    match EFFECTS.iter().find(|(k, _, _, _)| *k == key) {
        Some((_, allowed, _, _)) if allowed.contains(&value) => Ok(()),
        Some(_) => Err(format!("{value} is not a value for {key}")),
        None => Err(format!("unknown effect setting {key}")),
    }
}

/// `GEN_GFX` for the engine: every known setting the player changed, as `Key=value;Key=value` (they win over Options.ini).
/// Unknown keys or values in the config are skipped, so nothing else reaches the engine's environment.
pub fn effects_env(settings: &std::collections::BTreeMap<String, String>) -> Option<(String, String)> {
    let v: Vec<String> = EFFECTS
        .iter()
        .filter_map(|(k, _, _, _)| settings.get(*k).filter(|v| check_effect(k, v).is_ok()).map(|v| format!("{k}={v}")))
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
    fn presets_use_known_settings_and_are_recognised() {
        for (name, set) in PRESETS {
            for (k, v) in *set {
                check_effect(k, v).unwrap_or_else(|e| panic!("{name}: {e}"));
                let default = EFFECTS.iter().find(|e| e.0 == *k).unwrap().2;
                assert_ne!(*v, default, "{name}: {k} is the default, leave it out");
            }
            let stored = preset(name).unwrap();
            assert_eq!(current_preset(&stored, false), *name);
            assert_eq!(current_preset(&stored, true), *name, "{name} must differ from the others on macOS too");
        }
        assert!(preset("ultra").is_none());
        let shown = effects_view(&preset("cinematic").unwrap(), true);
        assert!(shown.contains(&("ColorLook".into(), "cinematic".into())) && shown.iter().all(|(k, _)| k != "ShadowQuality"));
        assert_eq!(effects_view(&preset("cinematic").unwrap(), false).len(), EFFECTS.len());
        let mut m = std::collections::BTreeMap::new();
        assert_eq!(current_preset(&m, false), "balanced");
        m.insert("Bloom".to_string(), "yes".to_string());
        assert_eq!(current_preset(&m, false), "balanced", "a stored default is still the default");
        m.insert("Vignette".to_string(), "yes".to_string());
        assert_eq!(current_preset(&m, false), "custom");
    }

    #[test]
    fn new_look_settings_reach_the_engine() {
        let e = effects_env(&preset("cinematic").unwrap()).unwrap().1;
        assert_eq!(e, "AmbientOcclusionQuality=full;ShadowQuality=high;BloomStrength=strong;ColorLook=cinematic;Sharpen=low;Vignette=yes");
        assert!(check_effect("Sharpen", "max").is_err() && check_effect("SmokeShading", "no").is_ok());
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
    fn enhanced_renderer_is_vulkan_on_64_bit_windows_and_metal_on_macos() {
        let x64 = std::path::Path::new("/app/engine/x64/generalszh.exe");
        let x86 = std::path::Path::new("/app/engine/generalszh.exe");
        assert_eq!(renderer_env("enhanced", x64, "windows"), Some(("GENERALS_GFX".into(), "vulkan".into())));
        assert_eq!(renderer_env("enhanced", x86, "windows"), None);
        assert_eq!(renderer_env("", x64, "windows"), None);
        assert_eq!(renderer_env("classic", x64, "windows"), None);
        let mac = std::path::Path::new("/Applications/Rallypoint.app/Contents/Resources/engine/generalszh");
        assert_eq!(renderer_env("enhanced", mac, "macos"), Some(("GENERALS_GFX".into(), "metal".into())));
        assert_eq!(renderer_env("classic", mac, "macos"), None);
        assert_eq!(renderer_env("enhanced", mac, "linux"), None);
        assert!(renderer_available("macos", false) && renderer_available("windows", true) && !renderer_available("windows", false));
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
