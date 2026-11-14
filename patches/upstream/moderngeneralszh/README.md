# ModernGeneralsZH snapshot

A pinned copy of the parts of [JaChriCaeTob/ModernGeneralsZH](https://github.com/JaChriCaeTob/ModernGeneralsZH) that the Vulkan renderer and the updated graphics need, so building never fetches from that repository.

- **Commit:** `040bf2cd` (their work starts at `2bfaea6`, an upstream TheSuperHackers commit of 2026-10-06).
- **Licence:** GPL-3.0 with EA's additional terms, same as the engine (`LICENSE.md` here is their copy). Credit them wherever this is used.
- **Nothing here is built yet.** Phase 1 (`../../0079-windows-x64-build.patch`) takes only the 64-bit pieces. Phases 2 and 3 port from this folder.

What is in it:
- `Core/Libraries/Source/WWVegas/WW3D2/gfx_*`: the renderer seam (`gfx_d3d8_map.h`), the null backend, and the native Vulkan backend with its post-processing and shadow code. `gfx_vk_shaders.h` is generated SPIR-V; the GLSL it comes from is in `Shaders/`, rebuilt by `scripts/compile-vk-shaders.ps1`.
- `fork-changes-2bfaea6..040bf2c.diff`: everything else they changed against that upstream base (water, shockwaves, shader manager, shadows, options), minus the generated SPIR-V header and the miniaudio header (miniaudio is fetched pinned by commit in `Dependencies/Miles/CMakeLists.txt`, which is its own repository, public domain).
- `tools/dxvk/`: their DXVK patch (allows shader model 2 and 3 on Direct3D 8), needed only for the water effects on the classic renderer.
- `tools/gen_scorch.py` and `assets/Art/Textures/exscorch01*.dds`: their procedural scorch atlas (colour and height, no game data). The textures were generated once with this script (numpy 2.0.2, Pillow 11.3.0) and are kept here so the build fetches nothing; the release copies them into the engine bundle (`assets/`), and the engine reads them only with the Enhanced renderer (`GEN_ASSETS`).
- `docs/`: their port notes and the list of renderer call sites.

Still fetched at build time, each pinned: the DX8 headers (`min-dx8-sdk`, as before), miniaudio (commit), DXVK (version and SHA-256), FFmpeg (vcpkg baseline). Vulkan headers will be needed in phase 2 and should be vendored then.
