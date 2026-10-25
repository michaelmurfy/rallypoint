# Agent guide

Rallypoint: cross-platform (macOS, Windows, Linux) launcher and multiplayer stack for C&C Generals / Zero Hour. User-facing docs are in `README.md`; this file is for contributors and AI agents.

## Layout
- `engine/` - git submodule: https://github.com/dvcdsys/GeneralsGameCode-macOS (fork of TheSuperHackers/GeneralsGameCode, itself from EA's GPL release). Don't edit in place; engine changes are `patches/*.patch`.
- `patches/` - `sh patches/apply.sh` applies them to `engine/` (idempotent). Regenerate: edit the engine tree, `git -C engine add -N <new files> && git -C engine diff -- <paths> > patches/NNNN-name.patch`, then `git -C engine reset` + checkout, and re-apply. `0001-widescreen.patch` = hor+ camera, uniform/centred 2D UI, terrain draw window on all platforms (`GEN_STRETCH=1` restores retail). `0002-relay-transport.patch` = the `UDP` class tunnels through the agent when `GEN_RELAY`/`GEN_RELAY_VIP` are set and drops datagrams not from the agent; `LANAPI::SetLocalIP` uses the virtual IP. Wire code is `RelayWire.h`. Both build in CI on macOS and Windows; neither has been played in a real game.
- `tests/` - `sh tests/run.sh` applies patches and runs the standalone engine-math tests (plain `c++`, no engine build); `relaywire_test` checks the same golden bytes as the Rust protocol tests.
- `launcher/core/` - GUI-free Rust crate, all logic and tests: `steam.rs` (find installs by `.big` files), `assets.rs` (steamcmd download of both games, prompt/progress parsing), `engine.rs` (find the bundled engine), `mods.rs`, `launch.rs` (engine command line/env), `config.rs` (`<app data>/GeneralsZH/config.json`, 0600), `agent.rs` (tunnel agent).
- `launcher/src-tauri/` - thin Tauri 2 shell exposing core as commands; `launcher/ui/` is plain HTML/CSS/JS (`app.js`, strict CSP, no `innerHTML`). The engine is bundled from `launcher/src-tauri/engine/` (CI copies the built engine in).
- `mods/mods.json` - mod manifest. A mod is a folder under `<app data>/GeneralsZH/Mods/` loaded with `-mod`.
- `shared/` (`generals-proto`) - UDP wire protocol and `room_verifier`. `server/` (`rallypoint-server`, Linux) - `Server` state machine in `lib.rs` (no sockets or clocks; all tests there), `geo.rs` (country filter), `main.rs` (env config, CLI, UDP loop).
- `Makefile`, `.env.example`, `docker-compose.yml`, `server/Dockerfile` - running the server. `scripts/smoke_relay.py` - full client handshake used by CI.
- `.github/workflows/ci.yml` - fast tests only, on push/PR. `release.yml` - engine + launcher bundles + server image, only on `v*` tags or manual run (manual never publishes).

## Commands
- `make test` (= `cargo test -p generals-core -p generals-proto -p generals-server` + `sh tests/run.sh`) before every commit.
- `cargo check -p generals-launcher` - needs webkit2gtk dev packages on Linux.
- `cd launcher/src-tauri && cargo tauri build` - needs the engine copied into `launcher/src-tauri/engine/`.
- Release: `make release VERSION=0.1.0-alpha.1` (tag must match `tauri.conf.json` version's numeric core). Dry run: Actions > Release > Run workflow.
- Clone with `git clone --recurse-submodules --depth 1`; the engine repo is large.
- No cmake/Docker is assumed locally; engine builds are verified through the release workflow.

## Server design
- Handshake: [`Register`(zero cookie) -> `Challenge` -> `Register`(cookie) -> `Issued`] on first run, then `Hello{serial, room, verifier}` -> `Welcome{vip, token}`. After that every client packet is `Signed{token, ...}`. Unsigned or wrongly signed packets are ignored (wrong token gets no reply at all).
- Serials are identities issued automatically and kept out of the UI. The challenge cookie (HMAC-like SHA-256 over a server secret + source IP + minute) stops spoofed sign-ups; the Challenge is no bigger than the request (no reflection). 5 sign-ups per IP per hour. `GENERALS_REGISTRATION=closed` makes the server admin-issue only (`rallypoint-server issue <label>`).
- Rooms: created by the first `Hello` for a name with its `verifier` (iterated SHA-256 of room+password, so the password never travels or is stored), removed when empty (explicit `Leave`, timeout 30 s, serial revoked). Max 256 rooms x 8 players; 10 wrong passwords per IP per 10 minutes. Broadcast/unicast never cross rooms; each room has its own `10.77.0.x` space.
- Country limit: `ALLOWED_COUNTRIES` + zone files in `GEO_DIR` (`make geo` downloads ipdeny.com aggregated lists). Fails closed (listed country without a file allows nobody); private ranges pass unless `ALLOW_PRIVATE=false`. Blocked sources get no reply.
- Not encrypted: an on-path attacker can read/replay traffic and the verifier. Server->client packets are unsigned (off-path spoofing needs the client's ephemeral port). The agent's loopback socket trusts any local process (same-user threat model).

## Rules
- Put logic in `launcher/core` or `server` lib with a test; keep the Tauri shell and UI thin.
- Detect games by `.big` files (`INIZH.big` = Zero Hour, `INI.big` = Generals). Steam app IDs (Generals 2229870, Zero Hour 2732960) are used only by the steamcmd download.
- Never redistribute EA assets, Miles or Bink. The user's own Steam copy supplies game data. Never store or log the Steam password; it only goes to steamcmd's stdin.
- Don't invent mod download URLs; manifests link to the mod's homepage only.
- UI: no `innerHTML` with backend data (folder names are attacker-controlled).
- Server loop must never die on I/O errors (no `?` inside `run`'s loop).
- No new dependencies when std or serde does it (sha2 is the exception: don't hand-roll crypto).

## Roadmap
1. Verify in a real game: widescreen/HiDPI (0001), two-machine play through the relay (0002), Steam download with a real account, Windows engine data lookup (cwd vs registry).
2. Linux engine. GeneralsX (https://github.com/fbraz3/GeneralsX) already targets Linux x86_64 (DXVK/Vulkan, SDL3, OpenAL) from the same TheSuperHackers base; the likely route is basing the Linux build on it rather than porting our own renderer. It publishes no discrete fix list, so cherry-picking means reading its commit history for the specific area (macOS/net/rendering fixes) and porting by hand.
3. Determinism (macOS arm64 vs Windows x86 desync): `-ffp-contract=off`, deterministic trig, RNG audit; headless engine on the server as a per-frame CRC referee.
4. Gameplay features, all engine work in a lockstep game, none started:
   - Save/load multiplayer: needs the save to be written identically on every client and a "load" handshake that restores all clients to the same frame (the game's save format is not network-aware).
   - Controlling allied units: command validation (`Player`/`Object` ownership checks in command processing) needs an "allowed controller" relation that is identical on every client; AI-owned allies and shared vision/economy rules need decisions.
   - Co-op / shared player: several humans issuing commands for one `Player` slot; commands from different clients for the same player must be ordered deterministically (the net layer already orders commands per frame), and the lobby needs a shared slot concept. Related to the item above.
5. Mod downloads inside the launcher; the relay as a public server list.
