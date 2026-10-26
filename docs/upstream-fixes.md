# Fixes from TheSuperHackers/GeneralsGameCode

The engine is based on [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) as it was around **2026-05-15** (through the macOS port in `engine/`). Upstream had 539 newer commits by 2026-10-25. Fixes are taken as patches (`patches/0034`-`0040`, each carrying the upstream commit and title in its first line), after checking with `git apply --check` against our tree. Test a candidate with:

```bash
gh api -H "Accept: application/vnd.github.diff" repos/TheSuperHackers/GeneralsGameCode/commits/<sha> > /tmp/c.diff
git -C engine apply --check /tmp/c.diff
```

## Taken

| Patch | Upstream | What it fixes |
|---|---|---|
| 0034 | `82f4b857a` #2796 | Spurious checksum mismatches for disconnected players at low CRC intervals (sync logs run at 10) |
| 0035 | `9a402fbe2` #2742 | Particle systems no longer feed the logic checksum: the logic random numbers they used are drawn whether or not a particle system gets created (it can be refused by detail settings or limits, which differ per machine) |
| 0036 | `50f27e057` #3055 | The same for the Spectre Gunship |
| 0037 | `4a4a1b23f` #3499 | The LAN lobby player is deleted when they leave the lobby |
| 0038 | `12a9b944b` #3311 | A game slot's IP is cleared on reset |
| 0039 | `a8464bcff` #3498 | The disconnect chat message is released after sending |
| 0040 | `5a4187cea` #2708 | The type of incoming game messages is verified |

`0041` raises `kRallypointSimulationId` to 4: 0035 and 0036 change when the logic random numbers are drawn, so builds from before must not join builds from after.

## Does not apply yet (needs commits we do not have)

| Upstream | What | Why it matters |
|---|---|---|
| `ff6ac21aa` #2857 | Map player index to slot index for CRC validation | Checksum attribution in games with AI or empty slots |
| `48e9fe9c8` #3039 | LAN game room hang from long player names | Our lobby name limit is 12 |
| `2d6761cc0` #3488 | Close sockets that fail to bind | Network |
| `9a9b1c2b2` #2736, `b720c3554` + `5943d3856` #3052, `c00312568` | Network command ordering and packet parsing | Lockstep command order |
| `171701e80` #3212, `a82fab89c` #3180 | Mismatches in Generals caused by update and module changes | Generals only, so not needed for Zero Hour |
| #2670 (`f12cfbbe8`, `6f14ae8da`, `aeeb97c61`, `91baad276`, `65dd652bd`) | Upstream's deterministic math: fdlibm via GameMath, `-ffp-contract=off`, Windows x86 and macOS ARM64 checked equal | Our own `DetMath` (patch 0017) does the same job; a switch needs the five commits and the GameMath dependency |

Larger upstream moves since the base (files moved between Generals, Zero Hour and Core, for example #3189 and #3561) are why a straight merge of upstream is a large job: most of our patches touch moved files.
