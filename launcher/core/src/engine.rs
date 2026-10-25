//! Locating the game engine binary. Release builds bundle it in the app's resources.
use std::path::{Path, PathBuf};

pub const EXE: &str = if cfg!(windows) { "generalszh.exe" } else { "generalszh" };

/// First existing of: explicit override, bundled `<resources>/engine/`, and the engine launcher's
/// install at `<data>/GeneralsZH/runtime/`.
pub fn find(override_path: Option<&str>, resources: Option<&Path>, data: &Path) -> Option<PathBuf> {
    override_path
        .map(PathBuf::from)
        .into_iter()
        .chain(resources.map(|r| r.join("engine").join(EXE)))
        .chain([data.join("GeneralsZH/runtime").join(EXE)])
        .find(|p| p.is_file())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn prefers_override_then_bundled_then_installed() {
        let t = std::env::temp_dir().join(format!("gen-eng-{}", std::process::id()));
        let (res, data) = (t.join("res"), t.join("data"));
        std::fs::create_dir_all(res.join("engine")).unwrap();
        std::fs::create_dir_all(data.join("GeneralsZH/runtime")).unwrap();
        assert_eq!(find(None, Some(&res), &data), None);
        std::fs::write(data.join("GeneralsZH/runtime").join(EXE), "").unwrap();
        assert_eq!(find(None, Some(&res), &data), Some(data.join("GeneralsZH/runtime").join(EXE)));
        std::fs::write(res.join("engine").join(EXE), "").unwrap();
        assert_eq!(find(None, Some(&res), &data), Some(res.join("engine").join(EXE)));
        let custom = t.join("custom");
        std::fs::write(&custom, "").unwrap();
        assert_eq!(find(custom.to_str(), Some(&res), &data), Some(custom));
        assert_eq!(find(Some("/nonexistent/x"), Some(&res), &data), Some(res.join("engine").join(EXE)));
        std::fs::remove_dir_all(t).unwrap();
    }
}
