## Rallypoint 0.1 Beta 1

The first beta: the feature set for 0.1 is in. It is still a **prerelease**, because a few pieces have only been tried by the developer (listed at the end), so please report what you hit.

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
- No Linux game engine yet (the Linux build is the launcher only). Apple Silicon only on macOS.
- You need your own copy of Generals and Zero Hour on Steam.
