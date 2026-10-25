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

#[cfg(test)]
mod tests {
    use super::*;

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
