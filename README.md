<p align="center"><img src="launcher/ui/logo.png" alt="Rallypoint" width="112"></p>

# Rallypoint

Play **Command & Conquer: Generals – Zero Hour** on a Mac or a PC, at any resolution, and play it online with friends through your own relay server.

You need your own copy of the game (for example *C&C The Ultimate Collection* on Steam). Rallypoint ships no EA game data.

**Where it stands:** single player works on macOS and Windows. Online games have been played Windows vs Windows and Mac vs Windows. Known issue: some games drift out of sync a couple of minutes in. We are tracking it down (see [Debug logs](#debug-logs)).

## Get started

1. Download the installer from [Releases](../../releases).
2. Open Rallypoint. The setup wizard finds or downloads your game files.
3. Press **Play**, or **Quick Skirmish** to be in a match in seconds.

- **macOS (Apple Silicon only):** open the `.dmg`, drag Rallypoint to Applications. The first launch is blocked by Gatekeeper: right-click → **Open**, or run `xattr -dr com.apple.quarantine /Applications/Rallypoint.app`.
- **Windows:** run the `-setup.exe`. If SmartScreen warns, choose **More info → Run anyway**.
- **Linux:** no launcher or engine yet. The relay server runs there (see [Run a server](#run-a-server)).

## What works

| | macOS | Windows | Linux |
|---|---|---|---|
| Launcher, engine, skirmish | ✅ | ✅ | ❌ |
| Widescreen, 4K, HiDPI | ✅ | ✅ | – |
| Mods (Shockwave, Chaos, Cold War Crisis) | ✅ | ✅ | – |
| Online through the relay | ✅ | ✅ | – |
| Relay server | ✅ | ✅ | ✅ x86_64, arm64, armv7 |

## Your game files

Rallypoint needs the folder with `INIZH.big`.

- **Windows:** Steam installs are found automatically.
- **macOS:** in *Settings → Game files* (or the setup wizard) open **Download them from Steam**. Enter your Steam account and password. Rallypoint downloads Generals and Zero Hour (about 4 GB) with the open-source [DepotDownloader](https://github.com/SteamRE/DepotDownloader). Approve the sign-in on your phone or type the code. The password is never saved, and the sign-in token the downloader keeps is deleted when the download ends. The account must own both games.
- **Already have them?** Point to the folder in the wizard or in *Settings → Game files*.

## Play

On the **Play** tab:

- pick the game (Zero Hour or an installed mod) and any extra mods
- pick a resolution (default: your screen's native size) and fullscreen
- **Play** opens the full game; **Quick Skirmish** starts a match straight away with the opponents (1–3) and difficulty you choose

**Widescreen and HiDPI** are on by default: wide screens see more of the map instead of a zoomed-in one, and the menus, command bar and text scale together. Set `GEN_STRETCH=1` to get the original stretched look back. *Settings → Graphics* has camera distance and the community's high-quality graphics settings (your `Options.ini` is backed up first).

## Mods

1. On the **Mods** tab press **Download page** under a mod and download it from its official page.
2. Press **Choose file…** (`.zip`, `.7z`, `.rar`, `.tar.gz`, or an installer `.exe`) or **Choose folder…**. You can also drop it on the window.
3. Pick it on the Play tab.

Listed mods: **Shockwave**, its add-on **Shockwave Chaos** (played on top of Shockwave), and **Cold War Crisis** (a total conversion: it can't be combined with the other two). Anything else you add shows up as a tick box on top of the game.

- **Why not download in the app?** Mod sites such as ModDB block automated downloads, so you fetch the file and Rallypoint installs it.
- **Installers:** Shockwave's Windows installer is unpacked by Rallypoint itself, on Mac and PC, and its own launcher is never started. For other installers, `innoextract` or 7-Zip does the same job (`brew install sevenzip innoextract`, `sudo apt install p7zip-full innoextract unzip`, or [7-Zip](https://www.7-zip.org/)).
- **What can't work:** a patched `generals.exe`, or features that live in a mod's own launcher. Mods without `.big` files aren't installed automatically yet.
- **Safety:** archives are checked before anything is written; ones that try to escape their folder are refused.

Mods live in the `Mods` folder of Rallypoint's data directory (macOS `~/Library/Application Support/GeneralsZH/`, Windows `%APPDATA%\GeneralsZH\`, Linux `~/.local/share/GeneralsZH/`).

## Maps

The **Maps** tab has two parts.

- **Get more maps:** one-click packs from [GenTool](https://www.gentool.net/download/) (all ranked maps, or the 1v1, 2v2 or 3v3 set) and the [Epic Showdown 707 pack](https://www.moddb.com/mods/cc-epic-showdown-707-mappack) (download it, then choose the file). Any other archive or folder works too.
- **Your maps:** search, filter by pack and player count, sort, and switch maps on or off. Big packs arrive switched off so they don't bury your list. Switched-off maps move to `<app data>/GeneralsZH/MapLibrary`; nothing is deleted until you delete it. Changes apply the next time the game starts.
- **Preview** shows a map as the game does, with numbered start positions. Click a start position to play from it (or leave it on Random), choose opponents and difficulty, and press **Play skirmish**.

## Play online

1. Open the **Online** tab and enter a server, a room name and a room password.
2. Press **Connect** (or Enter), then use the game's LAN lobby. Everyone in the same room with the same password ends up in the same game.

- **Save server** keeps the server and room. Tick *Remember the password* to keep that too (stored in Rallypoint's settings file, readable only by you, never shown again).
- **Your name** in the lobby is set on the same tab (default: your computer's name).
- **If it can't connect,** the page says what it tried and what to check: address, port, firewall (UDP), room password, full room.
- **While connected,** the bottom-left corner shows ping, players in your room and packet loss, and the Online tab graphs them with the server's load.

**Security:** everything between the launcher and the server is encrypted and authenticated (Noise over X25519 and ChaCha20-Poly1305). The first time you connect, the launcher remembers the server's identity; a different server answering later is refused until you say so. Compare the fingerprint from `make key` with the one on the Online tab to be sure the first time.

**Staying in sync:** both machines use the same deterministic maths. The lobby refuses a player whose game data or maths differs. The relay also compares every player's simulation checksum and tells everyone, with the frame and who is out of step, if they diverge. Bringing a player back in step mid-game isn't built yet.

## Debug logs

If a game crashes or goes out of sync:

1. Both players switch on **Settings → Multiplayer → Logs → Record sync logs**.
2. Play again. The first ten seconds are logged frame by frame, then every 100 frames for the whole game.
3. Each player presses **Download debug logs**. You see what goes in first; passwords, keys, your Steam name and your user name in paths are left out, and the replay is added only if you tick it.
4. Send the zip. **Compare…** (same card) shows where two games first differed.

*Settings → Troubleshooting* checks your game files and shows the game's last crash report.

## Updates

Rallypoint checks GitHub for a newer release when it starts (switch off in *Settings → Updates*). Updates are signed: the app installs one only if its signature matches the key built into the app. Installers are kept in a private folder, checked again right before they run, and cleaned up on the next start.

## Run a server

Runs on Linux (x86_64, arm64, 32-bit armv7: Raspberry Pi 3/4/5, ARM cloud servers), macOS (Apple silicon) and Windows. Needs one open **UDP** port (7777 by default).

**With Docker (Linux):**

```bash
git clone https://github.com/michaelmurfy/rallypoint && cd rallypoint
make up
```

The image `ghcr.io/michaelmurfy/rallypoint-relay` is multi-architecture, so this is the same on a PC and a Pi. It uses the host's network (no NAT delay), so open the UDP port in the firewall.

| Command | What it does |
|---|---|
| `make config COUNTRIES=NZ` | Create `.env`, optionally limited to countries (`NZ,AU`) |
| `make up` / `down` / `restart` | Start (pulling the released image), stop, apply changed settings |
| `make update` | Pull the latest image and country lists, restart |
| `make dev` | Build from your checkout |
| `make logs` / `status` / `key` | Watch it, check it, show the identity fingerprint |

**Without Docker:** each release has `rallypoint-server-<version>-linux-x86_64`, `-linux-aarch64`, `-linux-armv7`, `-macos-arm64` and `-windows-x86_64` downloads. Unpack and run `rallypoint-server run`; settings are environment variables (see [`.env.example`](.env.example)). Linux builds run on Raspberry Pi OS Bookworm and newer. Pi Zero and Pi 1 (armv6) aren't covered.

- **Settings** live in `.env`: the port, and `ALLOWED_COUNTRIES` to accept only some countries. Local-network addresses are refused by the country limit unless `ALLOW_PRIVATE=true` (LAN-only servers).
- **Keep `serials.key` safe** (in the data volume). A new key shows every player a changed identity.
- **Limits:** the tunnel carries only the game's own traffic; each player has a packet-rate limit; wrong room passwords and sign-ups are rate-limited per address.
- **`docker compose pull` says `unauthorized`?** The GHCR package is private: make it Public under your profile's Packages settings. Until then the commands build from your checkout.

## Linux

No Linux launcher in releases and no Linux engine yet (Proton or Wine is a possible future route). To build the launcher yourself:

```bash
sudo apt install build-essential curl libwebkit2gtk-4.1-dev libgtk-3-dev libayatana-appindicator3-dev librsvg2-dev
git clone --depth 1 https://github.com/michaelmurfy/rallypoint && cd rallypoint
make linux-client            # .deb, .rpm and .AppImage in target/release/bundle/
```

It manages mods, maps and relay rooms, but has no engine to start.

## Build from source

```bash
git clone --recurse-submodules --depth 1 https://github.com/michaelmurfy/rallypoint
cd rallypoint
make test                                   # everything, including the engine-patch maths checks
cargo install tauri-cli --version "^2" --locked
cd launcher/src-tauri && cargo tauri build  # needs the engine in launcher/src-tauri/engine/
```

Engine: `sh patches/apply.sh`, then follow `engine/README.md` (macOS: `cmake --preset apple-arm64-release`, target `z_generals`; Windows: `cmake --preset win32`). Releases are built when a `v*` tag is pushed (`make release VERSION=x.y.z`). More for contributors and AI agents in [`agent.md`](agent.md).

| Path | What |
|---|---|
| `launcher/` | Tauri 2 app: `core/` (logic, tested), `src-tauri/` (thin shell), `ui/` (plain HTML) |
| `server/`, `shared/` | Relay server and the wire protocol |
| `engine/` | Submodule: the macOS/Windows engine port |
| `patches/` | Our engine changes, applied with `patches/apply.sh` |
| `mods/` | Mod and map-pack lists |

## Known limitations

- Some online games drift out of sync a couple of minutes in. Investigation is under way; sync logs and debug logs are how to help.
- No Linux engine and no Intel Mac engine.
- Mods and the Epic Showdown pack are downloaded in your browser, not inside the launcher.
- Not built yet: save and load in multiplayer, controlling allies' units, co-op, bringing an out-of-step player back mid-game.
- The first connection to a relay trusts whoever answers (like SSH); after that its identity is checked.

## Credits and legal

Rallypoint stands on [Electronic Arts' GPL release of the Generals source](https://github.com/electronicarts/CnC_Generals_Zero_Hour), [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) and the [macOS port by dvcdsys](https://github.com/dvcdsys/GeneralsGameCode-macOS). The engine and our patches are GPL-3.0 with EA's additional terms (see `engine/LICENSE.md` and [`patches/LICENSE.md`](patches/LICENSE.md)). The launcher, relay server and scripts are MIT licensed ([`LICENSE`](LICENSE)); the launcher runs the engine as a separate program. Not endorsed by or affiliated with Electronic Arts. *Command & Conquer* and *Generals* are trademarks of EA. Steam downloads use the open-source (GPL-2.0) [DepotDownloader](https://github.com/SteamRE/DepotDownloader), fetched at runtime and not bundled. Releases bundle the engine; its complete source is `engine/` at the tagged commit plus `patches/`.
