## Rallypoint 0.5.2

Updates get their own screen, the Play page says what you are about to play, and the Mac loses its terrain grid.

- **Updating takes over the window.** One centred card with a spinner, the status (downloading, installing, restarting) and download progress in MB. Nothing else is useful while it runs.
- **Play shows the game.** The big heading is the game that will start (Zero Hour, Shockwave, Chaos...), with any extra mods you ticked.
- **A tidier Maps banner.** The skirmish tip is a slim bar instead of an oversized icon and loose text.
- **No Boss General in the list.** The game keeps it out of its own menus, so picking it could not start.
- **No grid on Mac terrain when zoomed out.** The terrain's mipmaps were blending neighbouring tiles.
- **Mac keyboard after switching Spaces.** Key presses for the game window now reach the game even when its view lost focus. If the keyboard still dies, the debug zip says which case it was.
- **Sync logs record each player's skill and science points,** so a desync like the one at frame 11100 can be traced to the player.

## Rallypoint 0.5.1

A tidier Play page and skirmish setup (a clearer Maps banner, a calmer skirmish dialog with a colour per player and drop-downs that stay open, Quick Skirmish as its own card), and no more black wedges on Mac terrain.

## Rallypoint 0.5.0

Set up a whole skirmish from the map preview (players, generals read from the game or mod, teams, starts), with Mac audio fading with distance, full names in game messages on Mac, and projectile flight paths using the same maths on Mac and Windows. The app opens at 1200 × 840. Older versions are refused at the lobby.
