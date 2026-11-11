## Rallypoint 0.7.0-beta.1

**Windows now runs a 64-bit game engine, and it is new, so this is a beta.** It starts the same way: pick Play and go. If anything misbehaves, **Settings > Graphics > Game engine** switches back to the 32-bit one, and the next game uses it.

- **64-bit engine.** Audio, movies and Direct3D 8 are replaced with 64-bit parts (miniaudio, FFmpeg, DXVK). Not played in a real game before this release, so please say how it goes.
- **Mixed games.** The simulation is meant to match macOS and the 32-bit engine bit for bit. If a lobby refuses you, or a game drifts apart, send the debug logs from Settings > Multiplayer.
- **Two engines in one install.** The Windows installer carries both, so it is a bigger download.
- **Credit.** The 64-bit work comes from the ModernGeneralsZH fork by JaChriCaeTob. A pinned copy of its Vulkan sources sits in the repo for the renderer work to come.

## Rallypoint 0.6.1

The map picture that follows the mouse only shows over a map's title now, not over the Skirmish button or the rest of the row.

## Rallypoint 0.6.0

Maps with square brackets in the name work on macOS, a skirmish never has more players than the map, starting money, tick boxes for the pack filter, a map picture on hover, and server setup as a regular user.
