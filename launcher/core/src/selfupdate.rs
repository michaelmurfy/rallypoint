//! Applying a downloaded update without the user doing anything: stage the new version, start a small detached
//! helper that waits for this process to exit, swaps the new version in and starts it again. A marker file lets
//! the new version show a "What's new" screen on its first start.
#[cfg(any(target_os = "macos", test))]
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
    // /D= installs into the folder this app runs from. Without it NSIS takes the folder from a registry key that is named
    // after the publisher, which 0.9.0 changed: an app installed anywhere but the default folder was "updated" into the
    // default one and kept running (and offering the update) from the old one.
    let dir = ps_quote(&format!("/D={}", exe.parent().unwrap_or(exe).to_string_lossy()));
    let (setup, exe, log) = (ps_quote(&setup.to_string_lossy()), ps_quote(&exe.to_string_lossy()), ps_quote(&log.to_string_lossy()));
    format!(
        "$log = {log}\n\
         function Note([string]$m) {{ Add-Content -LiteralPath $log -Value $m }}\n\
         Wait-Process -Id {pid} -ErrorAction SilentlyContinue\n\
         Start-Sleep -Milliseconds 400\n\
         try {{\n\
           $p = Start-Process -FilePath {setup} -ArgumentList '/S',{dir} -Wait -PassThru\n\
           Note \"installer $($p.ExitCode)\"\n\
         }} catch {{\n\
           Note $_.Exception.Message\n\
         }}\n\
         Start-Process -FilePath {exe}\n"
    )
}

#[cfg(any(target_os = "macos", target_os = "linux"))]
fn writable(dir: &Path) -> bool {
    let probe = dir.join(format!(".rallypoint-write-test-{}", std::process::id()));
    std::fs::write(&probe, b"").map(|_| std::fs::remove_file(&probe)).is_ok()
}

/// The prefix a Linux install lives in: the launcher is at `<prefix>/bin/generals-launcher`, so the prefix is the
/// folder above `bin` (`~/.local` for the CachyOS and Arch install, `/usr` for a `.deb` or `.rpm`).
pub fn linux_prefix(exe: &Path) -> Option<PathBuf> {
    let bin = exe.parent()?;
    if bin.file_name()? != "bin" {
        return None;
    }
    let prefix = bin.parent()?;
    if prefix.as_os_str().is_empty() {
        return None;
    }
    Some(prefix.to_path_buf())
}

/// Unpack the update into `work` and return the folder that holds `bin/`, `lib/` and `share/`.
///
/// The Linux tarball is the `.deb`'s `/usr` tree, and `scripts/package-linux.sh` packs it as
/// `rallypoint-<version>/{bin,lib,share}`, so the folder with the launcher in it is the archive's top folder. A
/// tarball that keeps the `usr` folder is found as well. An archive with a link or a path that escapes its folder is
/// refused before anything is written.
#[cfg(unix)]
pub fn stage_linux(tarball: &Path, work: &Path) -> Result<PathBuf, String> {
    if let Some(link) = crate::extract::find_link(tarball)? {
        return Err(format!("the update contains a symbolic link ({link}), so it won't be installed"));
    }
    let stage = work.join("stage");
    let _ = std::fs::remove_dir_all(&stage);
    std::fs::create_dir_all(&stage).map_err(|e| e.to_string())?;
    // the same unpacker the mods use: it refuses unsafe paths and links, and checks the finished tree
    crate::extract::extract(tarball, &stage)?;
    let mut found = None;
    for e in std::fs::read_dir(&stage).map_err(|e| e.to_string())? {
        let e = e.map_err(|e| e.to_string())?;
        if !e.file_type().map(|t| t.is_dir()).unwrap_or(false) {
            continue;
        }
        let dir = e.path();
        for cand in [dir.join("usr"), dir] {
            if cand.join("bin").join("generals-launcher").is_file() {
                found = Some(cand);
                break;
            }
        }
        if found.is_some() {
            break;
        }
    }
    found.ok_or_else(|| "the update has no launcher in it".to_string())
}

/// The script that swaps a Linux install over: it waits for the launcher to quit, puts the new files in place (the
/// old ones aside, so a failure puts them back), and starts the new launcher.
pub fn linux_script(pid: u32, staged_root: &Path, prefix: &Path, exe: &Path, log: &Path) -> String {
    let q = |p: &Path| sh_quote(&p.to_string_lossy());
    let launcher = format!("{}/bin/generals-launcher", prefix.to_string_lossy().trim_end_matches('/'));
    // the path goes into a sed expression, where \ & and the delimiter mean something
    let sed_exec = launcher.replace('\\', "\\\\").replace('&', "\\&").replace('|', "\\|");
    format!(
        "#!/bin/sh\n\
         # Written by Rallypoint: wait for the app to quit, swap the new version in, start it again.\n\
         LOG={log}\nSTAGED={staged}\nPREFIX={prefix}\nEXE={exe}\nSEDEXEC={sedexec}\n\
         note() {{ printf '%s\\n' \"$1\" >>\"$LOG\" 2>/dev/null; }}\n\
         fail() {{\n\
         \x20 note \"failed: $1\"\n\
         \x20 rm -rf \"$PREFIX/lib/Rallypoint.new\" \"$PREFIX/bin/generals-launcher.new\"\n\
         \x20 if [ -e \"$PREFIX/lib/Rallypoint.old\" ]; then rm -rf \"$PREFIX/lib/Rallypoint\"; mv \"$PREFIX/lib/Rallypoint.old\" \"$PREFIX/lib/Rallypoint\"; fi\n\
         \x20 if [ -e \"$PREFIX/bin/generals-launcher.old\" ]; then mv \"$PREFIX/bin/generals-launcher.old\" \"$PREFIX/bin/generals-launcher\"; fi\n\
         \x20 exit 1\n\
         }}\n\
         while kill -0 {pid} 2>/dev/null; do sleep 0.2; done\n\
         rm -rf \"$PREFIX/lib/Rallypoint.old\" \"$PREFIX/bin/generals-launcher.old\" \"$PREFIX/lib/Rallypoint.new\" \"$PREFIX/bin/generals-launcher.new\"\n\
         mkdir -p \"$PREFIX/bin\" \"$PREFIX/lib\" || fail \"cannot write to $PREFIX\"\n\
         if [ -d \"$STAGED/lib/Rallypoint\" ]; then\n\
         \x20 if [ -e \"$PREFIX/lib/Rallypoint\" ]; then mv \"$PREFIX/lib/Rallypoint\" \"$PREFIX/lib/Rallypoint.old\" || fail \"cannot move the old engine aside\"; fi\n\
         \x20 cp -a \"$STAGED/lib/Rallypoint\" \"$PREFIX/lib/Rallypoint.new\" || fail \"cannot copy the new engine\"\n\
         \x20 mv \"$PREFIX/lib/Rallypoint.new\" \"$PREFIX/lib/Rallypoint\" || fail \"cannot install the new engine\"\n\
         fi\n\
         if [ -f \"$STAGED/bin/generals-launcher\" ]; then\n\
         \x20 if [ -e \"$PREFIX/bin/generals-launcher\" ]; then mv \"$PREFIX/bin/generals-launcher\" \"$PREFIX/bin/generals-launcher.old\" || fail \"cannot move the old launcher aside\"; fi\n\
         \x20 cp -a \"$STAGED/bin/generals-launcher\" \"$PREFIX/bin/generals-launcher.new\" || fail \"cannot copy the new launcher\"\n\
         \x20 mv \"$PREFIX/bin/generals-launcher.new\" \"$PREFIX/bin/generals-launcher\" || fail \"cannot install the new launcher\"\n\
         \x20 chmod 755 \"$PREFIX/bin/generals-launcher\" || fail \"cannot make the launcher runnable\"\n\
         fi\n\
         if [ -d \"$STAGED/share\" ]; then\n\
         \x20 mkdir -p \"$PREFIX/share\" || fail \"cannot write to $PREFIX/share\"\n\
         \x20 cp -R \"$STAGED/share/.\" \"$PREFIX/share/\" || fail \"cannot install the desktop files\"\n\
         \x20 DESKTOP=\"$PREFIX/share/applications/Rallypoint.desktop\"\n\
         \x20 if [ -f \"$DESKTOP\" ]; then sed \"s|^Exec=.*|Exec=$SEDEXEC|\" \"$DESKTOP\" > \"$DESKTOP.new\" && mv \"$DESKTOP.new\" \"$DESKTOP\"; fi\n\
         fi\n\
         rm -rf \"$PREFIX/lib/Rallypoint.old\" \"$PREFIX/bin/generals-launcher.old\" \"$STAGED\"\n\
         note \"updated\"\n\
         \"$EXE\" >/dev/null 2>&1 &\n",
        log = q(log),
        staged = q(staged_root),
        prefix = q(prefix),
        exe = q(exe),
        sedexec = sh_quote(&sed_exec),
    )
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
    #[cfg(target_os = "linux")]
    {
        // a tarball over the prefix: the CachyOS and Arch install in ~/.local can do this itself, a system install
        // (/usr) cannot, and says so instead of failing
        if !installer.to_string_lossy().ends_with(".tar.gz") {
            return Ok(false);
        }
        let Some(prefix) = linux_prefix(&exe) else { return Ok(false) };
        if !writable(&prefix) || !writable(&prefix.join("bin")) {
            return Ok(false);
        }
        let lib = prefix.join("lib");
        if lib.exists() && !writable(&lib) {
            return Ok(false);
        }
        let staged = stage_linux(installer, work)?;
        let script = work.join("apply-update.sh");
        let log = work.join("apply-update.log");
        std::fs::write(&script, linux_script(pid, &staged, &prefix, &exe, &log)).map_err(|e| e.to_string())?;
        let mut c = Command::new("/bin/sh");
        c.arg(&script);
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
        assert!(w.contains("-Id 7") && w.contains("'C:\\Users\\O''Neil\\setup.exe'") && w.contains("'/S','/D="));
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

    // ---- Linux ----

    #[test]
    fn finds_the_linux_prefix() {
        assert_eq!(linux_prefix(Path::new("/home/u/.local/bin/generals-launcher")).unwrap(), Path::new("/home/u/.local"));
        assert_eq!(linux_prefix(Path::new("/usr/bin/generals-launcher")).unwrap(), Path::new("/usr"));
        assert!(linux_prefix(Path::new("/opt/rallypoint/generals-launcher")).is_none(), "not in a bin folder");
        assert!(linux_prefix(Path::new("/generals-launcher")).is_none());
    }

    #[test]
    fn the_linux_script_quotes_awkward_paths() {
        let s = linux_script(42, Path::new("/t/stage it's"), Path::new("/home/o'brien/.local"), Path::new("/home/o'brien/.local/bin/generals-launcher"), Path::new("/t/log file"));
        assert!(s.contains("kill -0 42"), "{s}");
        assert!(s.contains("'/t/stage it'\\''s'"), "{s}");
        assert!(s.contains("'/home/o'\\''brien/.local'"), "{s}");
        assert!(s.contains("'/t/log file'"), "{s}");
        assert!(s.contains("sed \"s|^Exec=.*|Exec=$SEDEXEC|\""), "the desktop file's Exec line is rewritten: {s}");
    }

    /// A folder under /tmp/opencode for the Linux tests.
    #[cfg(unix)]
    fn tmp(name: &str) -> PathBuf {
        let d = Path::new("/tmp/opencode").join(format!("rallypoint-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    /// A tarball with the shape of ours: `rallypoint-<version>/{bin,lib,share}` (the deb's `/usr` tree, one level
    /// up, which is what `scripts/package-linux.sh` packs). With `usr` the tree keeps the `usr` folder, which some
    /// other packer would leave in.
    #[cfg(unix)]
    fn make_tarball(t: &Path, version: &str, launcher: &str, engine: &str, usr: bool) -> PathBuf {
        use std::os::unix::fs::PermissionsExt;
        let src = t.join("src");
        let root = src.join(format!("rallypoint-{version}"));
        let root = if usr { root.join("usr") } else { root };
        std::fs::create_dir_all(root.join("bin")).unwrap();
        std::fs::create_dir_all(root.join("lib/Rallypoint/engine")).unwrap();
        std::fs::create_dir_all(root.join("share/applications")).unwrap();
        std::fs::create_dir_all(root.join("share/icons/hicolor/128x128/apps")).unwrap();
        let bin = root.join("bin/generals-launcher");
        std::fs::write(&bin, launcher).unwrap();
        std::fs::set_permissions(&bin, std::fs::Permissions::from_mode(0o755)).unwrap();
        std::fs::write(root.join("lib/Rallypoint/engine/generalszh"), engine).unwrap();
        std::fs::write(
            root.join("share/applications/Rallypoint.desktop"),
            "[Desktop Entry]\nType=Application\nName=Rallypoint\nExec=/usr/bin/generals-launcher\nIcon=Rallypoint\n",
        )
        .unwrap();
        std::fs::write(root.join("share/icons/hicolor/128x128/apps/Rallypoint.png"), "icon").unwrap();
        let tar = t.join(format!("rallypoint-{version}-linux-x86_64.tar.gz"));
        let ok = quiet(&mut Command::new("tar")).arg("-czf").arg(&tar).arg("-C").arg(&src).arg(format!("rallypoint-{version}")).status().unwrap().success();
        assert!(ok, "couldn't make the test tarball");
        tar
    }

    /// An installed prefix with an old launcher and an old engine in it.
    #[cfg(unix)]
    fn make_prefix(t: &Path) -> PathBuf {
        let prefix = t.join("home/.local");
        std::fs::create_dir_all(prefix.join("bin")).unwrap();
        std::fs::create_dir_all(prefix.join("lib/Rallypoint/engine")).unwrap();
        std::fs::write(prefix.join("bin/generals-launcher"), "old launcher").unwrap();
        std::fs::write(prefix.join("lib/Rallypoint/engine/generalszh"), "old engine").unwrap();
        prefix
    }

    /// The script that swaps the install over: it waits for the launcher to quit, puts the new files in place and
    /// starts the new launcher. It is run with a pid that is not there, so it does not wait.
    #[cfg(unix)]
    fn run_script(script: &Path) -> std::process::Output {
        Command::new("/bin/sh").arg(script).output().unwrap()
    }

    #[cfg(unix)]
    #[test]
    fn linux_script_quotes_and_swaps_and_rolls_back() {
        let t = tmp("swap");
        let prefix = make_prefix(&t);
        let staged = t.join("stage/usr");
        std::fs::create_dir_all(staged.join("bin")).unwrap();
        std::fs::create_dir_all(staged.join("lib/Rallypoint/engine")).unwrap();
        std::fs::write(staged.join("bin/generals-launcher"), "new launcher").unwrap();
        std::fs::write(staged.join("lib/Rallypoint/engine/generalszh"), "new engine").unwrap();
        let log = t.join("apply-update.log");
        let script = t.join("apply-update.sh");
        std::fs::write(&script, linux_script(u32::MAX, &staged, &prefix, &prefix.join("bin/generals-launcher"), &log)).unwrap();
        let out = run_script(&script);
        assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
        assert_eq!(std::fs::read_to_string(prefix.join("bin/generals-launcher")).unwrap(), "new launcher");
        assert_eq!(std::fs::read_to_string(prefix.join("lib/Rallypoint/engine/generalszh")).unwrap(), "new engine");
        assert!(!prefix.join("lib/Rallypoint.old").exists(), "the old engine is removed after a good update");
        assert!(!prefix.join("bin/generals-launcher.old").exists());
        assert!(std::fs::read_to_string(&log).unwrap().contains("updated"));

        // now a failure: the launcher cannot be moved aside, so the engine has to go back
        let t = tmp("rollback");
        let prefix = make_prefix(&t);
        let staged = t.join("stage/usr");
        std::fs::create_dir_all(staged.join("bin")).unwrap();
        std::fs::create_dir_all(staged.join("lib/Rallypoint/engine")).unwrap();
        std::fs::write(staged.join("bin/generals-launcher"), "new launcher").unwrap();
        std::fs::write(staged.join("lib/Rallypoint/engine/generalszh"), "new engine").unwrap();
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(prefix.join("bin"), std::fs::Permissions::from_mode(0o500)).unwrap();
        if std::fs::write(prefix.join("bin/probe"), "x").is_ok() {
            // running as root: permissions do not stop it, so this failure cannot be arranged here
            let _ = std::fs::remove_file(prefix.join("bin/probe"));
            let _ = std::fs::remove_dir_all(&t);
            return;
        }
        let log = t.join("apply-update.log");
        let script = t.join("apply-update.sh");
        std::fs::write(&script, linux_script(u32::MAX, &staged, &prefix, &prefix.join("bin/generals-launcher"), &log)).unwrap();
        let out = run_script(&script);
        assert!(!out.status.success(), "the failed update says so");
        assert_eq!(std::fs::read_to_string(prefix.join("lib/Rallypoint/engine/generalszh")).unwrap(), "old engine", "the old engine is put back");
        assert_eq!(std::fs::read_to_string(prefix.join("bin/generals-launcher")).unwrap(), "old launcher");
        assert!(std::fs::read_to_string(&log).unwrap().contains("failed"));
        std::fs::set_permissions(prefix.join("bin"), std::fs::Permissions::from_mode(0o755)).unwrap();
        let _ = std::fs::remove_dir_all(&t);
    }

    /// The whole Linux update: the archive is unpacked, the script is written and run, and the new launcher, engine
    /// and desktop file are in place with the app started again.
    #[cfg(unix)]
    #[test]
    fn the_linux_update_is_staged_and_installed() {
        let t = tmp("e2e");
        let prefix = make_prefix(&t);
        let marker = t.join("restarted");
        let launcher = format!("#!/bin/sh\necho restarted > '{}'\n", marker.display());
        let tar = make_tarball(&t, "1.0.0", &launcher, "new engine", false);
        let work = t.join("work");
        let staged = stage_linux(&tar, &work).expect("the update is unpacked");
        assert!(staged.join("bin/generals-launcher").is_file(), "the folder with the launcher is found: {}", staged.display());
        assert!(staged.join("lib/Rallypoint/engine/generalszh").is_file());
        let script = work.join("apply-update.sh");
        let log = work.join("apply-update.log");
        std::fs::write(&script, linux_script(u32::MAX, &staged, &prefix, &prefix.join("bin/generals-launcher"), &log)).unwrap();
        let out = run_script(&script);
        assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
        assert_eq!(std::fs::read_to_string(prefix.join("bin/generals-launcher")).unwrap(), launcher, "the new launcher is in place");
        assert_eq!(std::fs::read_to_string(prefix.join("lib/Rallypoint/engine/generalszh")).unwrap(), "new engine", "the new engine is in place");
        let desktop = std::fs::read_to_string(prefix.join("share/applications/Rallypoint.desktop")).unwrap();
        assert!(desktop.contains(&format!("Exec={}/bin/generals-launcher", prefix.display())), "{desktop}");
        assert!(desktop.contains("[Desktop Entry]"), "the desktop file is still a desktop file: {desktop}");
        assert!(prefix.join("share/icons/hicolor/128x128/apps/Rallypoint.png").is_file(), "the icons come too");
        assert!(!staged.exists(), "the staged files are cleaned up");
        assert!(std::fs::read_to_string(&log).unwrap().contains("updated"));
        let started = (0..50).any(|_| {
            if marker.is_file() {
                return true;
            }
            std::thread::sleep(std::time::Duration::from_millis(100));
            false
        });
        assert!(started, "the new launcher was not started again");
        let _ = std::fs::remove_dir_all(&t);
    }

    /// A tarball that keeps the `usr` folder (some packers do) is found as well.
    #[cfg(unix)]
    #[test]
    fn a_tarball_that_keeps_the_usr_folder_is_staged_as_well() {
        let t = tmp("usr");
        let tar = make_tarball(&t, "1.0.0", "launcher", "engine", true);
        let staged = stage_linux(&tar, &t.join("work")).unwrap();
        assert!(staged.join("bin/generals-launcher").is_file(), "{}", staged.display());
        assert!(staged.join("lib/Rallypoint/engine/generalszh").is_file());
        let _ = std::fs::remove_dir_all(&t);
    }

    /// An archive with a link in it is refused before anything is unpacked.
    #[cfg(unix)]
    #[test]
    fn an_update_with_a_link_in_it_is_refused() {
        let t = tmp("link");
        let src = t.join("src");
        std::fs::create_dir_all(src.join("rallypoint-1.0.0/usr/bin")).unwrap();
        std::os::unix::fs::symlink("/etc", src.join("rallypoint-1.0.0/usr/bin/escape")).unwrap();
        let tar = t.join("rallypoint-1.0.0-linux-x86_64.tar.gz");
        assert!(quiet(&mut Command::new("tar")).arg("-czf").arg(&tar).arg("-C").arg(&src).arg("rallypoint-1.0.0").status().unwrap().success());
        let err = stage_linux(&tar, &t.join("work")).unwrap_err();
        assert!(err.contains("symbolic link"), "{err}");
        assert!(!t.join("work/stage").exists() || std::fs::read_dir(t.join("work/stage")).unwrap().count() == 0, "nothing was unpacked");
        let _ = std::fs::remove_dir_all(&t);
    }
}

