# Rallypoint launcher: design contract

Every page and the setup wizard use these tokens from `style.css`. Add a token here before using a new value.

## Direction

A game launcher for a military RTS: dark by default, with a light theme when the system asks. The ground is a tonal scale of oxblood and the one action is a signal red, as the first brief asked ("a Generals theme"). The second brief asked for more colour and a better brand area, so the icon's gold is now the second brand colour and each section has a hue of its own. The signature detail is the rank chevron from the icon: the wizard's step marker, the mark beside every page title, and the stripes behind the brand block.

It uses the platform's own UI font on purpose (a desktop app should look native). Hierarchy comes from size and weight: 28px/750 page titles, 22px/750 on the one hero, 16px/700 section headings, 14px body.

## Color roles

| Token | Dark | Light | Job |
|---|---|---|---|
| `--bg` | `#150808` | `#f7eeee` | page ground (dominant) |
| `--surface` | `#200d0f` | `#ffffff` | sidebar, panels, the wizard |
| `--surface-2` | `#301318` | `#f0dcdc` | controls, selected nav, chips |
| `--text` / `--muted` | `#f8eded` / `#cba3a6` | `#2b0d0f` / `#6e4a4d` | body and secondary text (AA or better on every surface) |
| `--accent` | `#d4312f` | `#b3201f` | the primary action, the live wizard step, the switch when on; `--accent-ink` is its text |
| `--rose` | `#f08f8a` | `#a3322f` | secondary: links |
| `--gold` | `#e8b84a` | `#8a5a00` | the brand block (the icon's edge, the line under the name, its chevrons) and the sixth mod tile colour. Not for actions or state |
| `--c-play` `--c-mods` `--c-maps` `--c-online` `--c-settings` | coral, amber, green, blue, violet | darker | one hue per section: its nav icon chip (tinted, solid when current), the chevrons beside its page title, the soft light behind the title, the live tab. `main[data-page]` exposes the current one as `--page`. `--hue-ink` is text on a solid hue |
| `--ok-ink` `--warn-ink` `--bad-ink` | green, gold, light coral | darker | state text and dots only |

State colors stay off the red scale on purpose, so "installed", "waiting" and "failed" are not confused with the theme.

## Shape and depth

- Radii by role: fields 7px, buttons 9px, panels 12px, the hero 16px, floating wizard 18px.
- Panels have a hairline border and a faint top highlight, so they read on the ground without a shadow. Big shadows are for things that float: the wizard and toasts.
- A coloured shadow marks the thing that is live: the primary button, the current nav icon, an installed mod's tile. Nothing else glows.
- The ground is not flat: a soft red light in the window's top left corner (behind the brand) and the page's hue behind its title. Both are gradients on the ground, never on panels.
- No colored side stripes, no glass.

## Components

- **Status**: a dot and text (`.status`) for ambient state in the sidebar. A capsule (`.pill`) only for the state of one thing, such as a mod being installed, waiting for its download, or needing another mod.
- **Buttons**: one `primary` per view. Labels name the action ("Use these game files", "Install this file", "Send code"), not "Next" or "Submit".
- **Feedback**: it appears where the action happened. A button's result goes in `liveStatus()` right beside it (spinner while working, then the answer; good news clears itself, errors stay). Page-wide actions use `say()`, which writes to the line under the page title. Toasts are only the fallback inside the setup wizard.
- **Lists**: map packs and similar rows are separated by hairlines, not boxed in cards.
- **Filterable lists** (the map organiser): filters sit above the list (a `.searchbox` with its icon and clear button, a menu, a segmented control, then chips). The list is one bordered box: its `.listhead` has the result count and Clear filters on the left and a compact labelled sort on the right, its `.listfoot` the bulk actions, and the empty state (`.listempty`) sits inside it with a way out. A count inside a chip is a small badge, never bare digits next to the label.
- **Next step**: when an action leaves one obvious thing to do (connected, so start the game), a `.nextstep` strip says so and holds the view's primary button; the button that got you there steps down to a plain one.
- **Copy**: sentence case, no arrow glyphs, no filler adjectives.
- **Motion**: 120-240ms ease-out, no overshoot, nothing under `prefers-reduced-motion: reduce`. Pages rise in once with a 30ms stagger, wizard steps slide in, toasts fade out, the waiting dot pulses. Only elements built once per visit animate: anything that re-renders while you watch it (the mod list, sidebar status) must not, or it replays on every refresh.
- **Tabs** (`tabbed()`): a page longer than about two screens shows one section at a time (Settings: Game, Graphics, Multiplayer, Rallypoint). The live tab has a 2px accent rule; arrow keys move between tabs. The last tab is remembered while the launcher is open.
- **Setting rows** (`setting()` inside a `.settings` wrapper): the name and one sentence on the left, one control on the right, hairlines between. Every on/off or pick-one option uses this, on any page. A row whose effect depends on another one that is off is dimmed and says what it needs.
- **Choice tiles** (`.choice`, a radio group): for a choice between two or three things that each deserve a sentence, such as the renderer. Not for plain lists of values, which stay a `select` or a segmented control.
- **Grouped options**: more than about eight related options are grouped by what they change (Picture, Light and glow, ...), in two columns, with a preset as a segmented control above them.
- **Brand block** (`.brand`): the icon at 44px with a gold edge, the name, and "Generals Zero Hour" in gold under it; chevron stripes fade in from the right and a gold line closes it. Below 760px only the icon stays.
- **Mod tile** (`.modtile`): two letters where a picture would be, on one of six colours picked from the mod's name (`modHue`), so a mod keeps its colour: a tint before it is installed, solid after. The Mods page lists what is installed first, then what can be installed.
- **Mods**: the two steps are in order on the right (Download page, then the primary Choose file); choosing a folder is a quiet link; the relation to another mod is a pill only for add-ons, and a warning line only when the base is missing; the two secondary pages are quiet text links under the card, not buttons.

## Rules for new pages

1. Use the tokens above; do not introduce hex values in `app.js`.
2. Build DOM with `h()`; no `innerHTML` and no inline `style` (the CSP forbids it).
3. Check changes in the browser preview (`scripts/ui-preview/`) in both color schemes.
4. If the icon palette changes, regenerate it with `scripts/make_icons.py` and update this file.
