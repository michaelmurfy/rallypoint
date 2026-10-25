//! Starting helper programs from a GUI app.
use std::process::Command;

/// On Windows a GUI program that starts a console program (curl, tar, 7-Zip, the downloader) gets a flashing black
/// console window for each one unless told not to. No effect elsewhere.
pub fn quiet(cmd: &mut Command) -> &mut Command {
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        cmd.creation_flags(CREATE_NO_WINDOW);
    }
    cmd
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn leaves_the_command_usable() {
        let mut c = Command::new("echo");
        quiet(&mut c).arg("hi");
        assert_eq!(c.get_args().count(), 1);
    }
}
