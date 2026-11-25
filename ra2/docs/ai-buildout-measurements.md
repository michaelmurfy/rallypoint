# Skirmish AI build-out: before and after (patch 0243)

Harness: `ra2engine --ai-soak --minutes 12 --diff <easy|normal|brutal> --ai-side <soviet|allied>` with `RA2_INSTALL_DIR` set to a real install, passive human, default map (`xmp31s2`). One deterministic run per cell, so small differences between runs are noise (the random streams shift).

## What retail does (read from `gamemd.exe`)

- Team check pass every `TeamDelays` frames (2000 Brutal, 2500 Normal, 3500 Easy) plus 175 frames per house index. One trigger, so up to two teams, per pass.
- A TaskForce entry is admitted by an online factory for its type (`Has_Factories_For_TeamType`, `0x00509610`). No prerequisite check; the unit chooser buys what `CanBuild` allows and the team fills as tech arrives.
- `HouseClass::Update` chooses the next building and the next units every 8th frame (`0x004F9074`).
- Nothing in gamemd reads `BarracksRatio`, `WarRatio`, `RefineryRatio`, `*Limit` or `InfantryBaseMult`, so the AI has one barracks and one war factory unless its plan says otherwise.

## Divergences found

| | Before | After |
|---|---|---|
| Team admitted when | factory stands AND unit prerequisites met | factory stands |
| Next building chosen | every ~7 s | every 8 frames |
| Team timer start | `TeamDelays` for all houses | `TeamDelays` + 175 x house index |

## Numbers (units alive / units built / teams created, first damage to the human base)

| Run | min 6 | min 9 | min 12 | first damage |
|---|---|---|---|---|
| Brutal Soviet before | 14 / 2 / 0 | 18 / 6 / 3 | 24 / 13 / 6 | 11.7 |
| Brutal Soviet after | 22 / 10 / 3 | 23 / 11 / 5 | 39 / 28 / 9 | 7.7 |
| Normal Soviet before | 14 / 2 / 0 | 17 / 5 / 1 | 19 / 7 / 3 | none |
| Normal Soviet after | 14 / 2 / 3 | 15 / 3 / 5 | 17 / 6 / 7 | 9.0 |
| Easy Soviet before | 13 / 1 / 0 | 18 / 6 / 1 | 17 / 6 / 3 | none |
| Easy Soviet after | 17 / 5 / 1 | 18 / 6 / 3 | 17 / 7 / 5 | 11.3 |
| Brutal Allied before | 13 / 5 / 2 | 15 / 8 / 7 | 26 / 22 / 9 | 11.7 |
| Brutal Allied after | 13 / 5 / 2 | 15 / 7 / 4 | 23 / 22 / 10 | 11.0 |
| Normal Allied before | 13 / 5 / 2 | 15 / 7 / 5 | 23 / 20 / 7 | 11.5 |
| Normal Allied after | 13 / 5 / 2 | 13 / 5 / 5 | 19 / 17 / 7 | 10.9 |
| Easy Allied before | 12 / 4 / 1 | 13 / 5 / 2 | 12 / 13 / 3 | 9.8 |
| Easy Allied after | 12 / 4 / 1 | 12 / 4 / 2 | 12 / 13 / 3 | 10.0 |

Allied teams never waited on prerequisites (the Allied guard team needs only the barracks), so only the building cadence and the shifted random stream moved them: buildings at minute 12 are within one of the old run, and the army at minute 12 is 3 to 4 units smaller on Brutal and Normal. That is inside the run-to-run spread of a single seed, but it is not an improvement and is not claimed as one.

## Still open

- One barracks and one war factory feed every team; retail does the same, but the army only grows as fast as those two queues. Unit build time and the 0.8 `MultipleFactory` discount are as the player's.
- A computer house with no `Factory=` building should sell everything and send its units hunting (`AI_Building_Strategy` last legs, `Fire_Sale`/`All_To_Hunt`). Skirmish has no such path yet.
- `RatioAITriggerTeam` (the pass's chance to use triggers at all) and `MaximumAIDefensiveTeams` handling are not compared.
- Superweapon use (Iron Curtain, Chronosphere) exists but was not measured.
