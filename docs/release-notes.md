## Rallypoint 0.8.1

**Computer players no longer stand around.** Units that get boxed in by a chokepoint now get helped out, and the Stronger AI setting is easier to follow online.

- **Stuck AI units.** If a computer player's unit has somewhere to go but has not moved for 8 seconds, it gets a new path. Still stuck 8 seconds later, it is nudged a couple of cells towards its goal. Still stuck after that, its order is dropped so the rest of its team carries on. Your own units are never touched, and it works on every map and mod.
- **Not compatible with 0.8.0 in multiplayer.** This changes how computer players move, so everyone in a room needs 0.8.1.
- **Stronger AI follows the room.** Online, the Play options show the room's level. Only the room master can change it, and everyone else sees why it is locked.
- **Skirmish keeps its own level.** The pill in the skirmish setup is yours alone and ignores the room.
- **Tidier Room card.** One compact row on the Online page, with the save status beside the menu and plainer wording.
- **Still stuck somewhere?** Settings > Multiplayer > Debug logs includes `engine.log`, which names the unit, the place and what the game tried.

## Rallypoint 0.8.0

Computer players that put up more of a fight, in skirmish and online: a Stronger AI setting that works in every mod, set per room by its master, plus eight community fixes to computer players and pathfinding, mods' own general names in the skirmish setup, and Control Bar Pro fixes for Shockwave.

## Rallypoint 0.7.3

A tidy-up of the Maps and Online pages: a compact sort menu, a search box you can clear, readable player filters, packs that show what you already have, and a **Start the game** button once you are connected.

