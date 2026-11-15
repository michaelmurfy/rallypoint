## Rallypoint 0.8.3

**Big battles no longer crawl, and a handful of things that were quietly broken on the Mac now work.** Most of this came out of one long game on Twilight Flame and the logs from both ends of it.

- **Big battles run faster.** With lots of units the game used to slow to a crawl. On a Mac, more than half of that was sound: every single effect made the game wait for the audio system. Sounds are now handed off in the background. The pathfinder also uses its faster code from the first frame instead of part-way through a game. In our test battle the game ran nearly three times as fast.
- **Listen out.** The sound changes could not be checked by ear before release. If effects, panning or the music volume sound wrong on a Mac, send the debug logs.
- **Armies shoot their way in.** A computer player whose route into a base was closed off with buildings used to park its whole army outside. Its units now attack the nearest building in the way, including ones under camouflage.
- **Screenshots no longer break your clicks (Mac).** After Cmd+Shift+4 the game thought Shift and Command were still held, so clicks attacked instead of repairing, units would not enter buildings and hotkeys stopped working. Very likely the cause of many "dead keyboard" reports.
- **Map transfers work on a Mac.** Joining a game on a map you did not have ended with "Unable to transfer the map". The map arrived fine; the Mac could not save it.
- **No crash on exit (Windows).** Closing the game with the Load Game screen open could crash it.
- **No purple.** The Settings headings are back to their old colour.
- **Not compatible with 0.8.2 in multiplayer.** Computer players and pathfinding behave differently, so everyone in a room needs 0.8.3.

## Rallypoint 0.8.2

Computer players follow their attack routes properly (no walking backwards to start, no queues at waypoints, stuck units keep their orders), and the launcher got more colour, a proper brand block and more depth.

## Rallypoint 0.8.1

Computer players' units that get boxed in are helped out (a new path, then a nudge, then their order is dropped), and the Stronger AI setting follows the room online while skirmish keeps its own level.
