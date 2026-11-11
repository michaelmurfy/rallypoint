//! Locating the game engine binary. Release builds bundle it in the app's resources.
use std::path::{Path, PathBuf};

pub const EXE: &str = if cfg!(windows) { "generalszh.exe" } else { "generalszh" };

/// First existing of: explicit override, the bundled 64-bit engine `<resources>/engine/x64/` (Windows only, unless
/// `x86` asks for the 32-bit one), bundled `<resources>/engine/`, and the engine launcher's install at
/// `<data>/GeneralsZH/runtime/`.
pub fn find(override_path: Option<&str>, resources: Option<&Path>, data: &Path, x86: bool) -> Option<PathBuf> {
    find_with(override_path, resources, data, cfg!(windows) && !x86)
}

/// Both engines are bundled, so the Settings switch means something.
pub fn both_bundled(resources: Option<&Path>) -> bool {
    resources.is_some_and(|r| r.join("engine").join("x64").join(EXE).is_file() && r.join("engine").join(EXE).is_file())
}

fn find_with(override_path: Option<&str>, resources: Option<&Path>, data: &Path, prefer_x64: bool) -> Option<PathBuf> {
    override_path
        .map(PathBuf::from)
        .into_iter()
        .chain(resources.filter(|_| prefer_x64).map(|r| r.join("engine").join("x64").join(EXE)))
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
        assert_eq!(find(None, Some(&res), &data, false), None);
        std::fs::write(data.join("GeneralsZH/runtime").join(EXE), "").unwrap();
        assert_eq!(find(None, Some(&res), &data, false), Some(data.join("GeneralsZH/runtime").join(EXE)));
        std::fs::write(res.join("engine").join(EXE), "").unwrap();
        assert_eq!(find(None, Some(&res), &data, false), Some(res.join("engine").join(EXE)));
        let custom = t.join("custom");
        std::fs::write(&custom, "").unwrap();
        assert_eq!(find(custom.to_str(), Some(&res), &data, false), Some(custom));
        assert_eq!(find(Some("/nonexistent/x"), Some(&res), &data, false), Some(res.join("engine").join(EXE)));
        std::fs::remove_dir_all(t).unwrap();
    }

    #[test]
    fn prefers_the_bundled_64_bit_engine_when_asked() {
        let t = std::env::temp_dir().join(format!("gen-eng64-{}", std::process::id()));
        let (res, data) = (t.join("res"), t.join("data"));
        std::fs::create_dir_all(res.join("engine/x64")).unwrap();
        std::fs::write(res.join("engine").join(EXE), "").unwrap();
        assert_eq!(find_with(None, Some(&res), &data, true), Some(res.join("engine").join(EXE)));
        std::fs::write(res.join("engine/x64").join(EXE), "").unwrap();
        assert_eq!(find_with(None, Some(&res), &data, true), Some(res.join("engine/x64").join(EXE)));
        assert_eq!(find_with(None, Some(&res), &data, false), Some(res.join("engine").join(EXE)));
        assert!(both_bundled(Some(&res)));
        std::fs::remove_dir_all(t).unwrap();
    }
}
