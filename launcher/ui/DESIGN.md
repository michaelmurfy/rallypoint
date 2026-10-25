# Rallypoint launcher: design contract

Every page and the setup wizard use these tokens from `style.css`. Add a token here before using a new value.

## Direction

A game launcher for a military RTS, so dark by default, with a light theme when the system asks. The ground and accent come from the app icon: deep teal for the ground, brass for the one action color. The signature detail is the rank chevron from the icon, used as the wizard's step marker.

It uses the platform's own UI font on purpose (a desktop app should look native). Hierarchy comes from size and weight: 28px/750 page titles, 22px/750 on the one hero, 16px/700 section headings, 14px body.

## Color roles

| Token | Dark | Light | Job |
|---|---|---|---|
| `--bg` | `#07191f` | `#edf3f4` | page ground (dominant) |
| `--surface` | `#0d2630` | `#ffffff` | sidebar, panels, the wizard |
| `--surface-2` | `#143542` | `#e1ecee` | controls, selected nav, chips |
| `--text` / `--muted` | `#eaf3f5` / `#94b6be` | `#0a2128` / `#46676f` | body and secondary text (AA on both grounds) |
| `--accent` | `#f2c94c` | `#c8921a` | the primary action only; `--accent-ink` is its text |
| `--teal` | `#38b8b0` | `#0f6f6a` | secondary: links, switches, the chevrons that are done, info banners |
| `--ok-ink` `--warn-ink` `--bad-ink` | green, gold, coral | darker | state text and dots (never used as fills for body text) |

## Shape and depth

- Radii by role: fields 7px, buttons 9px, panels 12px, the hero 16px, floating wizard 18px.
- Only things that float have a shadow: the wizard and toasts. Everything else sits flat on tone (`--surface` on `--bg`). The hero is the one panel with a hairline border.
- No colored side stripes, no glow shadows, no glass.

## Components

- **Status**: a dot and text (`.status`) for ambient state in the sidebar. A capsule (`.pill`) only for the state of one thing, such as a mod being installed, waiting for its download, or needing another mod.
- **Buttons**: one `primary` per view. Labels name the action ("Use these game files", "Install this file", "Send code"), not "Next" or "Submit".
- **Copy**: sentence case, no arrow glyphs, no filler adjectives.
- **Motion**: 120-180ms ease-out, no overshoot, nothing under `prefers-reduced-motion: reduce`.

## Rules for new pages

1. Use the tokens above; do not introduce hex values in `app.js`.
2. Build DOM with `h()`; no `innerHTML` and no inline `style` (the CSP forbids it).
3. Check changes in the browser preview (`scripts/ui-preview/`) in both color schemes.
