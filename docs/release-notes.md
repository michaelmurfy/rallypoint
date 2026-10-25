## Rallypoint 0.1 Alpha

First public build. **Alpha:** the engine patches (widescreen/HiDPI, relay transport) have not been tested in a real game yet.

- Launcher for macOS (Apple Silicon) and Windows, with the game engine bundled in. Linux gets the launcher only.
- Skirmish: **Play** → Solo Play → Skirmish, or **Quick Skirmish** for an instant 1v1 vs AI.
- Any resolution / aspect ratio, with the camera and UI scaled instead of zoomed or stretched.
- Relay server (Docker image `ghcr.io/michaelmurfy/rallypoint-relay` or the `rallypoint-server` binary) with automatically issued client serials.
- You need your own copy of Generals – Zero Hour. See the README for game data setup, especially on macOS/Linux.
