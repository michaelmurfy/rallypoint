## Rallypoint 0.6.0

Skirmishes start where you asked them to, with the money you choose, and the map list is easier to browse.

- **Maps with square brackets in the name now work on macOS.** `[RANK] Desert Vortex ZH v1`, `Mt Pinatubo` and friends were invisible to the game, so it quietly started a different map. That is also what crashed some 8-player setups.
- **No more crash from too many players.** A skirmish table is cut to the map's start positions, and the game closes any extra computer places. If a map can't be found, the engine log now says so.
- **Starting money.** A new menu beside Speed in the skirmish setup: $5,000 to $250,000, or the game's own setting.
- **Pack filter with tick boxes.** Tick one or several packs to show only their maps. It only filters the list.
- **Map preview on hover.** A small picture of the map follows the mouse over each row.
- **Smaller fixes.** Close stays on the right for official maps, and text can't be selected outside fields and logs.
- **Server setup as a regular user.** The Ansible playbook works from a user that can sudo (it asks for the sudo password and adds you to the docker group), replaces a hand-made Docker apt source, and restarts SSH instead of reloading it, which on Debian 13 could cut the server off.

## Rallypoint 0.5.10

Starting money for skirmishes and the regular-user server setup (both are in 0.6.0 above).

## Rallypoint 0.5.9

Every game's official maps joined one list, each tagged **Official** and with its game, and setting up a skirmish on one switches "Play with" to the game that has it.

## Rallypoint 0.5.8
