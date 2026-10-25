//! Mod manifests. A mod is a folder of `.big`/INI data under the mods dir, loaded with `-mod`.
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, Deserialize, Serialize, PartialEq)]
pub struct Mod {
    pub id: String,
    pub name: String,
    pub homepage: String,
    /// Folder (or .big) name inside the mods dir.
    pub path: String,
    #[serde(default)]
    pub args: Vec<String>,
}

pub fn load_manifest(json: &str) -> Result<Vec<Mod>, serde_json::Error> {
    serde_json::from_str(json)
}

/// Mods from the manifest whose files are present in `mods_dir`.
pub fn installed(all: &[Mod], mods_dir: &Path) -> Vec<Mod> {
    all.iter().filter(|m| mods_dir.join(&m.path).exists()).cloned().collect()
}

impl Mod {
    pub fn full_path(&self, mods_dir: &Path) -> PathBuf {
        mods_dir.join(&self.path)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bundled_manifest_parses() {
        let m = load_manifest(include_str!("../../../mods/mods.json")).unwrap();
        assert!(m.iter().any(|m| m.id == "shockwave") && m.iter().any(|m| m.id == "chaos"));
    }

    #[test]
    fn installed_filters_by_presence() {
        let t = std::env::temp_dir().join(format!("gen-mods-{}", std::process::id()));
        std::fs::create_dir_all(t.join("Shockwave")).unwrap();
        let all = load_manifest(include_str!("../../../mods/mods.json")).unwrap();
        let got = installed(&all, &t);
        assert_eq!(got.len(), 1);
        assert_eq!(got[0].id, "shockwave");
        std::fs::remove_dir_all(t).unwrap();
    }
}
