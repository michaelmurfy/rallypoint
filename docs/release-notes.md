## Rallypoint 0.9.1

**Big games run faster.** Less time in the simulation, and when a machine falls behind it drops a picture instead of slowing everyone in the room.

- **About a third less simulation time** in the late stages of a large game (measured on the 0.8.3 Art of Defense replay that prompted this: three players, Shockwave Chaos, tens of thousands of objects).
- **A late machine no longer drags the room down with it.** When the simulation is more than half a frame late, the next loop skips drawing (never two skips in a row). The rate shown in a network game is frames over time. `GEN_NO_PICTURE_SKIP=1` turns the skip off.
- **`[perf]` lines in `engine.log`** every 1000 frames (logic ms, picture ms, pictures a frame, worst frame, objects), so the next slow game says where the time went and on which machine.
- **Mods load reliably** when several archives share the same file name (a standard-library change that would have broken Shockwave on the next Xcode).
- **Tried on Windows** as well as Mac: Shockwave Chaos skirmish, and the AoD replay through the busy stretch at about 29 frames a second with picture-skip. Linux packages are built; mods and online play on Linux have also been tried.
- **Compatible with 0.9.0 in multiplayer.** Nothing here changes the simulation.

## Rallypoint 0.9.0

**Rallypoint runs on Linux.** A native engine, not Wine or Proton, with packages for Debian, Ubuntu, Fedora and everything else.

- **Linux engine.** Our own Vulkan renderer, an X11 window (XWayland on Wayland desktops) and ALSA sound. Fullscreen works at any resolution, and the menu font ships with the game.
- **Three downloads.** A `.deb` for Debian and Ubuntu, an `.rpm` for Fedora, and a `.tar.gz` that installs into your home folder for CachyOS, Arch and the rest. You need a 64-bit PC, a Vulkan driver and glibc 2.39 or newer (Debian 13, Ubuntu 24.04, Fedora 40).
- **Your Steam copy is found on Linux**, the Flatpak Steam included, and the launcher can download the game from Steam there too.
- **Updates on Linux are by hand.** Rallypoint tells you about a new release and opens the download page. Install the new package over the old one.
- **Linux is new.** It has been played on one Debian 13 machine. Skirmish works; mods and online games there are untried. If something breaks, send the debug logs.
- **Browse for your game folder.** The game folder field has a Browse button, on every system.
- **Busy maps no longer crash.** A skirmish on a map with more than 255 script counters crashed on its first frame. On all systems.
- **Not compatible with 0.8.3 in multiplayer.** The counter fix changes the simulation, so everyone in a room needs 0.9.0.

## Rallypoint 0.8.3

Big battles run much faster (sound no longer holds up the game on a Mac, and the pathfinder uses its faster code from the first frame), computer players shoot their way into walled-in bases, and Mac screenshots, Mac map transfers and a Windows crash on exit are fixed.

## Rallypoint 0.8.2

Computer players follow their attack routes properly (no walking backwards to start, no queues at waypoints, stuck units keep their orders), and the launcher got more colour, a proper brand block and more depth.
