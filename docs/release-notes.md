## Rallypoint 0.7.0-beta.3

**Windows finally gets the 64-bit engine and the Enhanced renderer.** The last two betas left both out of the Windows installer by mistake, which is why the graphics options never showed up there.

- **Windows: the options are there now.** Settings > Graphics shows **Renderer** (Classic or Enhanced) and **Game engine** (64-bit or 32-bit).
- **Windows: this is the 64-bit engine's first real outing.** Earlier betas said they ran it and did not. If a game misbehaves, switch **Game engine** to 32-bit and send the debug logs.
- **Enhanced on Windows is untested by anyone.** It needs a Vulkan 1.3 graphics driver. Classic stays the default.
- **No more black beside the control bar.** On a wide screen the ground now carries on down either side of the bar, where there used to be a black strip with text floating on it.
- **macOS: scorch craters.** The crater textures were missing from the app for the same reason, so that effect did nothing. It works now.
- **macOS: sharper ambient occlusion.** Ambient occlusion quality has a Full resolution choice.

## Rallypoint 0.7.0-beta.2

The Enhanced renderer with tunable glow, colour looks, sharpening and shaded smoke, beta updates as an opt-in, and a tidier launcher with tabbed Settings.

## Rallypoint 0.7.0-beta.1

A 64-bit Windows game engine, built beside the 32-bit one.
