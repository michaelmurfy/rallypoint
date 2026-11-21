## Rallypoint 0.10.0-beta.2

**Red Alert 2 and Yuri's Revenge join the launcher.** Pick Generals or RA2 from the sidebar, find or download a Yuri's Revenge install, and play skirmish or online through the same relay.

- **Play, Maps and Online for RA2**, with official and `.mmx` maps, classic map packs, a clickable lobby map, Ready, and Yuri as a side.
- **Yuri units that matter in multiplayer:** Slave Miner economy, Gattling stages, Magnetron beam, Floating Disc drain, mind control, Grinder and Bio Reactor fates.
- **Lobby options from CnCNet** (crates, superweapons, allies, AutoRepair, crate density) and a first-start wizard that remembers the last game.
- **macOS is Apple Silicon only** for the RA2 engine (no Intel half). Generals was already arm64-only on Mac.
- **Windows build fixed** so the RA2 engine compiles under MinGW again (`far` / `near` names clash with Windows headers).
- **This is a beta, not an `ra2-alpha` tag.** Same updater channel as other 0.10.0 betas. Generals multiplayer stays compatible with 0.10.0-beta.1.

## Rallypoint 0.10.0-beta.1

**Make your own maps.** EA's World Builder now opens from the Maps page on Windows, with your mods loaded.

- **Open World Builder** at the top of the Maps page. It shows which game or mod it will load ("Open World Builder · Shockwave"); the arrow beside it picks another one you have installed.
- **Edit on each of your maps** opens that map straight in the editor. Saved maps land in your usual Maps folder, so they show up in the list.
- **Your mod's objects, terrain and water** are in the editor, loaded the same way the game loads them.
- **Windows only.** The editor is built on Microsoft's MFC, so there is no Mac or Linux version. There the button is greyed out and says why.
- **Untested on a real PC.** It builds and ships in the installer, but nobody has made a map with it yet. This beta is for exactly that: if it fails to start or a mod's objects are missing, send the debug logs.
- **Windows updates stay where you installed.** An app outside the default folder was updated into the default one and kept offering the same update.
- **Compatible with 0.9.1 in multiplayer.** Nothing here changes the simulation.

## Rallypoint 0.9.1

Big games run faster: about a third less simulation time late in a large game, a machine that falls behind skips a picture instead of slowing the whole room, and `[perf]` lines in `engine.log` show where the time went.

## Rallypoint 0.9.0

Rallypoint runs on Linux: a native Vulkan engine with `.deb`, `.rpm` and `.tar.gz` packages, the Steam copy found or downloaded there, a Browse button for the game folder, and no more crash on maps with more than 255 script counters.
