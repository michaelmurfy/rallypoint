<p align="center"><img src="launcher/ui/logo.png" alt="Rallypoint" width="112"></p>

# Rallypoint

A cross-platform launcher and multiplayer relay for **Command & Conquer: Generals – Zero Hour**: native on macOS and Windows, with a relay server so you can play "LAN" games over the internet.

> **0.1 Beta.** Single player works on macOS and Windows and has been played on both. Multiplayer through the relay is the main thing still to do. See [What works](#what-works-in-01-beta) for an honest list of what is and isn't done.

You need your own copy of the game (for example *C&C The Ultimate Collection* on Steam). Rallypoint ships no EA game data.

## Features

- **A modern launcher** with a first-run setup wizard that walks you through game files, display, mods and maps.
- **Get the game files from inside the launcher.** On macOS (where Steam can't install these games) it downloads them for you from your Steam account with a native downloader (no Rosetta needed); on Windows it finds your Steam install.
- **Skirmish / single player.** The game engine is bundled in the app. **Play** opens the full game (Solo Play → Skirmish); **Quick Skirmish** jumps straight into a 1v1 against the AI.
- **Any resolution and aspect ratio.** 4K, HiDPI and ultrawide work on macOS and Windows. The camera keeps the same world scale per pixel, so wide screens see *more* of the map instead of being zoomed in, the 2D UI (including the command bar and its icons) is scaled uniformly and centred instead of stretched, and text follows the same scale. Windowed and fullscreen both work, and Fullscreen off really gives you a window.
- **Mods made easy.** Download a mod from its official page, then point Rallypoint at the file or folder. It unpacks Shockwave's Windows installer itself (no Windows needed) and never starts a mod's own launcher. Mods that are add-ons for another mod are listed as such and played together with it.
- **Map packs.** One click installs GenTool's ranked 1v1, 2v2 and 3v3 maps (or all of them) into the right folder; you can add any other map archive or folder too.
- **Stays up to date.** The launcher checks GitHub for a newer release when it starts and, if you agree, downloads and opens the installer for your OS.
- **Graphics options.** A camera distance setting (zoom out further than the original game allows) and one button for the community's high-quality settings (shadows, soft water, trees, particles) from the graphics fix, with your original `Options.ini` backed up.
- **Help when something goes wrong.** *Settings → Troubleshooting* checks your game files and the original Generals, and shows the game's last crash report and what it printed. Results appear next to the button you pressed, not in pop-ups.
- **Internet multiplayer through your own server (in progress).** Pick a room name and password in the launcher and play the game's normal LAN lobby over the internet. The relay, the encryption and the connection diagnostics are built; playing a full multiplayer game through it is still to do (see below).

## What works in 0.1 Beta

| | macOS (Apple Silicon) | Windows | Linux |
|---|---|---|---|
| Launcher | ✅ | ✅ | build it yourself (see [Linux](#linux)) |
| Download game files from Steam | ✅ tried with a real account | ✅ finds your Steam install | not used (no engine) |
| Engine / Skirmish | ✅ | ✅ | ❌ no engine yet |
| Widescreen / HiDPI (any resolution, ultrawide, command bar and text) | ✅ | ✅ | – |
| Mods (Shockwave, Shockwave Chaos) | ✅ | ✅ | – |
| Relay server | – | – | ✅ (Docker or binary) |
| Encrypted relay traffic, server diagnostics | ✅ tested between the launcher and a real server | ✅ same code | – |
| Multiplayer game through the relay | ⏳ still to do | ⏳ still to do | – |

Legend: ✅ works and has been played or tried, ⏳ built but not yet played end to end, ❌ missing.

Intel Macs are not supported (the engine port is arm64 only).

## Install

Download the installer for your platform from the [Releases](../../releases) page.

- **macOS:** open the `.dmg` and drag Rallypoint to Applications. The app is ad-hoc signed, not notarised, so Gatekeeper blocks the first launch. Right-click → **Open**, or run `xattr -dr com.apple.quarantine /Applications/Rallypoint.app`.
- **Windows:** run the `-setup.exe`. SmartScreen may warn about an unknown publisher (**More info → Run anyway**).
- **Linux:** there is no Linux installer or game engine yet; see [Linux](#linux). The relay server does run on Linux (below).

### Game data

Rallypoint needs the folder that contains `INIZH.big` (Zero Hour's data).

- **Windows:** Steam installs are found automatically.
- **macOS:** the setup wizard (or **Settings**) has **Download from Steam**. Enter your Steam account name and password; Rallypoint fetches the open-source [DepotDownloader](https://github.com/SteamRE/DepotDownloader) (checked against a pinned checksum), signs in, and downloads Generals and Zero Hour (about 4 GB). If you use the Steam Mobile app, Steam sends a notification to approve the sign-in; just tap approve (or choose **Enter a code instead**). Email codes are asked for in the launcher. Your password is used only for that sign-in and is never saved. The account must own both games on Steam. It runs natively on Apple Silicon, so Rosetta isn't needed. This has been tried with a real account and works.
- Already have the files somewhere? Choose that folder in the wizard or in **Settings → Game files**.

## Playing

The first time you open Rallypoint, a short **setup wizard** helps you find or download your game files, choose a display setting, and optionally add mods and maps. Online play is set up later from the **Online** tab. You can run the wizard again from **Settings**.

On the Play tab choose a mod (optional), a resolution (it defaults to your screen's native pixels) and fullscreen, then press **Play** for the full menu, or **Quick Skirmish** to drop straight into a match against the AI.

### Mods

Open the **Mods** tab.

1. Press **Open download page** on a mod. Your browser opens the mod's official page; download it (choose the full release).
2. Back in Rallypoint press **Choose file…** (a `.zip`, `.7z`, `.rar`, `.tar.gz` or an installer `.exe`) or **Choose folder…** (for example the folder a Windows installer installed into). That card shows a spinner while it installs and flips to *Installed*. Nothing watches your Downloads folder and there is nothing to wait for. You can also drop a file or folder anywhere on the Mods page, or paste its path.
3. Tick the mods you want on the Play tab. Several can load at once; an add-on is laid over the mod it needs.

Mods are kept in the `Mods` folder inside Rallypoint's data directory and can be removed from the Mods tab (**Open mods folder** shows you where).

| OS | Data directory |
|---|---|
| macOS | `~/Library/Application Support/GeneralsZH/` |
| Windows | `%APPDATA%\GeneralsZH\` |
| Linux | `~/.local/share/GeneralsZH/` |

Why does it open your browser instead of downloading directly? Mod sites such as ModDB don't allow automated downloads, so the launcher links to the official page and you choose the file you downloaded. Currently listed: **C&C: Shockwave**, which plays on its own, and its add-on **C&C: Shockwave Chaos**, which needs Shockwave installed and is played together with it (Rallypoint lays it over Shockwave when you launch, without changing your Shockwave folder). Know another mod that should be listed? Open an issue with its ModDB link.

Unpacking uses tools already on your computer. macOS and Windows can open `.zip` out of the box; for `.7z`, `.rar` and installer `.exe` files you may need `brew install sevenzip innoextract` (macOS), `sudo apt install p7zip-full innoextract unzip` (Linux) or [7-Zip](https://www.7-zip.org/) (Windows). Rallypoint tells you if one is missing. Archives are checked before anything is written, and ones that try to escape their folder are refused.

#### Mods that come as an `.exe`

Many mods ship a Windows installer (and some also a launcher program, or patch the game's own executable). Rallypoint runs a ported engine, so what matters is the mod's **game data** (`.big` files). Your options, best first:

1. **Choose the installer (or drop it on the window).** Shockwave's download is a Clickteam installer (often inside a `.zip`). Rallypoint unpacks the game archives itself and does not start Shockwave's Windows launcher, so the same file works on macOS and Windows. Shockwave Chaos is a plain `.zip` and installs directly. For other installers, `innoextract` or 7-Zip unpacks them the same way.
2. **Look for a plain download.** Many mods also offer a `.zip` or "no installer" build.
3. **On Windows, run the installer normally**, then drag the folder it installed into onto the Mods tab. Rallypoint copies the game files into its own `Mods` folder.
4. **Unpack it yourself** with `innoextract -d out Setup.exe` or `7z x Setup.exe -oout`, then drop the `out` folder on the window.

What can't work: a patched `generals.exe` / `game.dat`, or a feature that exists only inside the mod's own launcher program. Rallypoint's engine loads the mod's `.big` and `.gib` files. Mods made only of loose files with no `.big` aren't installed automatically yet. Mods that need the 1.04 patch are fine on the Steam version, which already has it.

Setup downloads GenTool's sharp command-bar icons (Control Bar HD) and loads them with the game. On ultrawide screens the command bar and its icons keep their proportions and sit in the middle of the screen with the rest of the interface. A higher camera is optional on the Play tab and stays off unless you turn it on; *Settings → Graphics* has the camera distance and the high-quality options.

### Maps

Zero Hour looks for maps in `Documents/Command and Conquer Generals Zero Hour Data/Maps`. On the **Mods** tab (and in the setup wizard) the **Maps** section installs map packs from [GenTool's download page](https://www.gentool.net/download/): all ranked maps (56 MB), or just the 1v1, 2v2 or 3v3 sets. **Add maps from a file** or **Add a folder of maps** handles any other pack: Rallypoint finds the map folders inside and copies them over. GenTool itself is a Windows-only add-on and isn't needed or installed.

### Updates

At start-up Rallypoint asks GitHub whether a newer release exists (turn this off in **Settings → Updates**). If there is one, a banner offers **Download and open installer**: it fetches the installer for your OS from this repository's release, checks it against the release's `SHA256SUMS`, and opens it. You finish the install yourself, so quit Rallypoint first.

### Widescreen and HiDPI

On by default on macOS and Windows: any resolution, ultrawide, Retina. Menus, the command bar, its icons and text share one centred scale, the camera shows more of the map instead of zooming in, and the macOS trackpad zooms sensibly. Set `GEN_STRETCH=1` in the environment to get the original stretched UI and fixed-horizontal-FOV camera back.

## Multiplayer

**Status: still to do.** The relay server, the encryption, the server key checks and the connection diagnostics are built and tested, and the game only ever sees the relay's own address range while you are connected. A full multiplayer game through it has not been played yet, so treat it as work in progress.

Players connect to a Rallypoint server. In the launcher's **Online** box enter the server address, a room name and a room password, then press **Connect** and use the game's LAN lobby. Everyone who uses the same room and password lands in the same game.

Everything between the launcher and the server is encrypted and authenticated (Noise over X25519 and ChaCha20-Poly1305), including the serial, the room and the password proof. The first time you connect to a server the launcher remembers its identity key; if a different server ever answers at that address, it refuses to connect and asks you. The server keeps its key in its data volume (`serials.key`): back it up, because a new key makes every player see a changed identity. Compare the fingerprint from `make key` with the one shown on the Online tab to be sure of a server the first time.

The relay also checks that every player's game stays in step: each game reports a checksum of its simulation, the server compares them, and if they ever differ it tells everyone in the game straight away (in the game and on the Online tab) which frame diverged and, with three or more players, who is out of step. With two players it can tell you they disagree but not who is right.

While you are connected the Online tab shows how it is going: your latency, jitter and packet loss, the traffic you are sending, and the server's version, uptime, rooms and players in use, and load. The tunnel carries only the game's own network traffic (its lobby and game ports); anything else is dropped, and each player is limited to a sensible packet rate so one client cannot flood a server.

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
| `make key` | Show the server's identity fingerprint |

Settings live in `.env` (see [`.env.example`](.env.example)): the port, and `ALLOWED_COUNTRIES` to only accept players from certain countries. The country lists are downloaded for you; local-network addresses are always let through unless you set `ALLOW_PRIVATE=false`.

The image is also published as `ghcr.io/michaelmurfy/rallypoint-relay`, and the release page has a plain Linux binary (`rallypoint-server`).

## Linux

Releases don't include a Linux launcher, and there is no Linux game engine yet (only the relay server runs on Linux). Running the Windows engine through Proton or Wine is a possible way to get Linux players in the future, and is not started. If you want the launcher itself, build it:

```bash
# Debian / Ubuntu build dependencies (other distros: the equivalent webkit2gtk 4.1 / GTK 3 dev packages)
sudo apt install build-essential curl libwebkit2gtk-4.1-dev libgtk-3-dev libayatana-appindicator3-dev librsvg2-dev
git clone --depth 1 https://github.com/michaelmurfy/rallypoint
cd rallypoint
make linux-client            # .deb, .rpm and .AppImage in target/release/bundle/
```

The launcher opens and manages mods, maps and relay rooms, but has no engine to start on Linux.

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

Releases are built only when a version tag is pushed (`make release VERSION=0.1.0-beta.1`), never on ordinary pushes; see `.github/workflows/release.yml`. Routine pushes run just the fast tests.

### Layout

| Path | What |
|---|---|
| `launcher/` | Tauri 2 app: `core/` (all the logic, tested), `src-tauri/` (thin shell), `ui/` (plain HTML) |
| `server/`, `shared/` | Relay server and the wire protocol crate |
| `engine/` | Git submodule: the macOS/Windows engine port |
| `patches/` | Our engine changes (widescreen and HiDPI, relay transport, macOS and Windows fixes, mod loading), applied with `patches/apply.sh` |
| `mods/` | Mod manifest |

## Known limitations

- Multiplayer through the relay is still to do: it is built and tested piece by piece but a full game has not been played.
- No Linux engine (Proton or Wine is a possible future route) and no Intel Mac engine.
- Mods can't be downloaded from inside the launcher: you download from the mod's page and choose the file.
- Planned but not started: saving and loading multiplayer games, controlling allied units, and co-op (sharing one player and its units).
- Game determinism between macOS and Windows (the classic Generals desync problem) has not been tackled yet.
- The first time you connect to a relay you trust whoever answers (like SSH); after that its identity is checked. Compare the fingerprint to be sure.

## Credits and legal

Rallypoint stands on [Electronic Arts' GPL release of the Generals source](https://github.com/electronicarts/CnC_Generals_Zero_Hour), [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) and the [macOS port by dvcdsys](https://github.com/dvcdsys/GeneralsGameCode-macOS). The engine and our engine patches are GPL-3.0 with EA's additional terms (see `engine/LICENSE.md`). This project is not endorsed by or affiliated with Electronic Arts. *Command & Conquer* and *Generals* are trademarks of EA. The launcher, relay server and scripts in this repository are MIT licensed (see [`LICENSE`](LICENSE)). The engine in `engine/` and our changes to it in `patches/` are GPL-3.0 with EA's additional terms ([`patches/LICENSE.md`](patches/LICENSE.md)); the launcher runs the engine as a separate program. Steam downloads use the open-source (GPL-2.0) [DepotDownloader](https://github.com/SteamRE/DepotDownloader), fetched at runtime and not bundled. Release downloads bundle the engine, and its complete source is `engine/` at the tagged commit plus `patches/`.
