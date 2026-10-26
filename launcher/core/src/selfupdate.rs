//! Applying a downloaded update without the user doing anything: stage the new version, start a small detached
//! helper that waits for this process to exit, swaps the new version in and starts it again. A marker file lets
//! the new version show a "What's new" screen on its first start.
use crate::proc::quiet;
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct WhatsNew {
    pub version: String,
    pub notes: String,
}

fn marker(data: &Path) -> PathBuf {
    data.join("whats-new.json")
}

pub fn save_marker(data: &Path, w: &WhatsNew) -> Result<(), String> {
    std::fs::create_dir_all(data).map_err(|e| e.to_string())?;
    std::fs::write(marker(data), serde_json::to_vec(w).map_err(|e| e.to_string())?).map_err(|e| e.to_string())
}

/// The notes to show now, if an update was just applied. The marker is always removed; one left by an update that
/// never happened (version differs) is ignored.
pub fn take_marker(data: &Path, running: &str) -> Option<WhatsNew> {
    let p = marker(data);
    let w: Option<WhatsNew> = std::fs::read(&p).ok().and_then(|b| serde_json::from_slice(&b).ok());
    let _ = std::fs::remove_file(&p);
    w.filter(|w| w.version == running)
}

/// The `.app` this executable lives in (`X.app/Contents/MacOS/exe`).
pub fn app_bundle(exe: &Path) -> Option<PathBuf> {
    exe.ancestors().find(|a| a.extension().is_some_and(|e| e == "app")).map(Path::to_path_buf)
}

fn sh_quote(s: &str) -> String {
    format!("'{}'", s.replace('\'', "'\\''"))
}

fn ps_quote(s: &str) -> String {
    format!("'{}'", s.replace('\'', "''"))
}

/// Wait for the launcher to quit, swap the bundle (putting the old one back if the copy fails), and reopen it.
pub fn mac_script(pid: u32, new_app: &Path, target: &Path) -> String {
    let (n, t) = (sh_quote(&new_app.to_string_lossy()), sh_quote(&target.to_string_lossy()));
    format!(
        "#!/bin/sh\nwhile kill -0 {pid} 2>/dev/null; do sleep 0.2; done\n\
         old={t}.old\nrm -rf \"$old\"\n\
         if mv {t} \"$old\" && ditto {n} {t}; then rm -rf \"$old\"; else rm -rf {t}; mv \"$old\" {t}; fi\n\
         xattr -dr com.apple.quarantine {t} 2>/dev/null\nrm -rf {n}\nopen {t}\n"
    )
}

/// Wait for the launcher to quit, run the NSIS installer silently, and start the launcher again.
/// The installer result is written to `log` first, and the previous launcher is started even when the
/// installer fails, so a failed update still leaves the app open.
pub fn win_script(pid: u32, setup: &Path, exe: &Path, log: &Path) -> String {
    let (setup, exe, log) = (ps_quote(&setup.to_string_lossy()), ps_quote(&exe.to_string_lossy()), ps_quote(&log.to_string_lossy()));
    format!(
        "$log = {log}\n\
         function Note([string]$m) {{ Add-Content -LiteralPath $log -Value $m }}\n\
         Wait-Process -Id {pid} -ErrorAction SilentlyContinue\n\
         Start-Sleep -Milliseconds 400\n\
         try {{\n\
           $p = Start-Process -FilePath {setup} -ArgumentList '/S' -Wait -PassThru\n\
           Note \"installer $($p.ExitCode)\"\n\
         }} catch {{\n\
           Note $_.Exception.Message\n\
         }}\n\
         Start-Process -FilePath {exe}\n"
    )
}

#[cfg(target_os = "macos")]
fn writable(dir: &Path) -> bool {
    let probe = dir.join(format!(".rallypoint-write-test-{}", std::process::id()));
    std::fs::write(&probe, b"").map(|_| std::fs::remove_file(&probe)).is_ok()
}

/// Copy the app out of the disk image into `work`, then eject the image.
#[cfg(target_os = "macos")]
fn stage_mac(dmg: &Path, work: &Path) -> Result<PathBuf, String> {
    let mount = work.join("mnt");
    let stage = work.join("stage");
    let _ = std::fs::remove_dir_all(&stage);
    std::fs::create_dir_all(&mount).map_err(|e| e.to_string())?;
    std::fs::create_dir_all(&stage).map_err(|e| e.to_string())?;
    let attach = quiet(Command::new("hdiutil").args(["attach", "-nobrowse", "-readonly", "-noverify", "-mountpoint"]).arg(&mount).arg(dmg))
        .output()
        .map_err(|e| e.to_string())?;
    if !attach.status.success() {
        return Err(format!("couldn't open the update: {}", String::from_utf8_lossy(&attach.stderr).trim()));
    }
    let result = (|| {
        let app = std::fs::read_dir(&mount)
            .map_err(|e| e.to_string())?
            .flatten()
            .map(|e| e.path())
            .find(|p| p.extension().is_some_and(|e| e == "app"))
            .ok_or("the update has no app in it")?;
        let dest = stage.join(app.file_name().unwrap());
        let ok = quiet(Command::new("ditto").arg(&app).arg(&dest)).status().map_err(|e| e.to_string())?.success();
        if !ok {
            return Err("couldn't copy the update".to_string());
        }
        let sealed = quiet(Command::new("codesign").args(["--verify", "--deep", "--strict"]).arg(&dest)).status().map_err(|e| e.to_string())?.success();
        if !sealed {
            return Err("the update's signature is broken".to_string());
        }
        Ok(dest)
    })();
    let _ = quiet(Command::new("hdiutil").args(["detach", "-force"]).arg(&mount)).status();
    result
}

fn spawn_detached(mut cmd: Command) -> Result<(), String> {
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        // DETACHED_PROCESS makes PowerShell exit before -File runs, so the launcher would close and never
        // come back. CREATE_NO_WINDOW hides the console. Breakaway lets the helper outlive this process
        // when Windows has put us in a job; if the job forbids it, try again without that flag.
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        const CREATE_NEW_PROCESS_GROUP: u32 = 0x0000_0200;
        const CREATE_BREAKAWAY_FROM_JOB: u32 = 0x0100_0000;
        cmd.creation_flags(CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB);
        cmd.stdin(std::process::Stdio::null()).stdout(std::process::Stdio::null()).stderr(std::process::Stdio::null());
        if cmd.spawn().is_ok() {
            return Ok(());
        }
        cmd.creation_flags(CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP);
    }
    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        cmd.process_group(0); // outlives us
    }
    cmd.stdin(std::process::Stdio::null()).stdout(std::process::Stdio::null()).stderr(std::process::Stdio::null());
    cmd.spawn().map(|_| ()).map_err(|e| e.to_string())
}

/// Stage the downloaded installer and start the helper. `Ok(true)`: the caller must exit now so the helper can swap
/// the app in; `Ok(false)`: not possible here (e.g. an app in a read-only folder) and nothing was started.
pub fn apply(installer: &Path, work: &Path) -> Result<bool, String> {
    let pid = std::process::id();
    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    std::fs::create_dir_all(work).map_err(|e| e.to_string())?;
    #[cfg(target_os = "macos")]
    {
        let Some(target) = app_bundle(&exe) else { return Ok(false) };
        if !target.parent().is_some_and(writable) {
            return Ok(false);
        }
        let new_app = stage_mac(installer, work)?;
        let script = work.join("apply-update.sh");
        std::fs::write(&script, mac_script(pid, &new_app, &target)).map_err(|e| e.to_string())?;
        let mut c = Command::new("/bin/sh");
        c.arg(&script);
        spawn_detached(c)?;
        return Ok(true);
    }
    #[cfg(windows)]
    {
        let script = work.join("apply-update.ps1");
        let log = work.join("apply-update.log");
        std::fs::write(&script, win_script(pid, installer, &exe, &log)).map_err(|e| e.to_string())?;
        let mut c = Command::new("powershell");
        c.args(["-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File"]).arg(&script);
        spawn_detached(c)?;
        return Ok(true);
    }
    #[allow(unreachable_code)]
    {
        let _ = (installer, pid, exe);
        Ok(false)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn finds_the_app_bundle() {
        assert_eq!(app_bundle(Path::new("/Applications/Rallypoint.app/Contents/MacOS/rallypoint")).unwrap(), Path::new("/Applications/Rallypoint.app"));
        assert!(app_bundle(Path::new("/usr/local/bin/rallypoint")).is_none());
    }

    #[test]
    fn scripts_quote_awkward_paths() {
        let s = mac_script(42, Path::new("/t/it's/New.app"), Path::new("/Apps/Rally point.app"));
        assert!(s.contains("kill -0 42") && s.contains("'/t/it'\\''s/New.app'") && s.contains("'/Apps/Rally point.app'"));
        assert!(s.contains("open '/Apps/Rally point.app'"));
        let w = win_script(7, Path::new("C:\\Users\\O'Neil\\setup.exe"), Path::new("C:\\App\\r.exe"), Path::new("C:\\App\\apply-update.log"));
        assert!(w.contains("-Id 7") && w.contains("'C:\\Users\\O''Neil\\setup.exe'") && w.contains("'/S'"));
        assert!(w.contains("'C:\\App\\apply-update.log'") && w.contains("Start-Process -FilePath 'C:\\App\\r.exe'"));
    }

    #[test]
    fn whats_new_marker_shows_once_and_only_for_the_running_version() {
        let d = std::env::temp_dir().join(format!("gen-selfupd-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        let w = WhatsNew { version: "0.3.0".into(), notes: "things".into() };
        save_marker(&d, &w).unwrap();
        assert_eq!(take_marker(&d, "0.3.0"), Some(w.clone()));
        assert_eq!(take_marker(&d, "0.3.0"), None, "shown once");
        save_marker(&d, &w).unwrap();
        assert_eq!(take_marker(&d, "0.2.0"), None, "stale marker from an update that didn't happen");
        assert!(!marker(&d).exists());
        let _ = std::fs::remove_dir_all(&d);
    }

    /// The flag combination that used to be passed (CREATE_NO_WINDOW | DETACHED_PROCESS) starts PowerShell and
    /// then exits before -File runs. This fails if that regression comes back.
    #[cfg(windows)]
    #[test]
    fn hidden_helper_runs_the_script() {
        let dir = std::env::temp_dir().join(format!("gen-selfupd-helper-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let marker = dir.join("ran.txt");
        let script = dir.join("helper.ps1");
        std::fs::write(&script, format!("Set-Content -LiteralPath '{}' -Value ran\n", marker.display())).unwrap();
        let mut c = Command::new("powershell");
        c.args(["-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File"]).arg(&script);
        spawn_detached(c).unwrap();
        let ran = (0..300).any(|_| { // up to 30 s: PowerShell can take several seconds to start on a busy CI runner
            if marker.is_file() {
                return true;
            }
            std::thread::sleep(std::time::Duration::from_millis(100));
            false
        });
        let _ = std::fs::remove_dir_all(&dir);
        assert!(ran, "the helper exited before the script ran");
    }
}
