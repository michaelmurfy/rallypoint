//! Finding where two players' games went out of sync. With sync logging on, the engine writes one
//! `DebugFrame_NNNNNN.txt` per game frame (what changed in the simulation that frame, in a form that is
//! identical on every machine as long as the simulation is). Comparing two players' folders finds the first
//! frame and line where they differ, which names the object, field or calculation that diverged.
use crate::extract;
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct Divergence {
    pub frame: u32,
    /// 1-based line within that frame's file.
    pub line: usize,
    pub mine: String,
    pub theirs: String,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
#[serde(tag = "kind")]
pub enum Outcome {
    /// Every logged frame both players have is identical.
    Same { frames: usize },
    Differs { frames_before: usize, at: Divergence },
    /// Nothing to compare (no logs, or no frames in common).
    Nothing { why: String },
}

/// `DebugFrame_000100.txt` files of a folder, by frame. Looks one folder down too, so an unzipped archive
/// (which usually adds a wrapper folder) works.
fn frames(dir: &Path) -> BTreeMap<u32, PathBuf> {
    let mut out = BTreeMap::new();
    let mut stack = vec![(dir.to_path_buf(), 0)];
    while let Some((d, depth)) = stack.pop() {
        for e in std::fs::read_dir(&d).into_iter().flatten().flatten() {
            let p = e.path();
            let name = e.file_name().to_string_lossy().to_string();
            if p.is_dir() && depth < 2 {
                stack.push((p, depth + 1));
            } else if let Some(n) = name.strip_prefix("DebugFrame_").and_then(|r| r.strip_suffix(".txt")).and_then(|n| n.parse::<u32>().ok()) {
                out.entry(n).or_insert(p);
            }
        }
    }
    out
}

/// A folder, or a `.zip` of one (unpacked safely into `work`).
fn resolve(p: &Path, work: &Path, tag: &str) -> Result<PathBuf, String> {
    if p.is_dir() {
        return Ok(p.to_path_buf());
    }
    if extract::kind_of(p).is_some() {
        let out = work.join(tag);
        let _ = std::fs::remove_dir_all(&out);
        std::fs::create_dir_all(&out).map_err(|e| e.to_string())?;
        extract::extract(p, &out)?;
        return Ok(out);
    }
    Err(format!("{} is not a folder or a zip", p.display()))
}

pub fn compare(mine: &Path, theirs: &Path, work: &Path) -> Result<Outcome, String> {
    let (a, b) = (resolve(mine, work, "mine")?, resolve(theirs, work, "theirs")?);
    let (fa, fb) = (frames(&a), frames(&b));
    if fa.is_empty() || fb.is_empty() {
        let who = if fa.is_empty() { "your" } else { "their" };
        return Ok(Outcome::Nothing { why: format!("There are no sync log files in {who} folder (they are named DebugFrame_000000.txt and so on).") });
    }
    let mut compared = 0;
    for (frame, pa) in &fa {
        let Some(pb) = fb.get(frame) else { continue };
        let (ta, tb) = (std::fs::read_to_string(pa).map_err(|e| e.to_string())?, std::fs::read_to_string(pb).map_err(|e| e.to_string())?);
        let (la, lb): (Vec<&str>, Vec<&str>) = (ta.lines().map(str::trim_end).collect(), tb.lines().map(str::trim_end).collect());
        for i in 0..la.len().max(lb.len()) {
            let (x, y) = (la.get(i).copied().unwrap_or("<no more lines>"), lb.get(i).copied().unwrap_or("<no more lines>"));
            if x != y {
                return Ok(Outcome::Differs { frames_before: compared, at: Divergence { frame: *frame, line: i + 1, mine: x.to_string(), theirs: y.to_string() } });
            }
        }
        compared += 1;
    }
    if compared == 0 {
        return Ok(Outcome::Nothing { why: "The two folders have no game frames in common. Compare logs from the same game.".into() });
    }
    Ok(Outcome::Same { frames: compared })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn dir(name: &str, frames: &[(u32, &str)]) -> PathBuf {
        let d = std::env::temp_dir().join(format!("sync-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        for (n, text) in frames {
            std::fs::write(d.join(format!("DebugFrame_{n:06}.txt")), text).unwrap();
        }
        d
    }

    #[test]
    fn finds_the_first_difference() {
        let a = dir("a", &[(0, "0:00000 x=1\n0:00001 y=2\n"), (1, "1:00000 x=1.5\n1:00001 y=2.5\n"), (2, "2:00000 z\n")]);
        let b = dir("b", &[(0, "0:00000 x=1\n0:00001 y=2\n"), (1, "1:00000 x=1.5\n1:00001 y=2.6\n"), (2, "2:00000 other\n")]);
        let work = std::env::temp_dir().join("sync-work");
        match compare(&a, &b, &work).unwrap() {
            Outcome::Differs { frames_before, at } => {
                assert_eq!(frames_before, 1);
                assert_eq!((at.frame, at.line), (1, 2));
                assert_eq!((at.mine.as_str(), at.theirs.as_str()), ("1:00001 y=2.5", "1:00001 y=2.6"));
            }
            o => panic!("{o:?}"),
        }
        assert_eq!(compare(&a, &a, &work).unwrap(), Outcome::Same { frames: 3 });
    }

    #[test]
    fn a_shorter_frame_and_missing_frames() {
        let a = dir("c", &[(5, "l1\nl2\n"), (6, "same\n")]);
        let b = dir("d", &[(5, "l1\n"), (7, "other game\n")]);
        let work = std::env::temp_dir().join("sync-work");
        match compare(&a, &b, &work).unwrap() {
            Outcome::Differs { at, .. } => assert_eq!((at.frame, at.line, at.theirs.as_str()), (5, 2, "<no more lines>")),
            o => panic!("{o:?}"),
        }
        let e = dir("e", &[(9, "x\n")]);
        assert!(matches!(compare(&a, &e, &work).unwrap(), Outcome::Nothing { .. }), "no frames in common");
        let empty = dir("f", &[]);
        assert!(matches!(compare(&empty, &a, &work).unwrap(), Outcome::Nothing { .. }));
    }

    #[test]
    fn a_wrapper_folder_from_unzipping_is_looked_through() {
        let outer = std::env::temp_dir().join(format!("sync-wrap-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&outer);
        let inner = outer.join("sync-logs");
        std::fs::create_dir_all(&inner).unwrap();
        std::fs::write(inner.join("DebugFrame_000003.txt"), "a\n").unwrap();
        let other = dir("g", &[(3, "a\n")]);
        let work = std::env::temp_dir().join("sync-work");
        assert_eq!(compare(&outer, &other, &work).unwrap(), Outcome::Same { frames: 1 });
        assert!(compare(Path::new("/definitely/not/here"), &other, &work).is_err());
    }
}
