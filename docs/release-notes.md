## Rallypoint 0.7.2

Four fixes from a long Mac against Windows game, and releases that build in a fraction of the time.

- **64-bit Windows no longer crashes mid-game.** The sound system ran the game's end-of-sound code on its own thread, and about four minutes in the two collided. It now runs on the game's thread, as it already did on macOS.
- **Control Bar Pro lines up.** The command icons sit in their frames again on wide screens, on macOS and Windows.
- **Cloud shadows you can see through.** With the Enhanced renderer on Windows a thick cloud turned the ground nearly black. Shadows are now about half as strong. `CloudShadowStrength` in `Options.ini` still overrides it.
- **Crash dumps arrive intact.** The debug bundle used to scrub your home folder out of the Windows crash dump too, which wrecked it. Dumps and other binary files are now left alone.
- **Faster releases.** Builds reuse what has not changed instead of starting from nothing each time, so a fix reaches you sooner. The simulation maths is also checked with both Windows compilers on every change, not only at release.

## Rallypoint 0.7.1

A small release: the **Command bar** choice (sharp icons or Control Bar Pro) moved onto the Control Bar HD card on the Mods page, where people look for it. It is still in Settings > Graphics too.

## Rallypoint 0.7.0

The first full 0.7 release: a 64-bit game engine on Windows, an Enhanced renderer with tunable looks, Control Bar Pro, a resolution picker and more starting money in the skirmish setup, and a game that no longer opens a remote control on your computer.
