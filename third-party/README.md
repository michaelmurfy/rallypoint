# third-party

Everything the engine build used to download at configure time, copied here so building needs no other repository.
Each directory is a verbatim copy at the pinned commit, except where noted. `patches/0087-vendored-third-party.patch`
points the engine's CMake at these folders (`RTS_THIRD_PARTY_DIR`).

| Directory | Source (commit or version) | Licence |
|---|---|---|
| `gamespy` | TheAssemblyArmada/GamespySDK `07e3d15c500415abc281efb74322ab6d9c857eb8` | BSD-3-Clause (`LICENSE`) |
| `lzhl` | TheSuperHackers/lzhl-1.0 `dfd96e2ca64adaddb35dd4ebadd6add7d5586783` (under `CompLibHeader/`, the layout the engine includes) | none shipped upstream |
| `zlib` | TheSuperHackers/zlib-1.1.4 `ac753eee3990a2f592bd4807ff0b30ff572c2104` | zlib (`README`) |
| `dx8` | TheSuperHackers/min-dx8-sdk `7bddff8c01f5fb931c3cb73d4aa8e66d303d97bc` | Microsoft DirectX 8 SDK excerpt: headers and import libraries only |
| `bink` | TheSuperHackers/bink-sdk-stub `180fc4620ed72fd700347ab837a5271fd0259901` | GPL-3.0 (`LICENSE`): a stub, not RAD's Bink |
| `miles` | TheSuperHackers/miles-sdk-stub `6e32700d7ba4b4713a03bf1f5ffc3b0ac8d17264` | GPL-3.0 (`LICENSE`): a stub, not Miles |
| `miniaudio` | mackron/miniaudio `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` (0.11.25), `miniaudio.h` only | public domain / MIT-0 |
| `dxvk` | doitsujin/dxvk 3.1.1, 64-bit `d3d8.dll` and `d3d9.dll` only (see `dxvk/README.md`) | zlib |

Not vendored, because they are not source we can reasonably carry: the vcpkg baseline and its ports (FFmpeg and
friends, fetched by the 64-bit Windows job), pinned GitHub Actions, and tools the launcher downloads at run time
(DepotDownloader, GenTool icons, map packs). To update an entry, replace its folder from the new commit and change the
table.
