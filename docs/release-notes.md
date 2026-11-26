## Rallypoint 1.0.3

**Nine more retail rules ported for Red Alert 2.** Everyone in a room needs 1.0.3.

- **ExitCoord factory doors.** Units roll out of the factory door instead of the building center.
- **CrateBeneath death drops.** Destroyed city and tech buildings drop a crate where they fell.
- **ForceShield end-to-end.** The Force Shield superweapon goes live with proper targeting feedback.
- **Superweapon cursors, sounds and radar.** Targeting cursors, firing sounds, animations and radar pings audited against retail.
- **Spy camera.** The spy plane's camera flash honors its weapon Damage/Range and frame count.
- **Yuri wall and Allied gate.** Both rosters build their wall and gate the way retail does.
- **Selectable buildings.** Buildings flagged `Selectable=no` can no longer be picked up by clicks or boxes.
- **Movement hardening.** Regression cover for ramp data and terror-drone re-attacks, plus a live pixel check that tanks pitch on slopes.

## Rallypoint 1.0.2

**Four more retail rules ported for Red Alert 2.** Everyone in a room needs 1.0.2.

- **Immune buildings.** Civilian huts and the like (`Immune=yes`) no longer take damage from any weapon, and the AI leaves them alone.
- **CloakingSpeed fade.** Submarines re-cloak gradually over their retail fade length instead of vanishing instantly.
- **GateCloseDelay.** The Allied gate holds its last open stage for the retail delay before closing.
- **RevealToAll and RadarVisible.** Superweapon buildings ping every house's radar when built or captured, and tech buildings show on the radar through the shroud.

## Rallypoint 1.0.1

**Red Alert 2 fixes and a smoother map dock.** Everyone in a room needs 1.0.1.

- **Yuri Clone mind control without force-fire.** A selected Yuri Clone shows the Attack cursor on any capturable unit — including civilian cars — and captures it with a plain click.
- **Smarter skirmish AI.** Computer players follow retail team admission and building cadence.
- **Tank Bunker and a sharper Yuri roster.** Garrisoned Tank Bunkers, the audited Yuri roster, TypeImmune rules and Psychic Sensor disguise detection.
- **Real sidebar names.** Cameo tooltips use the string-table names, and ships no longer wait for a War Factory.
- **Menu slide.** Shell pages slide the way the original runs them, for skirmish, lobbies and movies.
- **Compact map dock.** One panel, summed-up transfers, self-clearing results.
- **Linux: Arch/CachyOS installer** and self-updating `~/.local` installs.

## Rallypoint 1.0.0

**Red Alert 2 and Yuri's Revenge are in, and they play online.** Rallypoint now runs both games from one launcher, and 1.0.0 is the first stable release with Red Alert 2.

- **Red Alert 2 and Yuri's Revenge.** A new engine plays both from your own Steam copy. Switch games from the sidebar and the whole launcher changes with it: maps, skirmish setup, settings and the lobby.
- **Online play.** Rooms, the lobby and games go through the same encrypted relay as Zero Hour. Everyone in a room needs 1.0.0.
- **Send a map to the room.** Press **Send** on any map, for either game. Players choose to accept it, it is checked and unpacked safely, and nothing is sent unless they say yes. The relay server needs updating for this.
- **Menus feel like the original.** Buttons press, the menu slides away and back in with the original sounds, and the WARNING needle sweeps at the right pace.
- **Fairer fights.** Damage, build times, ore growth and veterancy now round the way the original game does. Ore grows about three times faster than before, and the Industrial Plant makes vehicles cheaper. Harvesters pick the richest ore, groups move the way the game moves them, and Battle Fortresses flatten every wall.
- **Minimap clicks.** Left click sends your selected units, right click only scrolls the view.
- **Graphics options for Red Alert 2.** Pick the scaling filter, sprite smoothing, a wider smooth zoom, glow, dark corners and a colour look. All off until you turn them on.
- **Yuri, Chrono and the rest.** Yuri has his own sidebar and loading screen, bridges break and repair, aircraft fly and crash properly, Chrono Miners teleport home with a visible warp, garrisons eject on click.
- **Steadier builds.** Release checks no longer fail for network or cache reasons and finish in about half the time.

## Rallypoint 0.9.1

Big games run faster: about a third less simulation time late in a large game, and a machine that falls behind drops a picture instead of slowing everyone in the room. Compatible with 0.9.0.

## Rallypoint 0.9.0

Rallypoint runs on Linux, with a native engine and packages for Debian, Ubuntu, Fedora and everyone else (`.deb`, `.rpm`, `.tar.gz`). Busy maps with many script counters no longer crash on every system. Not compatible with 0.8.3 in multiplayer.
