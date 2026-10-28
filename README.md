<p align="center"><img src="launcher/ui/logo.png" alt="Rallypoint" width="112"></p>

# Rallypoint

Play **Command & Conquer: Generals – Zero Hour** on a Mac or a PC, at any resolution, and play it online with friends.

You need your own copy of the game (for example *C&C The Ultimate Collection* on Steam). Rallypoint ships no EA game data, and never will.

**Playing over the internet needs a Rallypoint server.** It's a small program that passes the game's traffic between you and your friends, and one cheap server is enough for everyone. Setting one up takes about ten minutes: see [Run a server](#run-a-server). Skirmishes, mods and maps work without one, and so does a LAN game on the same network.

**Where it stands:** single player works on macOS and Windows, and online games have been played Windows against Windows and Mac against Windows. It isn't perfect. Some long games still drift out of sync, and we're hunting that down (see [Debug logs](#debug-logs)). If you hit one, your logs are how you help.

## Get started

1. Download the installer from [Releases](../../releases).
2. Open Rallypoint. The setup wizard finds or downloads your game files.
3. Press **Play**, or **Start skirmish** under Quick Skirmish if you just want to blow something up in the next ten seconds.

- **macOS (Apple Silicon only):** open the `.dmg`, drag Rallypoint to Applications. The first launch is blocked by Gatekeeper: right-click → **Open**, or run `xattr -dr com.apple.quarantine /Applications/Rallypoint.app`.
- **Windows:** run the `-setup.exe`. If SmartScreen warns, choose **More info → Run anyway**.
- **Linux:** no launcher or engine yet. The relay server runs there, and Linux is the best place to host one (see [Run a server](#run-a-server)).

## What works

| | macOS | Windows | Linux |
|---|---|---|---|
| Launcher, engine, skirmish | ✅ | ✅ | ❌ |
| Widescreen, 4K, HiDPI | ✅ | ✅ | – |
| Mods (Shockwave, Chaos, Cold War Crisis) | ✅ | ✅ | – |
| Online through the relay | ✅ | ✅ | – |
| Relay server | – | – | ✅ x86_64, arm64, armv7 (Debian and Ubuntu setup script) |

## Your game files

Rallypoint needs the folder with `INIZH.big` (that's Zero Hour's main data file). You have to own the game; Rallypoint just needs to know where it lives.

- **Windows:** Steam installs are found automatically.
- **macOS:** in *Settings → Game files* (or the setup wizard) open **Download them from Steam**. Enter your Steam account and password. Rallypoint downloads Generals and Zero Hour (about 4 GB) with the open-source [DepotDownloader](https://github.com/SteamRE/DepotDownloader). Approve the sign-in on your phone or type the code. The password is never saved, and the sign-in token the downloader keeps is deleted when the download ends. The account must own both games.
- **Already have them?** Point to the folder in the wizard or in *Settings → Game files*.

## Play

On the **Play** tab:

- pick the game (Zero Hour or an installed mod) and any extra mods
- pick a resolution (default: your screen's native size) and fullscreen
- **Play** opens the full game; **Start skirmish** (in the Quick Skirmish block) starts a match straight away with the opponents (1–3) and difficulty you choose

**Widescreen and HiDPI** are on by default: wide screens see more of the map instead of a zoomed-in one, and the menus, command bar and text scale together. Set `GEN_STRETCH=1` to get the original stretched look back. *Settings → Graphics* has camera distance and the community's high-quality graphics settings (your `Options.ini` is backed up first).

## Mods

1. On the **Mods** tab press **Download page** under a mod and download it from its official page.
2. Press **Choose file…** (`.zip`, `.7z`, `.rar`, `.tar.gz`, or an installer `.exe`) or **Choose folder…**. You can also drop it on the window.
3. Pick it on the Play tab.

Listed mods: **Shockwave**, its add-on **Shockwave Chaos** (played on top of Shockwave), and **Cold War Crisis** (a total conversion: it can't be combined with the other two). Anything else you add shows up as a tick box on top of the game.

- **Why not download in the app?** Mod sites such as ModDB block automated downloads, so you fetch the file yourself and Rallypoint does the installing. A small chore, but it keeps us on the right side of those sites.
- **Installers:** Shockwave's Windows installer is unpacked by Rallypoint itself, on Mac and PC, and its own launcher is never started. For other installers, `innoextract` or 7-Zip does the same job (`brew install sevenzip innoextract`, `sudo apt install p7zip-full innoextract unzip`, or [7-Zip](https://www.7-zip.org/)).
- **What can't work:** a patched `generals.exe`, or features that live in a mod's own launcher. Mods without `.big` files aren't installed automatically yet.
- **Safety:** archives are checked before anything is written; ones that try to escape their folder are refused.

Mods live in the `Mods` folder of Rallypoint's data directory (macOS `~/Library/Application Support/GeneralsZH/`, Windows `%APPDATA%\GeneralsZH\`, Linux `~/.local/share/GeneralsZH/`).

## Maps

The **Maps** tab has two parts.

- **Get more maps:** one-click packs from [GenTool](https://www.gentool.net/download/) (all ranked maps, or the 1v1, 2v2 or 3v3 set) and the [Epic Showdown 707 pack](https://www.moddb.com/mods/cc-epic-showdown-707-mappack) (download it, then choose the file). Any other archive or folder works too.
- **Your maps:** search, filter by pack and player count, sort, and switch maps on or off. Big packs arrive switched off so they don't bury your list. Switched-off maps move to `<app data>/GeneralsZH/MapLibrary`; nothing is deleted until you delete it. Changes apply the next time the game starts.
- **Skirmish** (on every map in the list) opens the map with its numbered start positions. Set up the whole match: switch the game, then give each place a player (you, Easy/Medium/Brutal AI or closed), a general read from the game or mod, a team and a start position (click the map to assign one). Pick a game speed beside the button (50% to 200%; normal until you change it) and press **Play skirmish**. Speed only applies to skirmishes: online games run at the same speed for everyone.

## Play online

You need a Rallypoint server for this. If a friend runs one, ask them for its address. If nobody does, [set one up](#run-a-server); it's quicker than it sounds.

1. Open the **Online** tab and enter the server, a room name and a room password.
2. Press **Connect** (or Enter), then use the game's LAN lobby. Everyone in the same room with the same password ends up in the same game.

- **Save server** keeps the server and room. Tick *Remember the password* to keep that too (stored in Rallypoint's settings file, readable only by you, never shown again).
- **Your name** in the lobby is set on the same tab (default: your computer's name).
- **If it can't connect,** the page says what it tried and what to check: address, port, firewall (UDP), room password, full room.
- **While connected,** the bottom-left corner shows ping, players in your room and packet loss, and the Online tab graphs them with the server's load.

**Security:** everything between the launcher and the server is encrypted and authenticated (Noise over X25519 and ChaCha20-Poly1305). The first time you connect, the launcher remembers the server's identity; a different server answering later is refused until you say so. Compare the fingerprint from `rallypoint key` (run on the server) with the one on the Online tab to be sure the first time.

**Staying in sync:** a lockstep game only works if every machine does exactly the same sums, so Rallypoint makes a Mac and a PC do the same sums. The lobby refuses a player whose game data or maths differs. The relay also compares every player's simulation checksum and tells everyone, with the frame and who is out of step, if they diverge. Bringing a player back in step mid-game isn't built yet.

## Debug logs

If a game crashes or goes out of sync:

1. Both players switch on **Settings → Multiplayer → Logs → Record sync logs**.
2. Play again. The first ten seconds are logged frame by frame, then every 100 frames for the whole game.
3. Each player presses **Download debug logs**. You see what goes in first; passwords, keys, your Steam name and your user name in paths are left out, and the replay is added only if you tick it.
4. Send the zip. **Compare…** (same card) shows where two games first differed.

*Settings → Troubleshooting* checks your game files and shows the game's last crash report.

## Updates

Rallypoint checks GitHub for a newer release when it starts (switch that off in *Settings → Updates*), and *Changelog* there shows what changed. Updates are signed: the app installs one only if its signature matches the key built into the app. Installers are kept in a private folder, checked again right before they run, and cleaned up on the next start.

## Run a server

To play over the internet, someone has to run a Rallypoint server. It runs on Linux: a cheap rented VM or a Raspberry Pi. It uses almost nothing (a few megabytes of memory and a trickle of bandwidth), so the cheapest machine you can rent is plenty. Everyone who plays connects to the same server and meets in a room.

The fastest way is the Ansible playbook in [`deploy/ansible`](deploy/ansible). It sets up a blank server for you: updates, Docker, a firewall, protection against password guessing, automatic security updates, a login banner and the server itself. It also starts [Watchtower](https://github.com/nicholas-fedor/watchtower), which checks once a day for a newer Rallypoint server and restarts it on the new version, so the server looks after itself.

### 1. Rent a small server

You want a **Debian 13 or Ubuntu 26.04 LTS** machine (older releases still work) with 1 CPU and 1 GB of memory. Pick the data centre **closest to your players**, because distance is the biggest part of lag. When it asks, add your **SSH key** instead of using a password. If you've never made one, run `ssh-keygen` and paste the contents of the `.pub` file it makes.

| Provider | Why you might pick it |
|---|---|
| [Quic Compute](https://www.quic.nz/cloud-compute/) (New Zealand) | The obvious choice if your players are in NZ: a local server means low ping. |
| [Vultr](https://www.vultr.com/) | The $5 a month plan on Debian is all you need, and it has data centres all over the world. |
| [Hetzner Cloud](https://www.hetzner.com/cloud) | Very good value if your players are in Europe or the US. |
| [DigitalOcean](https://www.digitalocean.com/) or [Linode](https://www.linode.com/) | Easy panels and plenty of regions. |
| A Raspberry Pi at home | Works well. Use a Pi 3, 4 or 5 with **64-bit Raspberry Pi OS Lite**. You'll need to forward the UDP port on your router, and your home internet is then part of everyone's game. |

### 2. Run the playbook

**The playbook is for Debian and Ubuntu only**, which includes Raspberry Pi OS. It has been tested on **Debian 12, Debian 13, Ubuntu 24.04 LTS and Ubuntu 26.04 LTS**. The server is meant to run on a Linux VM or a Raspberry Pi, not on a Mac or Windows PC. On any other Linux, use the plain Docker steps further down.

The easiest way is to run it on the fresh server itself, so SSH in as root and install Ansible and git first (the playbook installs everything else):

```bash
sudo apt update && sudo apt install -y git ansible
```

Then clone the repo and run it against the machine you are on:

```bash
git clone https://github.com/michaelmurfy/rallypoint && cd rallypoint/deploy/ansible
ansible-playbook -i localhost, -c local playbook.yml
```

It works out the server's public address by itself. Prefer to run it from another Linux machine over SSH? Copy `inventory.example.ini` to `inventory.ini`, put the server's address in it, and use `ansible-playbook -i inventory.ini playbook.yml`.

It takes a few minutes and ends by printing the address to type into the app, the server's fingerprint, and its public and private IP addresses (the public ones come from [icanhazip.com](https://icanhazip.com), IPv4 and IPv6). The same details are on the login banner. The server accepts IPv4 connections only for now, so give players the IPv4 address. It's safe to run again whenever you like.

- **Only let some countries in:** add `-e rallypoint_countries=NZ,AU` (two-letter codes, comma separated).
- **A different port:** `-e rallypoint_port=7778`. Players then enter `your-server:7778`.
- **Keep SSH passwords on:** `-e harden_ssh=false`. By default the playbook turns passwords off, but only after checking that a key is installed, so it can't lock you out.

### 3. Connect

In the Rallypoint app open **Online**, enter the address the playbook printed, choose a room name and password, and press **Connect**. The first time, the app remembers the server's identity; if the fingerprint on screen matches the one the playbook printed, you're talking to your own server.

### Looking after it

Log in to the server over SSH and use the `rallypoint` command:

| Command | What it does |
|---|---|
| `rallypoint status` | Is it running, and which port do players use |
| `rallypoint update` | Get the newest release and restart right now. Watchtower does this for you once a day, so you only need it when you're impatient. |
| `rallypoint logs` | Watch what it's doing (Ctrl+C to leave) |
| `rallypoint key` | Show the identity fingerprint |
| `rallypoint countries` | A menu to choose which countries can connect (or `rallypoint countries NZ,AU`; `none` lets everyone in) |
| `rallypoint stop` / `start` / `restart` | Does what it says |

Keep the server's `serials.key` safe (it lives in Docker's data volume). A new key shows every player a changed identity. The relay carries only the game's own traffic, and each player has a packet limit, so a stranger can't use it for anything else.

### How the server is locked down

What the playbook sets up, so you know what you're trusting:

- **Firewall:** everything coming in is blocked except SSH (rate limited) and the game's UDP port.
- **SSH:** keys only, no passwords, root can log in with a key but not a password, three tries per connection, no X11 or agent forwarding, and fail2ban bans addresses that keep guessing.
- **The server itself:** runs as an unprivileged user in a read-only container with every Linux capability dropped, a memory cap and no way to gain privileges. It only carries the game's own traffic.
- **Updates:** security patches install by themselves every night. Kernel patches need a restart; add `-e auto_reboot=true` to let the machine restart itself at 04:00, or reboot when you next log in.
- **Watchtower:** checks once a day for a new Rallypoint server image and only touches that one container. It's pinned to a version, read-only and has no extra privileges. It talks to Docker on the machine itself and nothing else, so on a server that only does this job it's a sensible way to stay up to date. Don't want it? `docker rm -f watchtower`, then use `rallypoint update` yourself.
- **Not done:** the playbook doesn't create a separate admin user, so you still log in as root with your key. To follow a release tag instead of the latest code, use `-e rallypoint_version=v0.5.7`.

### Doing it by hand

On any Linux machine with Docker (a Pi included), open one **UDP** port (7777 by default) and run:

```bash
git clone https://github.com/michaelmurfy/rallypoint && cd rallypoint
make up
```

`make up` pulls the released image, starts it and waits until the container reports healthy. The image works on PCs, ARM servers and Raspberry Pis alike, and uses the host's network so game packets skip Docker's NAT.

| Command | What it does |
|---|---|
| `make config COUNTRIES=NZ` | Create `.env`, optionally limited to countries (`NZ,AU`) |
| `make up` / `down` / `restart` | Start, stop, apply changed settings |
| `make update` | Pull the newest image and country lists, restart |
| `make build` | Build the server from your checkout instead of pulling the release |
| `make logs` / `status` / `key` | Watch it, check it, show the identity fingerprint |

**Without Docker:** each release has `rallypoint-server-<version>-linux-x86_64`, `-linux-aarch64` and `-linux-armv7` downloads. Unpack and run `rallypoint-server run`; settings are environment variables (see [`.env.example`](.env.example)). Linux builds run on Raspberry Pi OS Bookworm and newer. Pi Zero and Pi 1 (armv6) aren't covered.

Settings live in `.env`: the port, and `ALLOWED_COUNTRIES` to accept only some countries. Local-network addresses are refused by the country limit unless `ALLOW_PRIVATE=true` (LAN-only servers).

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
| `deploy/` | Ansible playbook and the `rallypoint` command for setting up a server |
| `engine/` | Submodule: the macOS/Windows engine port |
| `patches/` | Our engine changes, applied with `patches/apply.sh` |
| `mods/` | Mod and map-pack lists |

## Known limitations

- Some long online games drift out of sync. We're on it, and sync logs and debug logs are how you can help.
- No Linux engine and no Intel Mac engine. Sorry, Intel Mac folks.
- Mods and the Epic Showdown pack are downloaded in your browser, not inside the launcher.
- Game speed can be changed in skirmishes only, not in online games.
- Not built yet: save and load in multiplayer, controlling allies' units, co-op, bringing an out-of-step player back mid-game.
- The first connection to a relay trusts whoever answers (like SSH); after that its identity is checked.

## Credits and legal

Rallypoint stands on [Electronic Arts' GPL release of the Generals source](https://github.com/electronicarts/CnC_Generals_Zero_Hour), [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) and the macOS port by **dvcdsys** ([original](https://github.com/dvcdsys/GeneralsGameCode-macOS)), which our engine is forked from ([fork](https://github.com/michaelmurfy/GeneralsGameCode-macOS)). The engine and our patches are GPL-3.0 with EA's additional terms (see `engine/LICENSE.md` and [`patches/LICENSE.md`](patches/LICENSE.md)). The launcher, relay server and scripts are MIT licensed ([`LICENSE`](LICENSE)); the launcher runs the engine as a separate program. Not endorsed by or affiliated with Electronic Arts. *Command & Conquer* and *Generals* are trademarks of EA. Steam downloads use the open-source (GPL-2.0) [DepotDownloader](https://github.com/SteamRE/DepotDownloader), fetched at runtime and not bundled. Releases bundle the engine; its complete source is `engine/` at the tagged commit plus `patches/`.
