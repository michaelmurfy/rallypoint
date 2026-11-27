## Rallypoint 1.0.6

**Shadows are softer.** On Linux and Windows 64-bit with the Enhanced renderer, neither a thick cloud nor a low sun turns the ground nearly black.

- **Softer by default.** The shadow keeps most of the light, so a cloud reads as shade, not a hole.
- **Your call.** Settings > Graphics > Enhanced effects has a new **Cloud shadow strength**: subtle, normal or strong.
- **Gentler sun shadows.** The random sun now stays higher and its shadows are a little lighter, so buildings no longer throw long dark stripes over an ultra wide screen.
- **Linux debug bundle is a real zip.** It was a plain tar with a .zip name that nothing could open. It needs the `zip` package, which the installers now ask for.

## Rallypoint 1.0.5

Linux no longer dies at start on GNOME with Wayland: the game ignores a refused keyboard-focus request instead of quitting, fullscreen goes to your primary monitor, and a game that dies at start shows the end of its log instead of vanishing.

## Rallypoint 1.0.4

Linux plays a lot better: one monitor by default (tick Span all monitors in Settings > Graphics for surround), the game's own mouse cursors, a keyboard that keeps up when you switch windows, double-click select and a tidier setup wizard.
