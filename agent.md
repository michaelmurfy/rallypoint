# Agent guide

Cross-platform (macOS, Windows, Linux) launcher and multiplayer stack for C&C Generals / Zero Hour.

## Layout
- `engine/` - git submodule: https://github.com/dvcdsys/GeneralsGameCode-macOS (fork of TheSuperHackers/GeneralsGameCode, itself from EA's GPL release). Don't edit in place; engine changes go in `patches/` (planned) or our own fork of it.
- `launcher/core/` - GUI-free Rust crate: Steam detection (`steam.rs`), mod manifests (`mods.rs`), engine command line (`launch.rs`). All logic and tests live here.
- `launcher/src-tauri/` - thin Tauri 2 shell exposing core as commands. `launcher/ui/` is plain HTML/JS, no build step.
- `mods/mods.json` - mod manifest (Shockwave, Chaos Mod). A mod is a folder under `<app data>/GeneralsZH/Mods/` loaded with `-mod`.
- `server/`, `shared/`, `patches/` - not created yet (see roadmap).

## Commands
- `cargo test -p generals-core` - fast, run before every commit. CI runs it on Linux/macOS/Windows.
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
2. Engine patches: any resolution/HiDPI, 2D UI scaling, zoom by aspect ratio.
3. `server/` (Linux Rust): UDP relay emulating LAN broadcast + lobby; `shared/` protocol crate.
   - **Serial required to connect:** each client has a unique serial; the handshake carries it and the server rejects unknown, revoked or already-in-use serials. Open design points: who issues serials (server admin CLI vs. launcher self-registration) and whether it is bound to a machine. Store it in the launcher config; never log it in full.
4. Determinism: `-ffp-contract=off`, deterministic trig, RNG audit; headless engine on the server as a per-frame CRC referee that pinpoints the first diverging object (incl. AI).
5. Linux engine build, packaging, engine auto-install in the launcher.

## Known gaps
- Launcher expects the engine at `<app data>/GeneralsZH/z_generals`; no installer for it yet.
- Steam ships Windows depots only; macOS/Linux users need the files via steamcmd (`+@sSteamCmdForcePlatformType windows`) or a manual path.
- Mod `.exe`-patching mods will not work on the ported engine.
