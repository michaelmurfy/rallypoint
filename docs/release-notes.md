## Rallypoint 0.5.1

A tidier Play page and skirmish setup, and no more black patches on the Mac terrain.

- **Skirmish setup is easier to find.** The Maps page opens with a banner, and every map has a clear **Skirmish** button.
- **A calmer skirmish dialog.** Each player gets a colour that matches their start on the map, the map's details sit on one quiet line, and the map's on or off state is a single switch. Drop-downs no longer close on the first click.
- **A tidier Play page.** Quick Skirmish is its own card ("a random match with a random map against the computer") with a tip pointing to Maps. Options explain themselves in one line each.
- **No more black wedges on Mac terrain.** They covered slopes and roads, and came from the terrain's 3-way blend pass. The few tiles that used a second noise pass now use one.

## Rallypoint 0.5.0

Set up a whole skirmish from the map preview (players, generals read from the game or mod, teams, starts), with Mac audio fading with distance, full names in game messages on Mac, and projectile flight paths using the same maths on Mac and Windows (a Chaos game desynced around frame 6600 without it). The app opens at 1200 × 840. Older versions are refused at the lobby.

## Rallypoint 0.4.6

Update installers stop piling up, and Windows debug zips carry the game's own log. Rallypoint now clears what an update leaves behind at every start, and the Windows zip includes `DebugLogFile.txt` so a desync can be compared with the Mac side line by line.
