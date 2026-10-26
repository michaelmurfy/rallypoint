## Rallypoint 0.3.4

- **Fixes from the upstream game code.** The engine is based on the open-source community project TheSuperHackers/GeneralsGameCode, and 539 newer upstream commits had never been merged. This release takes the ones that apply cleanly: particle effects no longer affect the game's random numbers (a likely cause of Windows and Mac games drifting apart), checksum checks no longer give false alarms at the short intervals the sync logs use, the LAN lobby removes a player who leaves, and a few network clean-ups. `docs/upstream-fixes.md` lists what was taken and what could not be applied yet.
- **Update every player together:** the game version changes, so players on older builds cannot join games with this one.

---

## Rallypoint 0.3.3

- **Play page:** pick the game from a drop-down (Zero Hour, Shockwave, Shockwave Chaos, Cold War Crisis) and tick any extra mods on top of it. A notice with a spinner now appears in the middle of the window while the game starts.
- **Online page:** tidier, with saved servers that can remember the room password (one press of Connect rejoins), a player name for the game's lobby (it defaults to your computer's name; on macOS the lobby name was always "M" and is fixed), and plain explanations when it can't connect (wrong address or port, a firewall, a wrong password, a full room).
- **Lobby going stale:** if the connection to the server drops, Rallypoint now rejoins within about 8 seconds, keeps your address in the room, and a restarted launcher takes over its old session instead of being locked out. Start the game again if the page says your address changed.
- **Map organiser:** the new Maps page lists every map with search and pack filters and tick boxes, so a 780-map pack doesn't bury the ones you want. Adds the Epic Showdown 707 map pack (download it from ModDB and choose the file). Switched-off maps are moved aside, not deleted.
- **Bundled mods** such as Control Bar HD are tagged "Bundled".
- **Server:** runs on the host network (less delay per game packet), logs handling time, and a rejoining player keeps the same address. After `make update`, open the UDP port in the host firewall if you use one.
- **Multiplayer sync:** more logging to find why a Windows and a Mac game still differ at the first frame (terrain height of objects at the edge of the map); the checksum interval returns to normal after the first 300 frames, which should remove stutter while sync logs are on.

---

## Rallypoint 0.3.2

A diagnostic build for the multiplayer desync. The Windows and macOS games still differ at the first frame, in the terrain height of objects that sit at the edge of a map (18.125 on the Mac, 25.0 on Windows). The sync logs now fingerprint the terrain height data and record the first off-map height lookups, so the next comparison shows whether the two machines load different height data or read it differently. Keep Settings > Multiplayer sync logs on. No server change.

---

## Rallypoint 0.3.1

Rallypoint 0.3.0 was never published: its macOS build failed to compile. This release has everything in it (see 0.3.0 below) plus the fix, so use this one. As before, **update the server and every launcher together**.

---

## Rallypoint 0.3.0

The first release that is no longer marked as a prerelease. Multiplayer between macOS and Windows has not yet been played from start to finish, so please keep Settings > Multiplayer sync logs on and report what you find.

- **Multiplayer sync fixes.** The Windows and macOS games differed from the very first frame: Windows used its own sine, cosine and inverse square root, and the height of objects placed off the map came from a screen-size-dependent copy of the terrain. Both are fixed, and the sync logs now record enough to find any further difference.
- **Security fixes.** A malformed report could crash the relay and every room on it; recorded logins can't be replayed to lock a player out; archives with links are refused before anything is unpacked; mod installers can't exhaust memory; other programs on your computer can't take over the game's connection; a server only receives its own serial.
- **Update the server and every launcher together.** The login handshake changed. Run `make update` on your server (it pulls the released image).
- **Mods and interface.** Cold War Crisis is in the mod list (it can't be combined with Shockwave or Chaos), a tile mod picker, smoother animations, and no autocorrect in text fields.
- **Updates.** Rallypoint can update itself on macOS and Windows: download, verify, restart, and show what's new.
- **Server.** Joins, leaves, refusals and each desync are logged; the country limit refuses private addresses unless `ALLOW_PRIVATE=true`.

---

## Rallypoint 0.2.0-beta.7

Still a **prerelease**. Security fixes from a review.

- **Update the server and every launcher together.** The login handshake changed, so old launchers cannot join a new server and new launchers cannot join an old one. Run `make update` on your server after installing this version.
- **Relay:** a malformed sync report could crash the relay and every room on it; fixed. A recorded login can no longer be replayed from another address to lock a player out. The country limit now refuses private addresses unless `ALLOW_PRIVATE=true` (behind Docker, proxied IPv6 clients looked private and all got through), and judges IPv4 clients on an IPv6 socket by their IPv4 address.
- **Launcher:** mod and map archives containing links are refused before anything is unpacked. A server only ever receives the serial it issued (the saved serial is kept for your current server, otherwise you sign up again automatically). Other programs on your computer can no longer take over the connection to the game. A forged server-key packet can't drop a connected player. Mod installers can no longer exhaust memory.

Multiplayer through the relay still hasn't been played from start to finish.

---

## Rallypoint 0.2.0-beta.6

Still a **prerelease**. A diagnostic build for the multiplayer desync.

- **Sync logs show more.** Beta.5 still went out of sync at the very start of a game, in the rotation and height of objects that sit on slopes (command centres, derricks, fences). The logs now record the inputs of those, so the next comparison can name the exact value that differs between Windows and macOS. The upgrades field in the logs is fixed too.
- No other changes. Please keep Settings > Multiplayer sync logs on, play until the game desyncs, and send both machines' `sync-logs` folders.

---

## Rallypoint 0.2.0-beta.5

Still a **prerelease**.

- **Fixes the Windows and macOS desync.** The sync logs showed the two games already differed at frame 0: Windows used its own sine, cosine and inverse square root, so every rotated object came out slightly different. Both now use the same code. **Everyone must update**: older builds can't join a game with this one.
- **Server:** the relay image is linked to the repository on GHCR.

Multiplayer through the relay still hasn't been played from start to finish, so please keep sync logs on and report what you find.

---

## Rallypoint 0.2.0-beta.4

Still a **prerelease**.

- **Fixes the crash at game start when Multiplayer sync logs is on** (macOS). Beta.3 crashed as soon as a game began with sync logs enabled; they now write their frame files.
- **Server:** the relay image is published as `ghcr.io/michaelmurfy/rallypoint-relay:latest` (and by version), and `make up` / `make update` now pull it instead of building on the server.

---

## Rallypoint 0.2.0-beta.3

Still a **prerelease**. Mostly about finding the multiplayer desync.

- **Multiplayer sync logs now really work.** Beta.2 still wrote nothing because the engine's build files compiled the log out. Turn on Settings > Multiplayer sync logs on both machines, play until the game goes out of sync, then use Compare. Logs are now taken every 10 frames for the first 300.
- **Cold War Crisis** is in the mod list (download the manual-install .zip from ModDB and choose it). It is a total conversion: ticking it on the Play page unticks Shockwave and Chaos and switches them off.
- **A nicer mod picker** on the Play page, and tidier mod cards.
- **Smoother interface:** pages and the setup wizard ease in, and the blank bar that stayed pinned at the top while scrolling is gone.
- **No autocorrect** in the launcher's text fields.
- **Windows:** desync checksums are written to the launcher log.
- **Server:** the relay now logs joins, leaves, timeouts, refusals and each desync verdict.

Known gaps are unchanged: multiplayer can still go out of sync, and resyncing a player to the host is not built.

---

## Rallypoint 0.2.0-beta.2

Still a **prerelease**. A small build to get the multiplayer sync logs working.

- **Multiplayer sync logs now work.** Settings > Multiplayer sync logs was switched on but the game ignored it, so no logs were written on macOS or Windows. Turn it on for both players, play until the game goes out of sync, then use Compare in Settings to find the first frame and line that differ.
- Builds of 0.2.0-beta.1 can still play with this one: the game simulation itself is unchanged.

Known gaps are unchanged from beta.1 (multiplayer still out of sync after a couple of minutes in some games; resyncing an out-of-step player is not built).

---

## Rallypoint 0.2.0-beta.1

The version number moves to **0.2** because multiplayer changed substantially. Still a **prerelease**: a full multiplayer game through the relay has not been played end to end yet, so please report what you hit.

**Builds from before this one cannot play multiplayer with this one.** The lobby now compares a simulation version and game data, and refuses mismatches instead of starting a game that goes out of sync. Everyone should update.

New since 0.1.0-beta.6:
- **Same simulation on macOS and Windows.** The game's sine, cosine, arc-tangent and similar functions now use one shared implementation, floating point is strict on both platforms, and the lobby check that was disabled upstream is on, comparing a platform-independent simulation version. This is aimed at the "game mismatched straight away" desync between a Mac and a Windows PC.
- **The server watches for desyncs.** Each game reports a checksum of its simulation to the relay, which compares them and tells everyone straight away which frame diverged. The game host is the master: whoever differs from the host is the one out of step. Shown in the game and on the Online tab. Bringing an out-of-step player back in line with the host is not done yet.
- **Multiplayer sync logs** (Settings): record the first ten seconds of a game on both machines and compare them to see exactly where two games first differed.
- **Encrypted relay traffic.** Everything between the launcher and the server, including your serial, room and password proof, is encrypted and authenticated, and the launcher remembers each server's identity and refuses a different one.
- **Connection diagnostics** on the Online tab: latency, jitter, packet loss, traffic, and the server's version, load, rooms and players. The relay now carries only the game's own traffic and limits floods, sign-ups and guessing.
- **Online tab:** separate server and port boxes, a saved server list, and the game only ever sees the relay's address range.
- **macOS:** Fullscreen off now really gives a window; HiDPI text and trackpad zoom fixed; sharper ultrawide command bar.
- **Windows:** the Visual C++ runtime ships with the engine and the engine runs from the game folder, which fixes the crash after start-up and the missing mouse cursor.
- **Settings:** graphics options, Troubleshooting (game files and crash report), and clearer messages that appear next to the button you pressed.
- **Mods:** choose the files yourself, standalone mods and add-ons are listed separately, a mod's own loading picture is shown, and Shockwave's installer unpacks inside Rallypoint.

Known gaps:
- Multiplayer through the relay has not been played from start to finish; the pieces are built and tested.
- Resyncing an out-of-step player to the host mid-game is not built yet.
- No Linux game engine (Proton or Wine is a possible future route) and no Linux launcher download. Apple Silicon only on macOS.
- You need your own copy of Generals and Zero Hour on Steam.

---

## Earlier: Rallypoint 0.1.0-beta.5

Still a **prerelease**. A few pieces have only been tried by the developer (listed at the end), so please report what you hit.

New since beta 4:
- **Zero Hour starts.** The engine was remembering that Weather.ini was missing, then refusing to read it from INIZH.big. It now checks for that archive on disk and loads the Zero Hour folder when it is not already in the working directory. On Windows this reaches the game window.
- **Mods.** Choose the finished download yourself. Standalone mods and add-ons are listed separately.
- **Ultrawide control bar.** Frame art and command icons sit in the same centred 4:3 region as the rest of the UI.
- **Online play hides your real network adapters.** The game only sees the relay's virtual address.

New since beta 3:
- **Zero Hour launches from the real game folder.** The engine is told where Weather.ini lives, and a nested base-game copy is no longer preferred over a sibling install.
- **Shockwave unpacks inside Rallypoint.** The Clickteam installer is extracted and its Windows launcher is not started, so the same mod loads on macOS and Windows. Several mods can be stacked.
- **Sharp command-bar icons download during setup**, with no extra prompt.
- **Higher camera is optional** on the Play tab. Settings still has its own camera height.

New since alpha 3:
- **macOS: fullscreen and any resolution now work.** The macOS game engine was discarding the launcher's settings, so fullscreen, custom resolutions (4K, ultrawide) and mods were silently ignored. Fixed, with native macOS fullscreen.
- **Map packs.** One click installs GenTool's ranked 1v1, 2v2 and 3v3 maps (or all of them), in the setup wizard and on the Mods tab; you can add any other maps archive or folder.
- **Updates.** The launcher checks GitHub for a newer release at start-up (switch off in Settings) and can download, verify and open the installer for your OS.
- **Steam sign-in uses phone approval** (the push notification) by default, with "Enter a code instead" as the fallback.
- **Mods are installed by choosing the finished download** with a file picker (it suggests the file it spots in Downloads). Shockwave and its Chaos add-on are listed.
- **Red "Generals" look**, a crimson app icon, and a cleaner layout, in dark and light.
- **Windows: no stray console windows.**

Also included: the setup wizard, native Steam download without Rosetta, any resolution/aspect ratio with a scaled camera and menus, Quick Skirmish, and your own relay server with rooms and passwords (`make up`).

Known gaps:
- Not yet verified in a real game or live: the widescreen and online-play engine patches, the Steam download with a real account (including Steam Guard prompts), the updater against a live release, and the map-pack zips' internal layout.
- No Linux game engine and no Linux launcher download (build it from source, see the README). Apple Silicon only on macOS.
- You need your own copy of Generals and Zero Hour on Steam.
