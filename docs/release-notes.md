## Rallypoint 1.0.5

**Linux no longer dies on start.** On GNOME with Wayland the game could flash a black window and vanish, or leave a process doing nothing. Everyone in a room needs 1.0.5.

- **Fixed the crash at start.** The game asked for the keyboard before its window was on screen, and the display server said no, which killed it. It now carries on and asks again once the window is up.
- **A game that dies at start says why.** The launcher shows the last lines of the engine's log instead of letting the window vanish, and no longer leaves a dead process behind on Linux.
- **Right monitor on Wayland.** Fullscreen went to whichever screen the system thought the mouse was on, which Wayland does not tell games. It now uses your primary monitor.

## Rallypoint 1.0.4

Linux plays a lot better: one monitor by default (tick Span all monitors in Settings > Graphics for surround), the game's own mouse cursors, a keyboard that keeps up when you switch windows, double-click select and a tidier setup wizard.

## Rallypoint 1.0.3

Nine more retail rules ported for Red Alert 2: factory exits, city crate drops, the Force Shield, superweapon cursors and sounds, the spy camera, Yuri walls and gates, unselectable buildings and movement tests.
