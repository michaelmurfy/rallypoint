<p align="center"><img src="launcher/ui/logo.png" alt="Rallypoint" width="112"></p>

# Rallypoint

A cross-platform launcher and multiplayer relay for **Command & Conquer: Generals – Zero Hour**: native on macOS and Windows, with a relay server so you can play "LAN" games over the internet.

> **0.1 Alpha.** Expect rough edges. See [What works](#what-works-in-01-alpha) for an honest list of what is and isn't tested.

You need your own copy of the game (for example *C&C The Ultimate Collection* on Steam). Rallypoint ships no EA game data.

## Features

- **Launcher** with Steam detection, mod selection, resolution and fullscreen options.
- **Get the game files from inside the launcher.** On macOS and Linux (where Steam can't install these games) it downloads them for you through Steam's own downloader; on Windows it finds your Steam install.
- **Skirmish / single player.** The game engine is bundled in the app. **Play** opens the full game (Solo Play → Skirmish); **Quick Skirmish** jumps straight into a 1v1 against the AI.
- **Any resolution and aspect ratio.** 4K, HiDPI and ultrawide are supported. The camera keeps the same world scale per pixel, so wide screens see *more* of the map instead of being zoomed in, and the 2D UI is scaled uniformly and centred instead of stretched.
- **Mods** (Shockwave, Chaos Mod) launched through the engine's `-mod` support.
- **Internet multiplayer through your own server.** Pick a room name and password in the launcher and play the game's normal LAN lobby over the internet.

## What works in 0.1 Alpha

| | macOS (Apple Silicon) | Windows | Linux |
|---|---|---|---|
| Launcher | ✅ | ✅ | ✅ (no engine yet) |
| Download game files from Steam | ⚠️ written, not tried with a real account | ✅ finds your Steam install | ⚠️ same as macOS |
| Engine / Skirmish | ⚠️ builds in CI; the upstream port reports Skirmish playable | ⚠️ builds from upstream source, untested here | ❌ not built yet |
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
- **macOS / Linux:** open **Don't have the files on this computer?**, enter your Steam account name and password, and press **Download**. Rallypoint fetches Valve's `steamcmd`, signs in, and downloads Generals and Zero Hour (about 4 GB); if Steam Guard asks for a code you'll be prompted. Your password is used only for that sign-in and is never saved. The account must own the games. On Apple Silicon, `steamcmd` needs Rosetta (`softwareupdate --install-rosetta`).
- Already have the files somewhere? Paste the folder into **game folder**.

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

Players connect to a Rallypoint server. In the launcher's **Online** box enter the server address, a room name and a room password, then press **Connect** and use the game's LAN lobby. Everyone who uses the same room and password lands in the same game.

### Run a server

Needs Docker, `make`, and one open **UDP** port (7777 by default).

```bash
git clone https://github.com/michaelmurfy/rallypoint && cd rallypoint
make up
```

| Command | What it does |
|---|---|
| `make config COUNTRIES=NZ` | Create `.env`, optionally limiting the server to one or more countries (`NZ`, `NZ,AU`) |
| `make up` / `make down` / `make restart` | Start, stop, or apply changed settings |
| `make update` | Pull the latest version, refresh the country lists, restart |
| `make logs` / `make status` | Watch the server |

Settings live in `.env` (see [`.env.example`](.env.example)): the port, and `ALLOWED_COUNTRIES` to only accept players from certain countries. The country lists are downloaded for you; local-network addresses are always let through unless you set `ALLOW_PRIVATE=false`.

The image is also published as `ghcr.io/michaelmurfy/rallypoint-relay`, and the release page has a plain Linux binary (`rallypoint-server`).

## Build from source

```bash
git clone --recurse-submodules --depth 1 https://github.com/michaelmurfy/rallypoint
cd rallypoint
cargo test -p generals-core -p generals-proto -p generals-server   # fast tests
make test                                                          # everything, including engine-patch math checks
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
- The Steam download has not been tried against a real account yet, and mods can't be downloaded from inside the launcher.
- Planned but not started: saving and loading multiplayer games, controlling allied units, and co-op (sharing one player and its units).
- Relay traffic is not encrypted, so someone on the network path could read or replay it; room passwords keep other players out, not eavesdroppers.
- Game determinism between macOS and Windows (the classic Generals desync problem) has not been tackled yet.

## Credits and legal

Rallypoint stands on [Electronic Arts' GPL release of the Generals source](https://github.com/electronicarts/CnC_Generals_Zero_Hour), [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) and the [macOS port by dvcdsys](https://github.com/dvcdsys/GeneralsGameCode-macOS). The engine and our engine patches are GPL-3.0 with EA's additional terms (see `engine/LICENSE.md`). This project is not endorsed by or affiliated with Electronic Arts. *Command & Conquer* and *Generals* are trademarks of EA. The launcher, relay server and scripts in this repository are MIT licensed (see [`LICENSE`](LICENSE)). The engine in `engine/` and our changes to it in `patches/` are GPL-3.0 with EA's additional terms ([`patches/LICENSE.md`](patches/LICENSE.md)); the launcher runs the engine as a separate program. Release downloads bundle the engine, and its complete source is `engine/` at the tagged commit plus `patches/`.
