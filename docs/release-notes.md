## Rallypoint 0.7.0-beta.4

**A security fix, plus two bugs from beta.3: slow unit voices on 64-bit Windows and a dead keyboard in macOS fullscreen.** Worth updating.

- **Security: the game no longer opens a remote control on your machine.** The engine we build on included a control server on your computer, switched on, with no password. It could pause the game and give orders. Any program on your computer, or a web page you had open, could reach it, even in a multiplayer game. It is now left out of the game entirely, and the launcher tells the game to keep it off as well.
- **Windows 64-bit: unit voices sound right again.** A selected unit could answer slow and muffled, at the speed of whatever sound played before it. Voices now play at their own speed.
- **macOS: the keyboard keeps working in fullscreen.** The Dock and menu bar no longer slide in when the pointer reaches a screen edge to scroll the map. Clicking them handed the keyboard to another app. If keys still die, `engine.log` now says why. Send the debug logs.

## Rallypoint 0.7.0-beta.3

The 64-bit engine and Enhanced renderer finally made it into the Windows installer, the ground now fills the space beside the control bar on wide screens, and macOS got scorch craters and full resolution ambient occlusion.

## Rallypoint 0.7.0-beta.2

The Enhanced renderer with tunable glow, colour looks, sharpening and shaded smoke, beta updates as an opt-in, and a tidier launcher with tabbed Settings.
