## Rallypoint 0.1 Alpha

First public build. **Alpha:** the engine patches (widescreen/HiDPI, online play) have not been tested in a real game yet.

- Launcher for macOS (Apple Silicon) and Windows, with the game engine bundled in. Linux gets the launcher only.
- Skirmish: **Play** → Solo Play → Skirmish, or **Quick Skirmish** for an instant 1v1 vs AI.
- Download the game files from Steam from inside the launcher (macOS/Linux); Windows finds your Steam install.
- Any resolution / aspect ratio, with the camera and UI scaled instead of zoomed or stretched.
- Online play through your own server with rooms and passwords: Docker image `ghcr.io/michaelmurfy/rallypoint-relay` or the `rallypoint-server` binary. `make up` starts it; `.env` can limit it to certain countries.
- You need your own copy of Generals – Zero Hour on Steam. See the README.
