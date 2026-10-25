## Rallypoint 0.1 Alpha 2

**Alpha:** the engine patches (widescreen/HiDPI, online play) and the Steam download have not been tested in a real game or with a real Steam account yet.

New since alpha 1:
- **Steam Guard fix.** Downloading the game files now signs in through a short non-interactive step, asks for your Steam Guard code (or waits for approval in the Steam Mobile app), and retries. A "Details" panel shows recent output if something goes wrong.
- **New app icon** and logo.
- MIT license for the launcher and server; the engine and our engine patches stay GPL-3.0 (see the README).

Everything from alpha 1:
- Launcher for macOS (Apple Silicon) and Windows, with the game engine bundled in. Linux gets the launcher only.
- Skirmish: **Play** → Solo Play → Skirmish, or **Quick Skirmish** for an instant 1v1 vs AI.
- Any resolution / aspect ratio, with the camera and UI scaled instead of zoomed or stretched.
- Online play through your own server with rooms and passwords: Docker image `ghcr.io/michaelmurfy/rallypoint-relay` or the `rallypoint-server` binary. `make up` starts it; `.env` can limit it to certain countries.
- You need your own copy of Generals – Zero Hour on Steam. See the README.
