# Rallypoint launcher: design contract

Every page and the setup wizard use these tokens from `style.css`. Add a token here before using a new value.

## Direction

A game launcher for a military RTS, in shades of red as the brief asked for ("a Generals theme"): dark by default, with a light theme when the system asks. The ground is a tonal scale of oxblood, the one action is a signal red, and the app icon (a crimson tile with a gold shield) is the same family. The signature detail is the rank chevron from the icon, used as the wizard's step marker.

It uses the platform's own UI font on purpose (a desktop app should look native). Hierarchy comes from size and weight: 28px/750 page titles, 22px/750 on the one hero, 16px/700 section headings, 14px body.

## Color roles

| Token | Dark | Light | Job |
|---|---|---|---|
| `--bg` | `#150808` | `#f7eeee` | page ground (dominant) |
| `--surface` | `#200d0f` | `#ffffff` | sidebar, panels, the wizard |
| `--surface-2` | `#301318` | `#f0dcdc` | controls, selected nav, chips |
| `--text` / `--muted` | `#f8eded` / `#cba3a6` | `#2b0d0f` / `#6e4a4d` | body and secondary text (AA or better on every surface) |
| `--accent` | `#d4312f` | `#b3201f` | the primary action, the live wizard step, the switch when on; `--accent-ink` is its text |
| `--rose` | `#f08f8a` | `#a3322f` | secondary: links, the active nav icon |
| `--ok-ink` `--warn-ink` `--bad-ink` | green, gold, light coral | darker | state text and dots only |

State colors stay off the red scale on purpose, so "installed", "waiting" and "failed" are not confused with the theme.

## Shape and depth

- Radii by role: fields 7px, buttons 9px, panels 12px, the hero 16px, floating wizard 18px.
- Only things that float have a shadow: the wizard and toasts. Everything else sits flat on tone (`--surface` on `--bg`). The hero is the one panel with a hairline border.
- No colored side stripes, no glow shadows, no glass.

## Components

- **Status**: a dot and text (`.status`) for ambient state in the sidebar. A capsule (`.pill`) only for the state of one thing, such as a mod being installed, waiting for its download, or needing another mod.
- **Buttons**: one `primary` per view. Labels name the action ("Use these game files", "Install this file", "Send code"), not "Next" or "Submit".
- **Feedback**: it appears where the action happened. A button's result goes in `liveStatus()` right beside it (spinner while working, then the answer; good news clears itself, errors stay). Page-wide actions use `say()`, which writes to the line under the page title. Toasts are only the fallback inside the setup wizard.
- **Lists**: map packs and similar rows are separated by hairlines, not boxed in cards.
- **Copy**: sentence case, no arrow glyphs, no filler adjectives.
- **Motion**: 120-240ms ease-out, no overshoot, nothing under `prefers-reduced-motion: reduce`. Pages rise in once with a 30ms stagger, wizard steps slide in, toasts fade out, the waiting dot pulses. Only elements built once per visit animate: anything that re-renders while you watch it (the mod list, sidebar status) must not, or it replays on every refresh.
- **Mods**: one primary action per mod; the relation to another mod is a pill only for add-ons, and a warning line only when the base is missing; the two secondary pages are quiet text links under the card, not buttons.

## Rules for new pages

1. Use the tokens above; do not introduce hex values in `app.js`.
2. Build DOM with `h()`; no `innerHTML` and no inline `style` (the CSP forbids it).
3. Check changes in the browser preview (`scripts/ui-preview/`) in both color schemes.
4. If the icon palette changes, regenerate it with `scripts/make_icons.py` and update this file.
