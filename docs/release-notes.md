## Rallypoint 0.1 Alpha 3

**Alpha:** the engine patches (widescreen/HiDPI, online play), the Steam download and the new mod installer have not been tested in a real game or with a real Steam account yet.

New since alpha 2:
- **New launcher look and a first-run setup wizard** that walks you through game files, display, mods and online play. Sidebar pages for Play, Mods, Online and Settings; light and dark themes.
- **No more Rosetta.** The Steam download now uses a native downloader (DepotDownloader, pinned and checksum-verified), so it works on Apple Silicon without Rosetta 2. Steam Guard codes and retries are handled.
- **Easy mods.** Press **Get** on a mod, download it in your browser, and Rallypoint installs it automatically; or drop any `.zip`, `.7z`, `.rar`, folder or installer `.exe` on the window. Archives are checked before extracting. The README explains mods that ship as an `.exe`.
- **Mod list:** C&C: Shockwave, and its add-on Shockwave Chaos (needs Shockwave; combined when you launch it).

From earlier alphas:
- Launcher for macOS (Apple Silicon) and Windows with the game engine bundled in; Linux gets the launcher only.
- Skirmish: **Play** → Solo Play → Skirmish, or **Quick Skirmish** for an instant 1v1 vs AI.
- Any resolution / aspect ratio, with the camera and UI scaled instead of zoomed or stretched.
- Online play through your own server with rooms and passwords: Docker image `ghcr.io/michaelmurfy/rallypoint-relay` or the `rallypoint-server` binary. `make up` starts it; `.env` can limit it to certain countries.
- You need your own copy of Generals – Zero Hour on Steam. See the README.
