## Rallypoint 0.1.0-beta.5

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
