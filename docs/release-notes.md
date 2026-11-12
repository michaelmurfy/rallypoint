## Rallypoint 0.7.0-beta.2

**The game can look a good deal newer now, on Windows and macOS, and it is a beta.** Turn it on in **Settings > Graphics > Renderer > Enhanced**. Classic stays the default and is one click back.

- **It only changes the picture.** The simulation is untouched, so Enhanced and Classic players share a lobby, and so do Mac and Windows.
- **Enhanced renderer.** Bloom, ambient occlusion, light from explosions, shockwave ripples, scorch craters and FXAA. Windows (64-bit engine, Vulkan 1.3 driver) also gets real shadow maps, new water and cloud shadows.
- **Better smoke.** Smoke and dust are darker near the ground and lighter on top, so a column has some depth. Dark smoke no longer cuts a hard line into the ground.
- **Glow you can tune.** Subtle, Normal or Strong. On macOS a small bright light keeps a steady glow instead of flickering.
- **Colour looks.** Standard, Vivid, or Cinematic (cool shadows, warm highlights).
- **Sharpening and vignette.** Both off until you ask. The vignette leaves menus and the control bar alone.
- **Presets.** Performance, Balanced and Cinematic set everything in one go. Every effect still has its own switch under **Enhanced effects**.
- **Betas are now opt-in.** From this version on you only get full releases unless you turn on **Settings > Updates > Get beta releases**. If you are on a beta with it off, you stay there until the next full release.
- **A calmer updater.** What's new opens inside Rallypoint, **Skip this version** puts the banner away until something newer exists, and a launcher left open looks again every six hours.
- **No more half-published updates.** A release whose installer is still uploading is not offered until it is ready.
- **A tidier launcher.** Settings is four tabs instead of one long scroll, and the Enhanced effects are grouped by what they change. Mods shows what you have first, and each mod's buttons are in the order you use them: download, then choose the file.
- **Honest status.** It builds on both systems and the Windows shaders are checked by the compiler. Nobody has played a full game with it yet, least of all on a Mac, so please say what you see. If the screen looks wrong, switch back to Classic and send the debug logs.
- **Credit.** The Vulkan renderer and its effects come from the ModernGeneralsZH fork by JaChriCaeTob.

## Rallypoint 0.7.0-beta.1

Windows got a 64-bit game engine (miniaudio, FFmpeg and DXVK in place of the old 32-bit parts), shipped beside the 32-bit one, with a switch in Settings > Graphics > Game engine.

## Rallypoint 0.6.1

The map picture that follows the mouse only shows over a map's title, not the rest of the row.
