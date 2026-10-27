## Rallypoint 0.4.6

The update installers stop piling up, and Windows desync hunts get real data.

- **The updater cleans up after itself.** Every start deletes the installer and staging files an update left behind, plus old Rallypoint installers in your Downloads folder (this version or older). An installer for a newer version you haven't run yet stays, and so does everything else.
- **Windows debug zips carry the game's own log.** The Windows game keeps its pathfinding, network and AI messages in `DebugLogFile.txt`. The zip now includes it, so a desync can be compared line by line with the Mac side.

## Rallypoint 0.4.5

Fixes the dead macOS keyboard after switching Spaces. Sync logs now cover the whole game (every 100th frame) and no longer cause false mismatches when only one player has them on. The window opens bigger (1080 × 780), with a roomier map preview.

## Rallypoint 0.4.4

Security release: signed updates, installers kept in a private folder, no Steam sign-in kept on disk, and a debug zip that shows what it holds first. Also new: Play skirmish from a map's preview, Quick Skirmish options (opponents, difficulty), Quick Skirmish starting on Windows, and Enter to connect.
