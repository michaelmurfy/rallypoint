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
