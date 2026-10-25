# Rallypoint

A cross-platform launcher and multiplayer relay for **Command & Conquer: Generals – Zero Hour**: native on macOS and Windows, with a relay server so you can play "LAN" games over the internet without Hamachi.

> **0.1 Alpha.** Expect rough edges. See [What works](#what-works-in-01-alpha) for an honest list of what is and isn't tested.

You need your own copy of the game (for example *C&C The Ultimate Collection* on Steam). Rallypoint ships no EA game data.

## Features

- **Launcher** with Steam detection, mod selection, resolution and fullscreen options.
- **Skirmish / single player.** The game engine is bundled in the app. **Play** opens the full game (Solo Play → Skirmish); **Quick Skirmish** jumps straight into a 1v1 against the AI.
- **Any resolution and aspect ratio.** 4K, HiDPI and ultrawide are supported. The camera keeps the same world scale per pixel, so wide screens see *more* of the map instead of being zoomed in, and the 2D UI is scaled uniformly and centred instead of stretched.
- **Mods** (Shockwave, Chaos Mod) launched through the engine's `-mod` support.
- **Internet "LAN" multiplayer** through a relay server you can run with Docker. Each player gets a unique, automatically issued serial number.

## What works in 0.1 Alpha

| | macOS (Apple Silicon) | Windows | Linux |
|---|---|---|---|
| Launcher | ✅ | ✅ | ✅ (no engine yet) |
| Engine / Skirmish | ✅ upstream port | ⚠️ builds from upstream source, untested here | ❌ not built yet |
| Widescreen / HiDPI patch | ⚠️ written, not played yet | ⚠️ written, not played yet | – |
| Relay server | – | – | ✅ (Docker or binary) |
| Multiplayer through the relay | ⚠️ protocol and launcher agent tested; engine patch **not yet tried in a real game** | same | – |

Legend: ✅ works / tested, ⚠️ implemented but not verified end to end, ❌ missing.

Intel Macs are not supported (the engine port is arm64 only).

## Install

Download the installer for your platform from the [Releases](../../releases) page.

- **macOS:** open the `.dmg` and drag Rallypoint to Applications. The app is ad-hoc signed, not notarised, so Gatekeeper blocks the first launch. Right-click → **Open**, or run `xattr -dr com.apple.quarantine /Applications/Rallypoint.app`.
- **Windows:** run the `-setup.exe`. SmartScreen may warn about an unknown publisher (**More info → Run anyway**).
- **Linux:** `.deb`, `.rpm` or `.AppImage`. This only gives you the launcher for now.

### Game data

Rallypoint needs the folder that contains `INIZH.big` (Zero Hour's data).

- **Windows:** Steam installs are found automatically.
- **macOS / Linux:** Steam does not ship these games natively, so there is nothing to detect. Download the Windows files with `steamcmd` (`+@sSteamCmdForcePlatformType windows +login <you> +app_update <appid> validate`) or copy them from a Windows install, then paste the folder into **game folder** in the launcher. It remembers it.

Automatic Steam download on macOS/Linux is planned, not done.

## Playing

1. Pick your game install (or paste the folder).
2. Choose a resolution (it defaults to your screen's native pixels) and whether to run fullscreen.
3. **Play** for the full menu, or **Quick Skirmish (vs AI)** to drop straight into a match.

### Mods

Put a mod's folder (or `.big`) in the `Mods` folder inside Rallypoint's data directory and it appears in the launcher's **Mod** list:

| OS | Data directory |
|---|---|
| macOS | `~/Library/Application Support/GeneralsZH/` |
| Windows | `%APPDATA%\GeneralsZH\` |
| Linux | `~/.local/share/GeneralsZH/` |

Known mods are listed in [`mods/mods.json`](mods/mods.json) (Shockwave in `Shockwave/`, Chaos Mod in `ChaosMod/`). Downloading them from inside the launcher isn't built yet; get them from the authors. Mods that rely on a patched `.exe` will not work on the ported engine.

### Widescreen

On by default. Set `GEN_STRETCH=1` in the environment to get the original stretched UI and fixed-horizontal-FOV camera back.

## Multiplayer

Multiplayer uses a relay server instead of Hamachi: each player's launcher connects to it, the server hands out virtual LAN addresses (`10.77.0.x`), and the game's LAN lobby traffic is tunnelled through it.

### Run a server

Needs Docker and one open **UDP** port (7777 by default).

```bash
git clone https://github.com/michaelmurfy/rallypoint && cd rallypoint
docker compose up -d
```

Or use the image `ghcr.io/michaelmurfy/rallypoint-relay` from the release, or the `rallypoint-server` binary from the release page (Linux).

**Serials.** Every client needs a unique serial to connect. By default the server issues one automatically the first time a client connects (limited to 5 sign-ups per IP per hour), and the launcher stores it. For a private server, switch to admin-issued serials:

```yaml
# docker-compose.yml
environment:
  GENERALS_REGISTRATION: closed
```

```bash
docker compose exec relay rallypoint-server issue alice   # prints a serial to give to alice
docker compose exec relay rallypoint-server revoke XXXX-XXXX-XXXX-XXXX
docker compose exec relay rallypoint-server list
```

While registration is open, a serial identifies a client but doesn't keep strangers out; use closed mode if you want access control. Serials live in the `relay-data` volume (`serials.txt`); treat that file as a secret.

### Join

In the launcher's **Online** box enter the server (`host:7777`), leave the serial blank for a new one, press **Connect**, then **Play** and use the game's LAN lobby.

## Build from source

```bash
git clone --recurse-submodules --depth 1 https://github.com/michaelmurfy/rallypoint
cd rallypoint
cargo test -p generals-core -p generals-proto -p generals-server   # fast tests
sh tests/run.sh                                                    # engine-patch math checks
cargo install tauri-cli --version "^2" --locked
cd launcher/src-tauri && cargo tauri build                         # needs the engine copied into launcher/src-tauri/engine/
```

Building the engine: `sh patches/apply.sh`, then follow `engine/README.md` (macOS: `cmake --preset apple-arm64-release`, target `z_generals`; Windows: `cmake --preset win32`). Details for contributors and AI agents are in [`agent.md`](agent.md).

Releases are built only when a version tag is pushed (`git tag v0.1.0-alpha.1 && git push --tags`), never on ordinary pushes; see `.github/workflows/release.yml`. Routine pushes run just the fast tests.

### Layout

| Path | What |
|---|---|
| `launcher/` | Tauri 2 app: `core/` (all the logic, tested), `src-tauri/` (thin shell), `ui/` (plain HTML) |
| `server/`, `shared/` | Relay server and the wire protocol crate |
| `engine/` | Git submodule: the macOS/Windows engine port |
| `patches/` | Our engine changes (widescreen, relay transport), applied with `patches/apply.sh` |
| `mods/` | Mod manifest |

## Known limitations

- The engine patches have not been played in a real game by the author yet; widescreen UI details and the relay transport are the most likely places to find bugs. Please file issues.
- No Linux engine, no Intel Mac engine.
- Automatic Steam downloading and in-launcher mod downloads are not implemented.
- The relay has one global room and identifies sessions by source address (a spoofed address could inject traffic).
- Game determinism between macOS and Windows (the classic Generals desync problem) has not been tackled yet.

## Credits and legal

Rallypoint stands on [Electronic Arts' GPL release of the Generals source](https://github.com/electronicarts/CnC_Generals_Zero_Hour), [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) and the [macOS port by dvcdsys](https://github.com/dvcdsys/GeneralsGameCode-macOS). The engine and our engine patches are GPL-3.0 with EA's additional terms (see `engine/LICENSE.md`). This project is not endorsed by or affiliated with Electronic Arts. *Command & Conquer* and *Generals* are trademarks of EA. A licence for the launcher and server code has not been chosen yet.
