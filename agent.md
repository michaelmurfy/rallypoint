# Agent guide

Cross-platform (macOS, Windows, Linux) launcher and multiplayer stack for C&C Generals / Zero Hour.

## Layout
- `engine/` - git submodule: https://github.com/dvcdsys/GeneralsGameCode-macOS (fork of TheSuperHackers/GeneralsGameCode, itself from EA's GPL release). Don't edit in place; engine changes go in `patches/` (planned) or our own fork of it.
- `launcher/core/` - GUI-free Rust crate: Steam detection (`steam.rs`), mod manifests (`mods.rs`), engine command line (`launch.rs`). All logic and tests live here.
- `launcher/src-tauri/` - thin Tauri 2 shell exposing core as commands. `launcher/ui/` is plain HTML/JS, no build step.
- `mods/mods.json` - mod manifest (Shockwave, Chaos Mod). A mod is a folder under `<app data>/GeneralsZH/Mods/` loaded with `-mod`.
- `patches/` - engine changes as `*.patch` files; `sh patches/apply.sh` applies them to `engine/` (idempotent). Regenerate with `git -C engine add -N <new files> && git -C engine diff > patches/NNNN-name.patch`, then reset the engine tree. `0001-widescreen.patch` = hor+ camera, uniform/centred 2D UI, terrain draw window on all platforms; `GEN_STRETCH=1` restores retail behaviour.
- `tests/` - `sh tests/run.sh` applies patches and runs the standalone engine-math test (plain `c++`, no engine build).
- `shared/` (`generals-proto`) - UDP wire protocol: Hello(serial)/Welcome(virtual IP)/Reject/Bcast/Data/Ping.
- `server/` (`generals-server`, Linux) - `Server` state machine in `lib.rs` (no sockets/clocks, all tests there); `main.rs` is the CLI + UDP loop. `generals-server [--serials F] [--bind A] run | issue [label] | revoke SERIAL | list`. Serial file is `SERIAL [label]` per line, `!` prefix = revoked, reloaded on change (revoking kicks live sessions). Never commit it (gitignored).

## Commands
- `cargo test -p generals-core -p generals-proto -p generals-server` - fast, run before every commit. CI runs it on Linux/macOS/Windows.
- `cargo check -p generals-launcher` - needs webkit2gtk dev packages on Linux.
- `cd launcher/src-tauri && cargo tauri build` - bundle the app.
- Engine (macOS): see `engine/README.md`; `cmake --preset apple-arm64`, target `z_generals`.
- Clone with `git clone --recurse-submodules --depth 1`; the engine repo is large.

## Rules
- Put logic in `launcher/core` with a test; keep the Tauri shell and UI thin.
- Detect games by `.big` files (`INIZH.big` = Zero Hour, `INI.big` = Generals), never by Steam app ID.
- Never redistribute EA assets, Miles or Bink. The user's own Steam copy supplies game data.
- Don't invent mod download URLs; manifests link to the mod's homepage only.
- No new dependencies when std or serde does it.

## Roadmap
1. (done) Launcher MVP: detect, mod select, launch.
2. (patch written, NOT compiled or run in game) Resolution/HiDPI, 2D UI scaling, zoom by aspect ratio. Upstream already accepts any `-xres/-yres` and drops the 4:3 filter; Windows is PerMonitorV2 DPI aware; macOS renders to a Metal drawable of the requested pixel size, so HiDPI = launcher passes native pixels. Still to do: build and eyeball on 16:9, 21:9, 4K; options-menu resolution list; screens that position by display width (ProcessAnimateWindow, control-bar extras); optional lobby cap on FOV.
3. `server/` (Linux Rust): UDP relay emulating LAN broadcast + lobby; `shared/` protocol crate.
   - (server + protocol done and tested over real UDP; **client side not done**.) Still missing: a launcher tunnel agent that captures the engine's LAN discovery broadcast and game UDP and forwards it via the relay, an engine/net change so peers are addressed by virtual IP (10.77.0.x) through the relay (or a TUN device, which needs privileges), rooms/lobby, serial storage in launcher config, deployment (systemd/Docker).
   - **Serial required to connect:** each client has a unique serial; the handshake carries it and the server rejects unknown, revoked or already-in-use serials. Decided: the server admin issues serials via a CLI, one per client, not bound to a machine. Store it in the launcher config; never log it in full.
4. Determinism: `-ffp-contract=off`, deterministic trig, RNG audit; headless engine on the server as a per-frame CRC referee that pinpoints the first diverging object (incl. AI).
5. Linux engine build, packaging, engine auto-install in the launcher.

## Known gaps
- Launcher expects the engine at `<app data>/GeneralsZH/runtime/generalszh` (the engine launcher's layout); no installer for it yet.
- Steam ships Windows depots only; macOS/Linux users need the files via steamcmd (`+@sSteamCmdForcePlatformType windows`) or a manual path.
- Mod `.exe`-patching mods will not work on the ported engine.
