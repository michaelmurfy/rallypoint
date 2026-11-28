## Rallypoint 1.0.7

**Red Alert 2 skirmishes, rebuilt.** The setup dialog now matches the Generals map viewer: a full-size map picture with the start positions on it (click one to give it to the selected player), arrows to step through the maps, and hover previews in the map list.

- **Stronger computer players.** Stronger AI no longer just builds faster: boosted opponents take extra supplies, rebuild sooner, and attack with less in the bank, so each level plays differently.
- **Spy plane flies straight.** The overfly used to circle its target instead of covering the area; it now flies a straight corridor like retail.
- **Minimap you can read.** Hovering the radar with a selection shows the move cursor, and the terrain no longer has black speckles.
- **Ships sail to shore.** Water orders near land no longer die outright: ships sail to the nearest reachable shore. Terror Drones animate while moving.

## Rallypoint 1.0.6

**Shadows are softer.** On Linux and Windows 64-bit with the Enhanced renderer, neither a thick cloud nor a low sun turns the ground nearly black.

- **Softer by default.** The shadow keeps most of the light, so a cloud reads as shade, not a hole.
- **Your call.** Settings > Graphics > Enhanced effects has a new **Cloud shadow strength**: subtle, normal or strong.
- **Gentler sun shadows.** The random sun now stays higher and its shadows are a little lighter, so buildings no longer throw long dark stripes over an ultra wide screen.
- **Linux debug bundle is a real zip.** It was a plain tar with a .zip name that nothing could open. It needs the `zip` package, which the installers now ask for.
- **Red Alert 2: see enemy health.** Hovering an enemy building now shows its health bar, like hovering an enemy unit already did.

## Rallypoint 1.0.5

Linux no longer dies at start on GNOME with Wayland: the game ignores a refused keyboard-focus request instead of quitting, fullscreen goes to your primary monitor, and a game that dies at start shows the end of its log instead of vanishing.

## Rallypoint 1.0.4

Linux plays a lot better: one monitor by default (tick Span all monitors in Settings > Graphics for surround), the game's own mouse cursors, a keyboard that keeps up when you switch windows, double-click select and a tidier setup wizard.
