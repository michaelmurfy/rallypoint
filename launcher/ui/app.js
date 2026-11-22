"use strict";
// Rallypoint launcher UI. Everything is built from DOM nodes (never markup strings), because folder names and
// mod names come from outside and the page has a strict content security policy.
const tauri = window.__TAURI__;
const invoke = (cmd, args) => tauri.core.invoke(cmd, args);

// ---------------------------------------------------------------- helpers
function h(tag, props, ...kids) {
  const el = document.createElement(tag);
  // Server names, rooms, paths and codes: the webview must not autocorrect, capitalise, spell-check or autofill them.
  if (tag === "input" || tag === "textarea") for (const [k, v] of [["autocomplete", "off"], ["autocorrect", "off"], ["autocapitalize", "off"], ["spellcheck", "false"]]) el.setAttribute(k, v);
  for (const [k, v] of Object.entries(props || {})) {
    if (v == null || v === false) continue;
    if (k === "class") el.className = v;
    else if (k.startsWith("on")) el.addEventListener(k.slice(2).toLowerCase(), v);
    else if (["value", "checked", "disabled", "hidden", "open"].includes(k)) el[k] = v;
    else el.setAttribute(k, v === true ? "" : v);
  }
  for (const kid of kids.flat(Infinity)) if (kid != null && kid !== false) el.append(kid.nodeType ? kid : document.createTextNode(String(kid)));
  return el;
}

const ICONS = {
  play: "M8 5v14l11-7z",
  cash: "M11.8 10.9c-2.27-.59-3-1.2-3-2.15 0-1.09 1.01-1.85 2.7-1.85 1.78 0 2.44.85 2.5 2.1h2.21c-.07-1.72-1.12-3.3-3.21-3.81V3h-3v2.16c-1.94.42-3.5 1.68-3.5 3.61 0 2.31 1.91 3.46 4.7 4.13 2.5.6 3 1.48 3 2.41 0 .69-.49 1.79-2.7 1.79-2.06 0-2.87-.92-2.98-2.1h-2.2c.12 2.19 1.76 3.42 3.68 3.83V21h3v-2.15c1.95-.37 3.5-1.5 3.5-3.55 0-2.84-2.43-3.81-4.7-4.4z",
  screen: "M21 2H3c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h7v2H8v2h8v-2h-2v-2h7c1.1 0 2-.9 2-2V4c0-1.1-.9-2-2-2zm0 14H3V4h18v12z",
  speed: "M12 2a10 10 0 100 20 10 10 0 000-20zm0 18a8 8 0 110-16 8 8 0 010 16zm.5-13H11v6l5.2 3.2.8-1.3-4.5-2.7V7z",
  mods: "M12 2 2 7l10 5 10-5-10-5zm0 11.5L4.6 9.8 2 11l10 5 10-5-2.6-1.2L12 13.5zm0 4-7.4-3.7L2 15l10 5 10-5-2.6-1.2L12 17.5z",
  online: "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zm6.9 6h-3a15.7 15.7 0 0 0-1.4-3.6A8 8 0 0 1 18.9 8zM12 4c.8 1.2 1.5 2.5 1.9 4h-3.8c.4-1.5 1.1-2.8 1.9-4zM4.3 14a8.200 8.200 0 0 1 0-4h3.400a16.500 16.500 0 0 0 0 4H4.300zm.8 2h3a15.700 15.700 0 0 0 1.400 3.600A8 8 0 0 1 5.100 16zm3-8h-3a8 8 0 0 1 4.400-3.600A15.700 15.700 0 0 0 8.100 8zM12 20c-.8-1.200-1.500-2.500-1.900-4h3.800c-.4 1.500-1.100 2.800-1.900 4zm2.300-6H9.700a14.700 14.700 0 0 1 0-4h4.600a14.700 14.700 0 0 1 0 4zm.2 5.600c.6-1.100 1.100-2.300 1.400-3.600h3a8 8 0 0 1-4.400 3.600zm1.800-5.600a16.500 16.500 0 0 0 0-4h3.400a8.200 8.200 0 0 1 0 4h-3.400z",
  maps: "M20.5 3l-.2.1L15 5.2 9 3 3.400 4.900a.5.5 0 0 0-.4.5v15.100a.5.5 0 0 0 .5.5l.2-.1L9 18.800l6 2.100 5.600-1.900a.5.5 0 0 0 .4-.5V3.500a.5.5 0 0 0-.5-.5zM15 19l-6-2.100V5l6 2.100V19z",
  settings: "M19.400 13a7.500 7.500 0 0 0 0-2l2.100-1.600-2-3.400-2.500 1a7.600 7.600 0 0 0-1.700-1L15 3.300h-4L10.700 6a7.600 7.600 0 0 0-1.700 1l-2.500-1-2 3.400L6.600 11a7.500 7.500 0 0 0 0 2l-2.100 1.600 2 3.400 2.500-1c.5.400 1.100.7 1.700 1l.3 2.700h4l.3-2.700c.6-.3 1.200-.6 1.700-1l2.500 1 2-3.400L19.400 13zM13 15.500a3.500 3.500 0 1 1 0-7 3.500 3.500 0 0 1 0 7z",
  ai: "M15 9H9v6h6V9zm-2 4h-2v-2h2v2zm8-2V9h-2V7c0-1.1-.9-2-2-2h-2V3h-2v2h-2V3H9v2H7c-1.1 0-2 .9-2 2v2H3v2h2v2H3v2h2v2c0 1.1.9 2 2 2h2v2h2v-2h2v2h2v-2h2c1.1 0 2-.9 2-2v-2h2v-2h-2v-2h2zm-4 6H7V7h10v10z",
  check: "M9 16.200 4.800 12l-1.400 1.400L9 19 21 7l-1.400-1.400z",
  plus: "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6z",
  prev: "M15.400 7.400 14 6l-6 6 6 6 1.400-1.400L10.800 12z",
  folder: "M10 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z",
  next: "M8.600 16.600 10 18l6-6-6-6-1.400 1.400L13.200 12z",
};
function icon(name) {
  const ns = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(ns, "svg");
  svg.setAttribute("viewBox", "0 0 24 24");
  svg.setAttribute("aria-hidden", "true");
  const p = document.createElementNS(ns, "path");
  p.setAttribute("d", ICONS[name]);
  p.setAttribute("fill", "currentColor");
  svg.append(p);
  return svg;
}

function toast(text, kind) {
  const t = h("div", { class: "toast " + (kind || "") }, text);
  document.getElementById("toasts").append(t);
  setTimeout(() => { t.classList.add("leaving"); setTimeout(() => t.remove(), 180); }, kind === "bad" ? 9000 : 4500);
}

/**
 * One line of feedback that sits beside what caused it: a spinner while working, then the result.
 * kind: "busy" | "ok" | "bad" | none. Good news clears itself; errors stay until the next action.
 */
function liveStatus() {
  const el = h("span", { class: "live", role: "status" });
  let timer = null;
  const api = {
    el,
    set(text, kind) {
      clearTimeout(timer);
      el.className = "live " + (kind || "");
      fill(el, kind === "busy" ? h("span", { class: "spin", "aria-hidden": "true" }) : null, text ? h("span", {}, text) : null);
      if (kind === "ok") timer = setTimeout(() => api.set(""), 8000);
      return api;
    },
    busy: (t) => api.set(t, "busy"),
    ok: (t) => api.set(t, "ok"),
    bad: (t) => api.set(t, "bad"),
  };
  return api;
}

/** A centred notice with a spinner for something that takes a moment and has no progress to show (starting the game). */
function busyOverlay(text) {
  const label = h("span", {}, text);
  const el = h("div", { class: "busy-overlay", role: "status", "aria-live": "polite" }, h("div", { class: "busy-card" }, h("span", { class: "spin big", "aria-hidden": "true" }), label));
  document.getElementById("root").append(el);
  return {
    set: (t) => { label.textContent = t; },
    close: () => { el.classList.add("leaving"); setTimeout(() => el.remove(), 200); },
  };
}

/** Page-wide feedback goes in the line under the page title; the setup wizard (no page line) falls back to a toast. */
let pageStatus = null;
function say(text, kind) {
  if (pageStatus && pageStatus.el.isConnected && !wizardEl) pageStatus.set(text, kind);
  else toast(text, kind);
}

/** replaceChildren that skips null/false (replaceChildren would print "null"). */
function fill(el, ...kids) {
  el.replaceChildren(...kids.flat(Infinity).filter((k) => k != null && k !== false));
}

/** A page's title row: title, an optional line under it, and the page's actions on the right. */
function pageHead(title, sub, ...actions) {
  return h("div", { class: "page-head" }, h("div", { class: "head-text" }, h("h1", {}, title), sub ? h("p", { class: "head-sub" }, sub) : null), actions.length ? h("div", { class: "row" }, actions) : null);
}
/** Nothing here yet: an icon, what is missing, what to do about it. */
function emptyState(ic, title, text, ...actions) {
  return h("div", { class: "empty" }, icon(ic), h("strong", {}, title), text ? h("p", {}, text) : null, actions.length ? h("div", { class: "row" }, actions) : null);
}
/** Something went wrong, and the button that tries again or leads on. */
const callout = (kind, text, ...actions) => h("div", { class: "callout " + kind, role: kind === "bad" ? "alert" : "status" }, h("span", {}, text), actions);
const skeletonLines = (n) => Array.from({ length: n }, () => h("span", { class: "skeleton skelrow", "aria-hidden": "true" }));

const errText = (e) => (typeof e === "string" ? e : (e && e.message) || String(e));

// ---------------------------------------------------------------- state
const store = { cfg: null, games: [], mods: { catalog: [], other: [] }, relay: "Offline", page: "play", watch: null, update: null, updateNote: "", maps: null };
const nativeRes = () => ({ w: Math.round(screen.width * devicePixelRatio), h: Math.round(screen.height * devicePixelRatio) });

const ICON_FOLDER = "Control Bar HD";
const PRO_FOLDER = "Control Bar Pro";
const isBundled = (folder) => folder === ICON_FOLDER || folder === PRO_FOLDER;
const prefs = {
  get() {
    const blank = { res: "native", fs: false, mods: [], camera: false };
    try {
      const raw = JSON.parse(localStorage.getItem("rallypoint.prefs") || "{}");
      const p = { ...blank, ...raw };
      if (!Array.isArray(raw.mods)) p.mods = raw.mod ? [raw.mod] : [];
      return p;
    } catch { return blank; }
  },
  set(patch) {
    try { localStorage.setItem("rallypoint.prefs", JSON.stringify({ ...prefs.get(), ...patch })); } catch { /* per-viewer convenience only */ }
  },
};

const refreshers = new Set(); // views that show mods re-render through these
const notifyMods = () => refreshers.forEach((f) => f());
async function loadMods() {
  try { store.mods = await invoke("mods_list"); } catch (e) { say(errText(e), "bad"); }
  notifyMods();
}

const needsOf = (m) => (m.addon_for ? (store.mods.catalog.find((b) => b.id === m.addon_for) || {}).name || null : null);

/** The games you can start: Zero Hour itself, and each listed mod (Shockwave, Chaos, Cold War Crisis...) that is installed. */
const GAME_ZH = "zh";
function gameChoices() {
  return [
    { id: GAME_ZH, name: "Zero Hour", folder: null, installed: true, note: "The original game" },
    ...store.mods.catalog.map((m) => ({
      id: m.id, name: m.name, folder: m.path, installed: !!m.installed,
      note: m.conflicts && m.conflicts.length ? "Total conversion, plays on its own" : m.addon_for ? `Plays on top of ${needsOf(m)}` : "Overhaul mod",
    })),
  ];
}
/** The chosen game; falls back to Zero Hour when the saved choice is no longer installed. A pre-dropdown saved selection is carried over. */
function selectedGame() {
  const p = prefs.get(), all = gameChoices();
  const byId = all.find((g) => g.id === p.game && g.installed);
  if (byId) return byId;
  const legacy = all.find((g) => g.folder && g.installed && (p.mods || []).includes(g.folder));
  return legacy || all[0];
}
/** Mods installed from a file or folder (not listed in the catalog) are the extras, ticked on top of the chosen game. */
const extraMods = () => store.mods.other.filter((o) => !isBundled(o.folder)).map((o) => ({ folder: o.folder, name: o.folder, mb: o.mb }));
function selectedExtras() {
  const have = new Set(extraMods().map((m) => m.folder));
  return (prefs.get().mods || []).filter((f) => have.has(f));
}
function toggleExtra(folder, on) {
  const cur = selectedExtras().filter((f) => f !== folder);
  prefs.set({ mods: on ? [...cur, folder] : cur });
  notifyMods();
}
/** What the engine loads, bottom layer first: the bundled icons, the chosen game, then the ticked extras. */
function launchFolders(g = selectedGame(), extras = selectedExtras()) {
  const has = (f) => store.mods.other.some((o) => o.folder === f);
  return [...(has(ICON_FOLDER) ? [ICON_FOLDER] : []), ...(store.cfg && store.cfg.control_bar === "pro" && has(PRO_FOLDER) ? [PRO_FOLDER] : []), ...(g.folder ? [g.folder] : []), ...extras];
}
/** The names of the games an official map comes with (its tags). */
const gameNames = (m) => (m.games || []).map((id) => (gameChoices().find((g) => g.id === id) || { name: id }).name.replace(/^C&C: /, ""));
/** The mod folders to read an official map's archives with: those of the game it comes with (the selected one when it has the map). */
function officialFolders(m) {
  const id = m.games.includes(selectedGame().id) ? selectedGame().id : m.games[0];
  const g = gameChoices().find((c) => c.id === id);
  return g && g.folder ? [g.folder] : [];
}
let iconsJob = null;
/** Sharp command-bar icons. Setup downloads them on its own; they load with every game. */
function ensureIcons() {
  if (store.mods.other.some((o) => o.folder === ICON_FOLDER)) return Promise.resolve();
  if (!iconsJob) {
    iconsJob = (async () => {
      await invoke("icons_install");
      await loadMods();
    })().catch((e) => { iconsJob = null; throw e; });
  }
  return iconsJob;
}

// ---------------------------------------------------------------- updates
const mb = (bytes) => (bytes / 1048576).toFixed(1);
/** Download, verify and install the update with the whole window given over to it: nothing else is useful meanwhile.
 *  Returns the message from the backend when it could not restart by itself, or throws. */
async function runUpdate() {
  const title = h("h2", {}, `Updating Rallypoint${store.update ? " to " + store.update.latest : ""}`);
  const status = h("p", { class: "muted", role: "status", "aria-live": "polite" }, "Starting…");
  const bar = h("progress", { max: 100, value: 0 });
  const detail = h("p", { class: "muted small" });
  const el = h("div", { class: "busy-overlay updating", role: "dialog", "aria-modal": "true", "aria-label": "Updating Rallypoint" },
    h("div", { class: "update-card" }, h("span", { class: "spin big", "aria-hidden": "true" }), title, status, bar, detail, h("p", { class: "muted small" }, "Rallypoint will restart by itself.")));
  document.getElementById("root").append(el);
  const tick = async () => {
    const p = await invoke("update_progress").catch(() => null);
    if (!p) return;
    if (p.phase === "Downloading") {
      status.textContent = "Downloading the update…";
      if (p.total) { bar.value = Math.min(100, (p.done / p.total) * 100); detail.textContent = `${mb(p.done)} of ${mb(p.total)} MB`; } else { bar.removeAttribute("value"); detail.textContent = ""; }
    } else if (p.phase === "Installing") {
      status.textContent = "Installing the update…"; bar.removeAttribute("value"); detail.textContent = "";
    } else if (p.phase === "Restarting") {
      status.textContent = "Restarting…"; bar.removeAttribute("value"); detail.textContent = "";
    }
  };
  const timer = setInterval(tick, 400);
  try { return await invoke("update_install"); }
  finally { clearInterval(timer); el.remove(); }
}

/** "0.7.0-beta.2 (beta)" for a beta, the plain version otherwise. */
const updateName = (u) => `Rallypoint ${u.latest}${u.beta ? " (beta)" : ""}`;
/** What the offered release changes: its notes in a dialog, or its page when it has none. */
function showUpdateNotes(u, bad) {
  if (u.notes && u.notes.trim()) showChangelog(u.notes, `What's new in ${u.latest}`);
  else invoke("open_release_page").catch((e) => bad(errText(e)));
}

/** Linux has no in-app installer (the packages are .deb, .rpm and .tar.gz): the release page is how it updates. */
const pageButton = (u, bad) => !u.installer && store.cfg.os === "linux"
  ? h("button", { class: "btn primary", onclick: () => invoke("open_release_page").catch((e) => bad(errText(e))) }, "Download page")
  : null;

function renderUpdate() {
  const host = document.getElementById("updatehost");
  if (!host) return;
  const u = store.update;
  if (!u || u.skipped || store.updateDismissed) return fill(host);
  const install = h("button", { class: "btn primary", onclick: async () => {
    install.disabled = true;
    try { store.updateNote = await runUpdate(); } catch (e) { store.updateNote = errText(e); }
    install.disabled = false;
    renderUpdate();
  } }, "Update and restart");
  fill(host, h("div", { class: "banner stack updatebar" },
    h("div", {}, h("b", {}, `${updateName(u)} is available`),
      h("p", { class: "muted small" }, store.updateNote || `You have ${u.current}.${u.size_mb ? ` The download is about ${u.size_mb} MB.` : ""}`)),
    h("div", { class: "row" },
      u.installer ? install : null,
      pageButton(u, (m) => say(m, "bad")),
      h("button", { class: u.installer || pageButton(u, () => {}) ? "btn" : "btn primary", onclick: () => showUpdateNotes(u, (m) => say(m, "bad")) }, "What's new"),
      h("button", { class: "btn ghost", onclick: () => { store.updateDismissed = true; renderUpdate(); } }, "Later"),
      h("button", { class: "btn ghost", onclick: async () => {
        try { await invoke("update_skip"); u.skipped = true; renderUpdate(); } catch (e) { say(errText(e), "bad"); }
      } }, "Skip this version"))));
}
/** Ask GitHub for a newer release. Returns an error message, or null. The startup check stays quiet unless there is an update.
 *  `quiet` (the six-hourly look) leaves a banner the player already put away alone unless the release changed. */
async function checkUpdate(quiet) {
  const before = store.update && store.update.latest;
  try { store.update = await invoke("update_check"); } catch (e) { return "Couldn't check for updates: " + errText(e); }
  if (quiet && store.update && store.update.latest === before) return null;
  store.updateDismissed = false;
  store.updateNote = "";
  renderUpdate();
  return null;
}

/** One line of a settings card: what it is and what it does on the left, its control on the right. */
function setting(title, desc, ...controls) {
  return h("div", { class: "setting" },
    h("div", { class: "setting-text" }, h("div", { class: "setting-title" }, title), desc ? h("div", { class: "muted small" }, desc) : null),
    h("div", { class: "setting-ctl" }, ...controls));
}

/** Settings card: Check now with the answer beside it, and the install actions once there is something to install. */
function updatesCard() {
  const fb = liveStatus();
  const actions = h("div", { class: "row end" });
  const auto = h("input", { type: "checkbox", "aria-label": "Check for updates when Rallypoint starts", checked: store.cfg.check_updates !== false, onchange: () => { store.cfg.check_updates = auto.checked; invoke("set_check_updates", { on: auto.checked }); } });
  const install = h("button", { class: "btn primary", onclick: async () => {
    install.disabled = true;
    try { fb.ok(await runUpdate()); } catch (e) { fb.bad(errText(e)); }
    install.disabled = false;
  } }, "Update and restart");
  const notes = h("button", { class: "btn", onclick: () => store.update && showUpdateNotes(store.update, fb.bad) }, "What's new");
  const changelog = h("button", { class: "btn", onclick: () => invoke("changelog").then((t) => showChangelog(t)).catch((e) => fb.bad(errText(e))) }, "Changelog");
  const runCheck = async () => {
    check.disabled = true;
    fill(actions);
    fb.busy("Checking…");
    const err = await checkUpdate();
    check.disabled = false;
    if (err) fb.bad(err); else report();
  };
  const check = h("button", { class: "btn", onclick: runCheck }, "Check now");
  const betaNote = h("div", { class: "muted small" });
  const drawBetaNote = () => {
    betaNote.textContent = store.cfg.beta_updates
      ? "You get new features early, and the rough edges with them."
      : store.cfg.version_is_beta
        ? "You are on a beta now. With this off you stay on it until the next full release."
        : "Off: only full releases. On: new features early, less tested.";
  };
  const beta = h("input", { type: "checkbox", "aria-label": "Get beta releases", checked: !!store.cfg.beta_updates, onchange: async () => {
    try {
      await invoke("set_beta_updates", { on: beta.checked });
      store.cfg.beta_updates = beta.checked;
      // The backend forgot a found beta when opting out; do the same here so no install button is left for it.
      if (!beta.checked && store.update && store.update.beta) { store.update = null; renderUpdate(); }
      drawBetaNote();
      await runCheck();
    } catch (e) { beta.checked = !!store.cfg.beta_updates; fb.bad(errText(e)); }
  } });
  function report() {
    const u = store.update;
    fb.set(u ? `${updateName(u)} is available${u.size_mb ? ` (${u.size_mb} MB)` : ""}${u.installer ? "" : store.cfg.os === "linux" ? ". Install the new .deb, .rpm or .tar.gz from the download page" : ". Its installer for this computer is not up yet"}` : "You have the latest version", "ok");
    fill(actions, u && u.installer ? install : null, u ? pageButton(u, fb.bad) : null, u ? notes : null);
  }
  if (store.update) report();
  drawBetaNote();
  return h("div", { class: "card" }, h("h2", {}, "Updates"),
    setting(`Rallypoint ${store.cfg.version}`, fb.el, changelog, check),
    actions,
    setting("Check when Rallypoint starts", "And every six hours while it is open.", h("label", { class: "switch" }, auto)),
    setting("Get beta releases", betaNote, h("label", { class: "switch" }, beta)));
}

// ---------------------------------------------------------------- mod downloads
// "Get" opens the mod's official page. Nothing installs by itself: the user chooses the finished download
// (or confirms the file we spotted in their Downloads folder).
let watchTimer = null;
async function startWatch(m) {
  try {
    await invoke("mods_get", { id: m.id });
  } catch (e) { return say(errText(e), "bad"); }
  store.watch = { id: m.id, name: m.name, found: null };
  notifyMods();
  clearInterval(watchTimer);
  watchTimer = setInterval(async () => {
    if (!store.watch) return clearInterval(watchTimer);
    let s;
    try { s = await invoke("mods_poll"); } catch { return; }
    const found = s.state === "found" ? { path: s.path, name: s.message } : null;
    if (JSON.stringify(found) !== JSON.stringify(store.watch.found)) { store.watch.found = found; notifyMods(); }
  }, 1500);
}
async function cancelWatch() {
  clearInterval(watchTimer);
  store.watch = null;
  await invoke("mods_cancel_watch");
  notifyMods();
}
/** Install `path`; with `id` it becomes that catalog mod. Errors are shown and the user can try another file. */
async function installPath(path, id) {
  // What was picked is what gets installed: the card swaps to a progress state right away, with nothing left to choose.
  const mod = id && store.mods.catalog.find((m) => m.id === id);
  store.installing = { id: id || "*", name: mod ? mod.name : "mod" };
  if (id) { clearInterval(watchTimer); store.watch = null; }
  notifyMods();
  try {
    const r = await invoke("mods_install", { path, id: id || null });
    say(`${r.folder} installed (${r.bigs} game file${r.bigs === 1 ? "" : "s"})`, "ok");
  } catch (e) { say(errText(e), "bad"); }
  store.installing = null;
  await loadMods();
}
/** Native file or folder picker, opened in the Downloads folder. */
async function choose(id, directory) {
  let picked;
  try {
    const dl = await invoke("downloads_dir").catch(() => null);
    picked = await tauri.dialog.open({
      multiple: false, directory: !!directory, defaultPath: dl || undefined,
      title: directory ? "Choose the mod folder" : "Choose the downloaded mod",
      filters: directory ? undefined : [{ name: "Mods and installers", extensions: ["zip", "7z", "rar", "exe", "gz", "tgz", "xz", "bz2", "tar"] }],
    });
  } catch (e) { return say("Couldn't open the file picker: " + errText(e), "bad"); }
  if (picked) await installPath(typeof picked === "string" ? picked : picked.path, id);
}

// ---------------------------------------------------------------- maps
async function loadMaps() {
  try { store.maps = await invoke("maps_info"); } catch (e) { say(errText(e), "bad"); }
  notifyMods();
}
/** Pick an archive or folder of maps, in the Downloads folder, and add it. */
// Set by the maps card while it is on screen, so adding a file or folder reports in the card (where you clicked), not in a toast.
let mapsNotice = null;
const mapsSay = (text, kind) => (mapsNotice ? mapsNotice(text, kind) : say(text, kind));
async function pickMaps(directory, packId) {
  let picked;
  try {
    const dl = await invoke("downloads_dir").catch(() => null);
    picked = await tauri.dialog.open({
      multiple: false, directory: !!directory, defaultPath: dl || undefined,
      title: directory ? "Choose a folder of maps" : "Choose a maps archive",
      filters: directory ? undefined : [{ name: "Map archives", extensions: ["zip", "7z", "rar", "gz", "tgz"] }],
    });
  } catch (e) { return mapsSay("Couldn't open the file picker: " + errText(e), "bad"); }
  if (!picked) return;
  mapsSay("Adding maps… this can take a minute for a big pack", "busy");
  try {
    const n = await invoke("maps_install_path", { path: typeof picked === "string" ? picked : picked.path, pack: packId || null });
    const pack = packId && (store.maps.packs.find((p) => p.id === packId) || {});
    mapsSay(pack && pack.start_disabled ? `${n} maps added to your library. Choose the ones you want on the Maps page.` : `${n} map${n === 1 ? "" : "s"} added`, "ok");
  } catch (e) { mapsSay(errText(e), "bad"); }
  loadMaps();
  notifyMaps();
}
const notifyMaps = () => refreshers.forEach((f) => f.name === "reloadOrganiser" && f());
/** Map packs to install with one button, plus adding your own. Used on the Mods page and in the wizard. */
function mapsCard(installed) { // installed(pack name) -> how many of its maps are in the library, when the page knows
  let timer = null, active = null, last = null; // active: the pack being installed (null for a file or folder you chose)
  const info = h("p", { class: "muted small" });
  const list = h("div", { class: "stack" });
  // One progress block. It sits inside the row of the pack being installed, or above the "Add maps from…" buttons for a
  // file or folder, so it shows where you just clicked instead of at the end of a long list.
  const bar = h("progress", { max: 100, value: 0 });
  const text = h("span", { class: "jobtext", role: "status" });
  const cancel = h("button", { class: "btn small", onclick: () => invoke("maps_cancel") }, "Cancel");
  const job = h("div", { class: "packjob", hidden: true }, h("div", { class: "row between" }, text, cancel), bar);
  const strip = h("div", {});
  // a file or folder being added: no percentage to show, so the bar just moves
  mapsNotice = (message, kind) => {
    last = { phase: kind === "busy" ? "Installing" : kind === "ok" ? "Done" : "Failed", percent: 0, message, local: true };
    paintJob();
  };
  function paintJob() {
    const s = last;
    const busy = !!s && (s.phase === "Downloading" || s.phase === "Installing");
    job.hidden = !s || s.phase === "Idle";
    bar.hidden = !busy; cancel.hidden = !busy || !!(s && s.local);
    if (s && s.percent > 0) bar.value = s.percent; else bar.removeAttribute("value");
    text.className = "jobtext " + (s && s.phase === "Failed" ? "bad" : s && s.phase === "Done" ? "ok" : "");
    text.textContent = s && s.phase !== "Idle" ? (busy ? `${s.message}${s.percent ? ` ${Math.round(s.percent)}%` : ""}` : s.message) : "";
    list.querySelectorAll("button").forEach((b) => { if (!job.contains(b)) b.disabled = busy; });
  }
  const have = (p) => (installed ? installed(p.name) : 0);
  function render() {
    const m = store.maps;
    if (!m) return;
    info.textContent = m.count ? `${m.count} map${m.count === 1 ? "" : "s"} installed in ${m.dir}` : "No extra maps installed yet.";
    fill(list, m.packs.map((p) => {
      const row = h("div", { class: "mappack" },
        h("div", {}, h("h3", {}, p.name, have(p) ? h("span", { class: "pill ok packhave" }, `${have(p)} in your maps`) : null), h("p", { class: "muted small" }, `${p.description} About ${p.size_mb} MB.`)),
        p.manual
          ? h("div", { class: "row" },
            h("button", { class: "btn small", onclick: () => invoke("maps_open_pack_page", { id: p.id }).catch((e) => say(errText(e), "bad")) }, "Open download page"),
            h("button", { class: "btn small", onclick: () => { active = p.id; render(); pickMaps(false, p.id); } }, "Choose the file…"))
          : h("button", { class: "btn small", onclick: () => start(p) }, have(p) ? "Install again" : "Install"));
      if (active === p.id) row.append(job);
      return row;
    }));
    if (!job.parentElement || !list.contains(job)) strip.append(job);
    paintJob();
  }
  async function start(p) {
    try { active = p.id; render(); await invoke("maps_install_pack", { id: p.id }); poll(); } catch (e) { active = null; render(); say(errText(e), "bad"); }
  }
  function poll() {
    clearInterval(timer);
    timer = setInterval(async () => {
      const s = await invoke("maps_status").catch(() => null);
      if (!s) return;
      last = s;
      paintJob();
      if (s.phase === "Done" || s.phase === "Failed") { clearInterval(timer); loadMaps(); }
    }, 600);
  }
  refreshers.add(render);
  render();
  const el = h("div", { class: "stack" }, info, list,
    h("div", { class: "mappack ownmaps" },
      h("div", {}, h("h3", {}, "Your own maps"), h("p", { class: "muted small" }, "Maps you downloaded yourself: an archive (zip, 7z, rar) or a folder that holds map folders.")),
      h("div", { class: "row" },
        h("button", { class: "btn small", onclick: () => { active = null; render(); pickMaps(false); } }, "Choose a file…"),
        h("button", { class: "btn small", onclick: () => { active = null; render(); pickMaps(true); } }, "Choose a folder…")),
      strip));
  return { el, render, dispose: () => { clearInterval(timer); refreshers.delete(render); mapsNotice = null; } };
}

/** The map picture's side in the preview window, in pixels (also in style.css: .mapview-canvas). */
const MAP_VIEW = 400;
/** One colour per place in a skirmish: on its row's swatch (style.css .pc0 to .pc7) and on its start marker. */
const PLACE_COLORS = ["#4d8dff", "#e5484d", "#46a758", "#f5c542", "#f08c3c", "#9a6bdc", "#3dc7c0", "#e5509b"];

const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

/** Draw a map preview (a map_view result) on its square canvas with the start markers. `owner(n)` says who has start n:
 *  null, or { color, halo }. Returns where the markers are, for clicks (`spotUnder`). Shared by Generals and RA2. */
function paintMap(canvas, v, owner) {
  const info = v.info;
  // The canvas is always the same square, so nothing moves between maps: the picture is drawn at the map's own
  // proportions, centred in it.
  const ratio = info ? info.width / info.height : 1;
  const cw = ratio >= 1 ? MAP_VIEW : Math.round(MAP_VIEW * ratio), ch = ratio >= 1 ? Math.round(MAP_VIEW / ratio) : MAP_VIEW;
  const ox = Math.round((MAP_VIEW - cw) / 2), oy = Math.round((MAP_VIEW - ch) / 2);
  canvas.width = MAP_VIEW; canvas.height = MAP_VIEW;
  const ctx = canvas.getContext("2d");
  ctx.fillStyle = css("--bg"); ctx.fillRect(0, 0, MAP_VIEW, MAP_VIEW);
  if (v.rgba) {
    if (!v.bitmap) {
      const bin = atob(v.rgba), px = new Uint8ClampedArray(bin.length);
      for (let k = 0; k < bin.length; k++) px[k] = bin.charCodeAt(k);
      v.bitmap = document.createElement("canvas");
      v.bitmap.width = v.image_w; v.bitmap.height = v.image_h;
      v.bitmap.getContext("2d").putImageData(new ImageData(px, v.image_w, v.image_h), 0, 0);
    }
    ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = "high";
    ctx.drawImage(v.bitmap, ox, oy, cw, ch);
  }
  const spots = [];
  if (info) {
    ctx.font = "700 13px system-ui, sans-serif"; ctx.textAlign = "center"; ctx.textBaseline = "middle";
    for (const st of info.starts) {
      const x = ox + st.x * cw, y = oy + (1 - st.y) * ch;
      const o = owner(st.number);
      spots.push({ x, y, n: st.number });
      // a taken start wears its owner's colour; the selected one's start also gets a halo
      if (o && o.halo) { ctx.beginPath(); ctx.arc(x, y, 21, 0, Math.PI * 2); ctx.fillStyle = o.color; ctx.globalAlpha = .3; ctx.fill(); ctx.globalAlpha = 1; }
      ctx.beginPath(); ctx.arc(x, y, o ? 14 : 11, 0, Math.PI * 2);
      ctx.fillStyle = o ? o.color : "rgba(20, 8, 9, .72)"; ctx.fill();
      ctx.lineWidth = o ? 2.5 : 1.5; ctx.strokeStyle = o ? "#fff" : "rgba(255, 255, 255, .7)"; ctx.stroke();
      ctx.fillStyle = "#fff"; ctx.fillText(String(st.number), x, y + 1);
    }
  }
  return spots;
}
/** The start marker under the pointer, if any. */
function spotUnder(canvas, spots, e) {
  const r = canvas.getBoundingClientRect(), k = MAP_VIEW / r.width;
  const x = (e.clientX - r.left) * k, y = (e.clientY - r.top) * k;
  return spots.find((s) => Math.hypot(s.x - x, s.y - y) <= 18);
}

/** A map as the game shows it: the preview picture with the start positions on it, beside the skirmish table. */
function openMapViewer(entries, start, onChange) {
  let i = start, token = 0, active = 0, shown = null; // active: the row the next click on the map assigns a start position to
  let generalsList = [];                              // what the chosen game and mod offer: [{ id, name }]
  let rows = [];                                      // the skirmish table: one per place on this map
  const canvas = h("canvas", { class: "mapview-canvas", width: MAP_VIEW, height: MAP_VIEW, role: "img", "aria-label": "Map preview: click a start position to give it to the selected place" });
  const title = h("h2", {});
  const factline = h("p", { class: "factline" });
  const position = h("span", { class: "muted small mappos" });
  const note = h("p", { class: "muted small mapnote", role: "status" });
  const prev = h("button", { class: "btn iconbtn", "aria-label": "Previous map", onclick: () => go(-1) }, icon("prev"));
  const next = h("button", { class: "btn iconbtn", "aria-label": "Next map", onclick: () => go(1) }, icon("next"));
  const listed = h("input", { type: "checkbox", role: "switch", onchange: async () => {
    const m = entries[i];
    listed.disabled = true;
    try { await invoke("maps_set_enabled", { names: [m.name], enabled: listed.checked }); m.enabled = listed.checked; if (onChange) onChange(); }
    catch (e) { listed.checked = m.enabled; note.textContent = errText(e); }
    listed.disabled = false;
  } });
  const listedRow = h("label", { class: "switch" }, listed, "Show in the game's map list");
  const close = h("button", { class: "btn", onclick: () => finish() }, "Close");

  // The game (Zero Hour or a mod) the skirmish is played with. Changing it changes the generals on offer.
  const gameSel = h("select", { "aria-label": "Game", onchange: async () => {
    prefs.set({ game: gameSel.value }); notifyMods();
    await loadGenerals(); drawTable(); paint(shown);
  } });
  function drawGames() {
    const cur = selectedGame();
    gameSel.replaceChildren(...gameChoices().map((g) => { const o = new Option(g.installed ? g.name : `${g.name} (not installed)`, g.id); o.disabled = !g.installed; return o; }));
    gameSel.value = cur.id;
  }
  async function loadGenerals() {
    try { generalsList = await invoke("skirmish_generals", { gameDir: store.cfg.game_dir, modFolders: launchFolders() }); } catch { generalsList = []; }
    // the computer levels as this game names them (a mod may rename them: Chaos has "Noob Army" and "Turbo Death Army")
    try {
      const names = await invoke("skirmish_ai_names", { gameDir: store.cfg.game_dir, modFolders: launchFolders() });
      diffs = DIFF_IDS.map((id, k) => [id, names[k] || DIFF_PLAIN[k]]);
    } catch { diffs = DIFF_IDS.map((id, k) => [id, DIFF_PLAIN[k]]); }
    // a general the new game does not have goes back to random
    for (const r of rows) if (r.faction && !generalsList.some((g) => g.id === r.faction)) r.faction = null;
  }

  // The table: you first, then one row per other place on the map.
  const table = h("div", { class: "skgrid", role: "group", "aria-label": "Skirmish players" });
  const summary = h("span", { class: "muted small" });
  const DIFF_IDS = ["easy", "medium", "brutal"], DIFF_PLAIN = ["Easy Army", "Medium Army", "Hard Army"];
  let diffs = DIFF_IDS.map((id, k) => [id, DIFF_PLAIN[k]]);
  const sel = (label, options, value, onchange) => {
    const el = h("select", { "aria-label": label, onchange: () => onchange(el.value) }, ...options.map(([v, t]) => new Option(t, v)));
    el.value = value;
    return el;
  };
  function newRows(places) {
    const wantAi = Math.min(Math.max(1, prefs.get().skAi || 1), Math.max(0, places - 1)), diff = ["easy", "medium", "brutal"].includes(prefs.get().skDiff) ? prefs.get().skDiff : "easy";
    rows = Array.from({ length: places }, (_, k) => k === 0 ? { kind: "player", faction: null, team: null, start: null } : { kind: k <= wantAi ? "ai" : "closed", difficulty: diff, faction: null, team: null, start: null });
    active = 0;
  }
  // Choosing a row only moves the highlight: rebuilding the table here would close a drop-down as it opens.
  function setActive(k) {
    if (active === k) return;
    active = k;
    table.querySelectorAll(".skrow2").forEach((r, j) => r.classList.toggle("active", j === k));
    paint(shown);
  }
  function drawTable() {
    if (!rows.length) { fill(table, h("p", { class: "muted small" }, "This map has no start positions for a skirmish (a mission or solo map).")); summary.textContent = ""; updatePlay(); return; }
    const header = h("div", { class: "skrowhead muted small" }, h("span", {}), h("span", {}, "Player"), h("span", {}, "General"), h("span", {}, "Team"), h("span", {}, "Start"));
    fill(table, header, rows.map((r, k) => {
      const closed = r.kind === "closed";
      const who = k === 0 ? h("span", { class: "skwho" }, "You")
        : sel(`Place ${k + 1}`, [...diffs, ["closed", "Closed"]], closed ? "closed" : r.difficulty, (v) => {
          if (v === "closed") r.kind = "closed"; else { r.kind = "ai"; r.difficulty = v; prefs.set({ skDiff: v }); }
          if (v === "closed" || closed) { drawTable(); paint(shown); }
        });
      const general = closed ? h("span", {}) : sel(`General for place ${k + 1}`, [["", "Random general"], ...generalsList.map((g) => [g.id, g.name])], r.faction || "", (v) => { r.faction = v || null; });
      const team = closed ? h("span", {}) : sel(`Team for place ${k + 1}`, [["", "No team"], ["0", "Team 1"], ["1", "Team 2"], ["2", "Team 3"], ["3", "Team 4"]], r.team == null ? "" : String(r.team), (v) => { r.team = v === "" ? null : +v; });
      const startSel = closed ? h("span", {}) : sel(`Start for place ${k + 1}`, [["", "Random"], ...Array.from({ length: rows.length }, (_, n) => [String(n + 1), `Start ${n + 1}`])], r.start == null ? "" : String(r.start), (v) => assign(k, v === "" ? null : +v));
      return h("div", { class: `skrow2 pc${k}` + (k === active ? " active" : "") + (closed ? " closed" : ""), onclick: () => setActive(k), onfocusin: () => setActive(k) }, h("i", { class: "swatch" }), who, general, team, startSel);
    }));
    const ai = rows.filter((r) => r.kind === "ai").length;
    summary.textContent = ai ? `You and ${ai} computer player${ai === 1 ? "" : "s"}` : "No computer players";
    updatePlay();
  }
  // Give start position n to place k; whoever had it goes back to random.
  function assign(k, n) {
    if (n != null) rows.forEach((r, j) => { if (j !== k && r.start === n) r.start = null; });
    rows[k].start = n;
    drawTable(); paint(shown);
  }
  const play = h("button", { class: "btn primary big playbtn", onclick: async () => {
    const m = entries[i];
    if (m.official && !m.games.includes(selectedGame().id)) { note.textContent = `${m.name} comes with ${gameNames(m).join(" and ")}. Choose it under "Play with".`; return; }
    play.disabled = true;
    try {
      // a switched-off map is not in the game's folder: switch it on first
      if (!m.enabled) { await invoke("maps_set_enabled", { names: [m.name], enabled: true }); m.enabled = true; listed.checked = true; if (onChange) onChange(); }
      const ai = rows.filter((r) => r.kind === "ai");
      prefs.set({ skAi: ai.length, skDiff: ai[0].difficulty });
      const slots = rows.map((r) => r.kind === "closed" ? { kind: "closed" } : { kind: r.kind, difficulty: r.kind === "ai" ? r.difficulty : null, faction: r.faction, team: r.team, start: r.start });
      if (await startGame({ skirmish: true, map: m.name, slots })) finish();
    } catch (e) { note.textContent = errText(e); }
    updatePlay();
  } }, icon("play"), "Play skirmish");
  const speedPick = speedSelect();
  const cashPick = cashSelect();
  const resPick = resolutionSelect();
  function updatePlay() {
    const ai = rows.filter((r) => r.kind === "ai").length;
    play.disabled = !ai;
    play.title = !rows.length ? "This map has no start positions for a skirmish" : ai ? "" : "Give the computer at least one place";
  }

  const overlay = h("div", { class: "overlay", role: "dialog", "aria-modal": "true", "aria-label": "Skirmish setup" },
    h("div", { class: "wizard mapviewer" },
      h("div", { class: "wiz-head mvhead" }, h("div", { class: "mvtitle" }, title, factline), h("label", { class: "inline" }, h("span", { class: "muted small" }, "Play with"), gameSel)),
      h("div", { class: "wiz-body mapbody" },
        h("div", { class: "mapcol" }, h("div", { class: "stage" }, canvas), h("div", { class: "mapnav" }, prev, position, next)),
        h("div", { class: "mapside" }, h("div", { class: "sidehead" }, h("h3", {}, "Players"), summary), table, note, h("div", { class: "playrow" }, cashPick, speedPick, aiSelect(), resPick, play))),
      h("div", { class: "wiz-foot" }, listedRow, close)));
  const onKey = (e) => { if (e.target.tagName === "SELECT") return; if (e.key === "Escape") finish(); else if (e.key === "ArrowLeft") go(-1); else if (e.key === "ArrowRight") go(1); };
  function finish() { document.removeEventListener("keydown", onKey); overlay.remove(); }
  function go(d) { const n = i + d; if (n >= 0 && n < entries.length) { i = n; show(); } }

  // Where the start markers are on the canvas, for drawing them and for clicks.
  let spots = [];
  function paint(v) {
    if (!v) return;
    shown = v;
    spots = paintMap(canvas, v, (n) => {
      const owner = rows.findIndex((r) => r.kind !== "closed" && r.start === n);
      return owner < 0 ? null : { color: PLACE_COLORS[owner], halo: owner === active };
    });
  }
  // Click a start position on the picture to give it to the selected place in the table (again to make it random).
  const spotAt = (e) => spotUnder(canvas, spots, e);
  canvas.addEventListener("click", (e) => { const s = spotAt(e); if (s && rows[active] && rows[active].kind !== "closed") assign(active, rows[active].start === s.n ? null : s.n); });
  canvas.addEventListener("mousemove", (e) => { const s = spotAt(e); canvas.style.cursor = s ? "pointer" : "default"; canvas.title = s ? `Start ${s.n}: click to give it to the selected place` : ""; });

  async function show() {
    const m = entries[i], mine = ++token;
    // an official map belongs to its game(s): play it with one of them
    if (m.official && !m.games.includes(selectedGame().id)) { prefs.set({ game: m.games[0] }); notifyMods(); drawGames(); }
    listed.checked = m.enabled;
    listedRow.hidden = !!m.official;
    position.textContent = `${i + 1} of ${entries.length}`;
    title.textContent = m.name;
    prev.disabled = i === 0; next.disabled = i === entries.length - 1;
    note.textContent = "Loading…";
    fill(factline);
    shown = null; spots = []; rows = [];
    drawTable();
    try {
      const v = m.official ? await invoke("official_map_view", { gameDir: store.cfg.game_dir, modFolders: officialFolders(m), name: m.name }) : await invoke("map_view", { name: m.name });
      if (mine !== token) return;
      const places = v.info ? Math.min(8, v.info.starts.length) : 0;
      newRows(places >= 2 ? places : 0);
      await loadGenerals();
      if (mine !== token) return;
      paint(v);
      drawTable();
      fillFacts(v, m);
    } catch (e) { if (mine === token) note.textContent = "Couldn't read this map: " + errText(e); }
  }
  function fillFacts(v, m) {
    const n = v && v.info ? v.info.starts.length : 0;
    const bits = [m.official ? `Official, ${gameNames(m).join(", ")}` : m.pack];
    if (v && v.info) bits.push(`${Math.round(v.info.width / 10)} × ${Math.round(v.info.height / 10)}`);
    bits.push(n ? `${n} players` : "Solo map");
    if (v) bits.push(v.size_kb >= 1024 ? `${(v.size_kb / 1024).toFixed(1)} MB` : `${v.size_kb} KB`);
    fill(factline, bits.map((b) => h("span", {}, b)));
    note.textContent = v && !v.rgba ? "This map has no preview picture." : n > 1 ? "Select a player, then click a start on the map." : "";
  }
  document.addEventListener("keydown", onKey);
  document.getElementById("root").append(overlay);
  drawGames();
  show();
  close.focus();
}

/** Every map, on or off: pick the ones the game should show. Switching a map off moves it aside; nothing is deleted. */
function mapOrganiser(onLoad) {
  const LIMIT = 150;
  let own = [], official = [], all = [], q = "", packs = new Set(), view = "all", shown = LIMIT, confirmDelete = false;
  let counts = null, pf = "", sortBy = "name"; // counts: map name -> players, once read (null while counting)
  const summary = h("span", { class: "pill" });
  const search = h("input", { type: "text", placeholder: "Search maps by name", "aria-label": "Search maps by name", oninput: () => { q = search.value.trim().toLowerCase(); shown = LIMIT; draw(); } });
  const searchClear = h("button", { class: "searchclear", "aria-label": "Clear the search", hidden: true, onclick: () => { q = ""; search.value = ""; shown = LIMIT; draw(); search.focus(); } }, "\u00d7");
  const searchBox = h("div", { class: "searchbox" }, search, searchClear);
  // The pack filter is a menu of tick boxes: show the maps of the ticked packs (none ticked is every pack). It only filters the list.
  const packName = h("span", { class: "packbtn-name" });
  const packBtn = h("button", { class: "btn packbtn", "aria-haspopup": "true", "aria-expanded": "false", "aria-label": "Show maps from", onclick: () => setPackMenu(packMenu.hidden) }, packName);
  const packMenu = h("div", { class: "packmenu", role: "group", "aria-label": "Map packs", hidden: true });
  const packPick = h("div", { class: "packpick" }, packBtn, packMenu);
  function setPackMenu(open) { packMenu.hidden = !open; packBtn.setAttribute("aria-expanded", String(open)); }
  const closeOutside = (e) => { if (!packMenu.hidden && !packPick.contains(e.target)) setPackMenu(false); };
  const closeOnEscape = (e) => { if (e.key === "Escape" && !packMenu.hidden) { setPackMenu(false); packBtn.focus(); } };
  // On press and in the capture phase, not on click: a drop-down opens on the press and never sends a click, and the
  // Skirmish buttons stop theirs from reaching the document. Moving the keyboard focus out closes it too.
  document.addEventListener("pointerdown", closeOutside, true);
  document.addEventListener("focusin", closeOutside);
  document.addEventListener("keydown", closeOnEscape);
  const seg = (id, label) => h("button", { class: "seg", "data-view": id, onclick: () => { view = id; shown = LIMIT; draw(); } }, label);
  const segs = h("div", { class: "segs", role: "group", "aria-label": "Show" }, seg("all", "All"), seg("on", "On"), seg("off", "Off"));
  const sortSel = h("select", { "aria-label": "Sort maps", onchange: () => { sortBy = sortSel.value; shown = LIMIT; draw(); } },
    new Option("Name", "name"), new Option("Fewest players", "players"), new Option("Most players", "players-desc"), new Option("Largest file", "size"));
  const chips = h("div", { class: "chips", role: "group", "aria-label": "Number of players" });
  const result = h("p", { class: "muted small mapresult", role: "status" });
  const clear = h("button", { class: "btn small ghost", hidden: true, onclick: () => { q = ""; packs.clear(); view = "all"; pf = ""; search.value = ""; shown = LIMIT; draw(); } }, "Clear filters");
  const list = h("div", { class: "maplist" });
  const more = h("button", { class: "btn small", hidden: true, onclick: () => { shown += LIMIT; draw(); } }, "Show more");
  const on = h("button", { class: "btn small", onclick: () => bulk(true) }, "Switch on shown");
  const off = h("button", { class: "btn small", onclick: () => bulk(false) }, "Switch off shown");
  const del = h("button", { class: "btn small ghost danger", onclick: () => remove() }, "Delete shown…");
  const empty = h("div", { class: "listempty", role: "status", hidden: true });
  // A small picture of the map under the mouse, following it. Pictures are fetched once, a moment after the mouse settles on a row.
  const TIP = 190;
  const tipCanvas = h("canvas", { width: TIP, height: TIP });
  const tip = h("div", { class: "maptip", hidden: true, "aria-hidden": "true" }, tipCanvas);
  const pictures = new Map(); // map name -> the bitmap, or null when it has none
  async function pictureOf(m) {
    if (pictures.has(m.name)) return pictures.get(m.name);
    let pic = null;
    try {
      const v = m.official ? await invoke("official_map_view", { gameDir: store.cfg.game_dir, modFolders: officialFolders(m), name: m.name }) : await invoke("map_view", { name: m.name });
      if (v.rgba) {
        const bin = atob(v.rgba), px = new Uint8ClampedArray(bin.length);
        for (let k = 0; k < bin.length; k++) px[k] = bin.charCodeAt(k);
        const bmp = document.createElement("canvas");
        bmp.width = v.image_w; bmp.height = v.image_h;
        bmp.getContext("2d").putImageData(new ImageData(px, v.image_w, v.image_h), 0, 0);
        pic = { bmp, ratio: v.info ? v.info.width / v.info.height : 1 };
      }
    } catch { /* no picture: no tip */ }
    pictures.set(m.name, pic);
    return pic;
  }
  function placeTip(e) {
    const x = e.clientX + 18 + TIP > innerWidth ? e.clientX - 18 - TIP : e.clientX + 18;
    const y = Math.min(Math.max(8, e.clientY - TIP / 2), innerHeight - TIP - 8);
    tip.style.transform = `translate(${x}px, ${y}px)`;
  }
  function hoverPreview(target, m) {
    let timer = null, token = 0;
    target.addEventListener("mouseenter", (e) => {
      const mine = ++token;
      placeTip(e);
      timer = setTimeout(async () => {
        const pic = await pictureOf(m);
        if (mine !== token || !pic) return;
        const cw = pic.ratio >= 1 ? TIP : Math.round(TIP * pic.ratio), ch = pic.ratio >= 1 ? Math.round(TIP / pic.ratio) : TIP;
        tipCanvas.width = cw; tipCanvas.height = ch; // the frame takes the map's own proportions
        const ctx = tipCanvas.getContext("2d");
        ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = "high";
        ctx.drawImage(pic.bmp, 0, 0, cw, ch);
        tip.hidden = false;
      }, 140);
    });
    target.addEventListener("mousemove", placeTip);
    target.addEventListener("mouseleave", () => { token++; clearTimeout(timer); tip.hidden = true; });
  }
  const players = (m) => (m.official ? m.players : counts && m.name in counts ? counts[m.name] : null);
  const playerText = (n) => (n === null ? "" : n === 0 ? "Solo" : `${n} players`);
  const base = () => all.filter((m) => (!q || m.name.toLowerCase().includes(q)) && (!packs.size || packs.has(m.pack)) && (view === "all" || (view === "on") === m.enabled));
  const visible = () => {
    const v = base().filter((m) => pf === "" || players(m) === Number(pf));
    const byName = (a, b) => a.name.toLowerCase().localeCompare(b.name.toLowerCase());
    if (sortBy === "size") v.sort((a, b) => b.size_kb - a.size_kb || byName(a, b));
    else if (sortBy.startsWith("players")) { const d = sortBy === "players" ? 1 : -1; v.sort((a, b) => d * ((players(a) ?? 99) - (players(b) ?? 99)) || byName(a, b)); }
    return v;
  };
  function drawChips() {
    // the player counts that exist among the maps the other filters leave, so a chip never leads to nothing
    const have = new Map();
    if (counts) for (const m of base()) { const n = players(m); if (n !== null) have.set(n, (have.get(n) || 0) + 1); }
    if (pf !== "" && !have.has(Number(pf))) pf = "";
    const chip = (value, label) => h("button", { class: "chip", "aria-pressed": String(pf === value), onclick: () => { pf = value; shown = LIMIT; draw(); } }, label);
    fill(chips,
      h("span", { class: "muted small chiplabel" }, "Players"),
      chip("", "Any"),
      ...[...have.keys()].sort((a, b) => a - b).map((n) => {
        const c = chip(String(n), [n === 0 ? "Solo" : String(n), h("span", { class: "chipcount", "aria-hidden": "true" }, String(have.get(n)))]);
        const what = `${have.get(n)} ${n === 0 ? "solo" : n + "-player"} map${have.get(n) === 1 ? "" : "s"}`;
        c.title = what; c.setAttribute("aria-label", what);
        return c;
      }),
      counts ? null : h("span", { class: "muted small" }, "counting…"));
  }
  function drawPacks(names) {
    const total = (p) => all.filter((m) => m.pack === p).length;
    fill(packName, packs.size === 0 ? "All packs" : packs.size === 1 ? [...packs][0] : `${packs.size} packs`);
    packBtn.classList.toggle("filtered", packs.size > 0);
    const row = (label, count, checked, onchange) => h("label", { class: "packrow" }, h("input", { type: "checkbox", checked, "aria-label": label, onchange }), h("span", { class: "packlabel" }, label), h("span", { class: "muted small" }, String(count)));
    packMenu.replaceChildren(
      row("All packs", all.length, packs.size === 0, () => { packs.clear(); shown = LIMIT; draw(); }),
      ...names.map((p) => row(p, total(p), packs.has(p), (e) => { if (e.target.checked) packs.add(p); else packs.delete(p); shown = LIMIT; draw(); })));
  }
  function drawSummary() {
    const mine = all.filter((m) => !m.official), n = mine.filter((m) => m.enabled).length;
    summary.textContent = mine.length ? `${n} of ${mine.length} added maps switched on` : "";
    summary.className = "pill" + (n ? " ok" : "");
  }
  function draw() {
    const packNames = [...new Set(all.map((m) => m.pack))].sort();
    for (const p of [...packs]) if (!packNames.includes(p)) packs.delete(p);
    drawPacks(packNames);
    segs.querySelectorAll(".seg").forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.view === view)));
    const v = visible();
    drawSummary();
    drawChips();
    result.textContent = all.length ? (v.length === all.length ? `${all.length} maps` : `${v.length} of ${all.length} maps`) : "";
    clear.hidden = !(q || packs.size || view !== "all" || pf !== "");
    searchClear.hidden = !q;
    empty.hidden = !!v.length;
    list.hidden = !v.length;
    fill(empty, all.length
      ? [h("strong", {}, "No maps match"), h("span", { class: "muted small" }, q ? `Nothing is called \u201c${search.value.trim()}\u201d with these filters.` : "No map fits these filters."), h("button", { class: "btn small", onclick: () => clear.click() }, "Clear filters")]
      : [h("strong", {}, "No maps yet"), h("span", { class: "muted small" }, "Install a pack below, or add your own maps from a file or folder.")]);
    fill(list, v.slice(0, shown).map((m) => {
      const box = h("input", { type: "checkbox", checked: m.enabled, "aria-label": `Use ${m.name}`, onchange: async () => {
        try { await invoke("maps_set_enabled", { names: [m.name], enabled: box.checked }); m.enabled = box.checked; row.classList.toggle("on", m.enabled); drawSummary(); }
        catch (e) { box.checked = m.enabled; say(errText(e), "bad"); }
      } });
      const idx = v.indexOf(m);
      const row = h(m.official ? "div" : "label", { class: "maprow" + (m.official ? " official" : m.enabled ? " on" : "") + (!m.official && store.cfg.editor_available ? " editable" : "") }, m.official ? h("span", {}) : box,
        h("span", { class: "maptext" }, h("span", { class: "mapname" }, m.name), m.official ? h("span", { class: "maptags" }, h("span", { class: "officialtag" }, "Official"), ...gameNames(m).map((g) => h("span", { class: "gametag" }, g))) : h("span", { class: "muted small mappack-tag" }, m.pack)), h("span", { class: "small mapplayers" }, playerText(players(m))), h("span", { class: "muted small mapsize" }, m.size_kb >= 1024 ? `${(m.size_kb / 1024).toFixed(1)} MB` : `${m.size_kb} KB`),
        m.official || !store.cfg.editor_available ? null : (() => { const b = h("button", { class: "btn small", "aria-label": `Edit ${m.name} in World Builder`, title: `Open ${m.name} in World Builder with ${editorGame().name}`, onclick: (e) => { e.preventDefault(); e.stopPropagation(); launchEditor(m.name, [b]); } }, "Edit"); return b; })(),
        h("button", { class: "btn small", "aria-label": `Set up a skirmish on ${m.name}`, title: "Choose players, generals, teams and start positions", onclick: (e) => { e.preventDefault(); e.stopPropagation(); openMapViewer(v, idx, () => { row.classList.toggle("on", m.enabled); box.checked = m.enabled; drawSummary(); }); } }, icon("play"), "Skirmish"));
      hoverPreview(row.querySelector(".mapname"), m); // only the title, not the buttons or the rest of the row
      return row;
    }));
    more.hidden = v.length <= shown;
    more.textContent = `Show more (${v.length - shown} left)`;
    const yours = v.filter((m) => !m.official).length; // the game's own maps can't be switched off or deleted
    [on, off, del].forEach((b) => { b.disabled = !yours; });
    del.textContent = confirmDelete ? `Click again to delete ${yours} map${yours === 1 ? "" : "s"}` : "Delete shown…";
  }
  async function bulk(enabled) {
    const names = visible().filter((m) => !m.official && m.enabled !== enabled).map((m) => m.name);
    if (!names.length) return;
    on.disabled = off.disabled = true;
    try {
      const n = await invoke("maps_set_enabled", { names, enabled });
      all.forEach((m) => { if (names.includes(m.name)) m.enabled = enabled; });
      say(`${n} map${n === 1 ? "" : "s"} switched ${enabled ? "on" : "off"}`, "ok");
    } catch (e) { say(errText(e), "bad"); }
    draw();
  }
  async function remove() {
    if (!confirmDelete) { confirmDelete = true; draw(); setTimeout(() => { confirmDelete = false; draw(); }, 4000); return; }
    confirmDelete = false;
    const names = visible().filter((m) => !m.official).map((m) => m.name);
    try { const n = await invoke("maps_delete", { names }); say(`${n} map${n === 1 ? "" : "s"} deleted`, "ok"); } catch (e) { say(errText(e), "bad"); }
    await reloadOrganiser();
  }
  // The skirmish maps of every installed game, each tagged with its game(s). A map of yours with the same name wins, as in the game.
  function merge() {
    const taken = new Set(own.map((m) => m.name.toLowerCase()));
    all = [...own, ...official.filter((o) => !taken.has(o.name.toLowerCase())).map((o) => ({ name: o.name, pack: "Official", enabled: true, official: true, size_kb: o.size_kb, players: o.players, games: o.games }))]
      .sort((a, b) => a.name.toLowerCase().localeCompare(b.name.toLowerCase()));
  }
  async function loadOfficial() {
    if (!store.cfg.game_dir) return;
    try { official = await invoke("maps_official", { gameDir: store.cfg.game_dir }); } catch { official = []; }
    merge();
    draw();
  }
  async function reloadOrganiser() {
    try { own = await invoke("maps_organiser"); } catch (e) { say(errText(e), "bad"); own = []; }
    merge();
    draw();
    if (onLoad) onLoad();
    loadOfficial();
    // reading each map's start positions takes a moment the first time; the list is usable meanwhile
    invoke("maps_players").then((c) => { counts = c; draw(); }).catch(() => { counts = {}; draw(); });
  }
  refreshers.add(reloadOrganiser);
  reloadOrganiser();
  document.body.append(tip); // on the body: a page that is mid-animation has a transform, which would move a fixed child
  const el = h("div", { class: "organiser" },
    h("div", { class: "row between" }, h("h2", {}, "Your maps"), summary),
    h("div", { class: "organiser-bar" }, searchBox, packPick, segs),
    chips,
    h("div", { class: "listbox" },
      h("div", { class: "listhead" }, h("div", { class: "row" }, result, clear), h("label", { class: "sortpick" }, h("span", { class: "muted small" }, "Sort by"), sortSel)),
      list,
      empty,
      h("div", { class: "listfoot" }, h("div", { class: "row" }, on, off), del)),
    h("div", { class: "row center" }, more),
    h("p", { class: "muted small" }, "Switched-off maps are moved aside, not deleted. Changes apply the next time the game starts."));
  return { el, packCount: (name) => own.filter((m) => m.pack === name).length, dispose: () => { tip.remove(); refreshers.delete(reloadOrganiser); document.removeEventListener("pointerdown", closeOutside, true); document.removeEventListener("focusin", closeOutside); document.removeEventListener("keydown", closeOnEscape); } };
}

// ---------------------------------------------------------------- map editor (World Builder)
/** The game or mod the editor loads: what Play is set to, unless another was picked in the menu since the Maps page opened. Extras stay a Play matter. */
let editorPick = null;
function editorGame() {
  return gameChoices().find((g) => g.id === editorPick && g.installed) || selectedGame();
}
let editorBusy = false;
/** Start the editor, on one of the player's maps or empty. It takes several seconds to show, so the buttons wait instead of starting a second copy. */
async function launchEditor(map, buttons) {
  if (editorBusy) return;
  editorBusy = true;
  buttons.forEach((b) => { b.disabled = true; });
  const kids = [...buttons[0].childNodes];
  buttons[0].replaceChildren("Starting\u2026");
  const done = () => { editorBusy = false; buttons.forEach((b) => { b.disabled = false; }); buttons[0].replaceChildren(...kids); };
  try {
    await invoke("launch_editor", { gameDir: store.cfg.game_dir, modFolders: launchFolders(editorGame(), []), map: map || null });
    setTimeout(done, 8000);
  } catch (e) { done(); say(errText(e), "bad"); }
}
/** "Open World Builder \u00b7 Shockwave" with a caret to pick another installed game or mod. Nothing where there is no editor. */
function editorControl() {
  if (!store.cfg.editor_available) return null;
  editorPick = null; // each visit starts from the Play selection
  const name = h("span", { class: "muted" });
  const main = h("button", { class: "btn small", onclick: () => launchEditor(null, [main]) }, "Open World Builder", name);
  const menu = h("div", { class: "menu", role: "menu", "aria-label": "Game or mod for World Builder", hidden: true });
  const caret = h("button", { class: "btn small caret", "aria-haspopup": "menu", "aria-expanded": "false", "aria-label": "Choose the game or mod for World Builder", onclick: () => toggle(menu.hidden) }, "\u25be");
  function toggle(open) {
    menu.hidden = !open;
    caret.setAttribute("aria-expanded", String(open));
    if (open) { const on = menu.querySelector("[aria-checked=true]"); (on || menu.firstChild).focus(); }
  }
  function draw() {
    const cur = editorGame();
    name.textContent = `\u00b7 ${cur.name}`;
    fill(menu, gameChoices().filter((g) => g.installed).map((g) => h("button", { class: "menuitem", role: "menuitemradio", "aria-checked": String(g.id === cur.id), onclick: () => { editorPick = g.id; draw(); toggle(false); caret.focus(); } }, g.id === cur.id ? "\u2713 " : "\u2003", g.name)));
  }
  draw();
  const el = h("div", { class: "splitbtn" }, main, caret, menu);
  const closeOutside = (e) => { if (!menu.hidden && !el.contains(e.target)) toggle(false); };
  const onKey = (e) => { if (e.key === "Escape" && !menu.hidden) { toggle(false); caret.focus(); } };
  document.addEventListener("pointerdown", closeOutside, true);
  document.addEventListener("keydown", onKey);
  refreshers.add(draw); // the list of installed games changes while the page is open
  el.dispose = () => { document.removeEventListener("pointerdown", closeOutside, true); document.removeEventListener("keydown", onKey); refreshers.delete(draw); };
  return el;
}

function mapsPage() {
  // the packs list says which packs are already in the library, so it follows the organiser
  let packs = null;
  const org = mapOrganiser(() => packs && packs.render());
  packs = mapsCard((name) => org.packCount(name));
  const editor = { el: editorControl() };
  const el = h("div", { class: "page" },
    h("div", { class: "page-head" }, h("h1", {}, "Maps"), editor.el),
    h("div", { class: "skbanner" }, h("span", { class: "skbanner-icon", "aria-hidden": "true" }, icon("play")), h("div", {}, h("h3", {}, "Play a skirmish on any map"), h("p", { class: "muted small" }, "Press Skirmish on a map below to choose who plays, their generals, teams and where they start."))),
    h("div", { class: "card" }, org.el),
    h("div", { class: "card" }, h("h2", {}, "Get more maps"), packs.el));
  return { el, dispose: () => { packs.dispose(); org.dispose(); editor.el && editor.el.dispose(); } };
}

// ---------------------------------------------------------------- components
/** A chip for the state of one thing (a mod is installed, waiting, or blocked). */
function pill(text, kind) { return h("span", { class: "pill " + (kind || "") }, text); }
/** A plain status line with a dot, for ambient state in the sidebar. */
function status(text, kind) { return h("span", { class: "status " + (kind || "") }, text); }

/** What differs between the two games' file panels. */
const FILES = {
  generals: {
    file: "INIZH.big", dir: () => store.cfg.game_dir, setDir: (d) => { store.cfg.game_dir = d; },
    found: () => store.games.filter((g) => g.zero_hour),
    check: async (path) => { const r = await invoke("check_game_dir", { path }); if (r.ok) await invoke("save_game_dir", { path }); return r; },
    steam: "Downloads Generals and Zero Hour (about 4 GB) through Steam. You must own both on Steam.",
  },
  ra2: {
    file: "ra2md.mix", dir: () => store.cfg.ra2.game_dir, setDir: (d) => { store.cfg.ra2.game_dir = d; },
    found: () => store.ra2Games,
    check: (path) => invoke("ra2_check_dir", { path }),
    steam: "Downloads Red Alert 2 and Yuri's Revenge (about 3 GB) through Steam. You must own them on Steam.",
  },
};

/** "Choose or download the game files". `onChange(ok)` fires whenever validity changes. */
function gameFilesPanel(onChange, game = "generals") {
  const timers = [];
  const g$ = FILES[game];
  const dir = h("input", { type: "text", placeholder: `Folder that contains ${g$.file}`, value: g$.dir() || "", "aria-label": "Game folder" });
  const status = h("div", { class: "msg muted", role: "status" });
  const detected = h("div", { class: "stack" });

  async function check(path) {
    const r = await g$.check(path);
    status.className = "msg " + (r.ok ? "ok" : path.trim() ? "bad" : "muted");
    status.textContent = (r.ok ? "✓ " : path.trim() ? "✗ " : "") + r.message;
    if (r.ok) g$.setDir(path.trim());
    onChange && onChange(r.ok);
    return r.ok;
  }
  dir.addEventListener("change", () => check(dir.value));
  const browse = h("button", { class: "btn", type: "button", onclick: async () => {
    let picked;
    try {
      picked = await tauri.dialog.open({
        multiple: false, directory: true, defaultPath: dir.value || undefined,
        title: `Choose the folder that contains ${g$.file}`,
      });
    } catch (e) {
      status.className = "msg bad";
      status.textContent = "Couldn't open the folder picker: " + errText(e);
      return;
    }
    if (!picked) return;
    dir.value = typeof picked === "string" ? picked : picked.path;
    check(dir.value);
  } }, "Browse");

  const zh = g$.found();
  zh.forEach((g) => {
    detected.append(h("button", { class: "btn", onclick: () => { dir.value = g.dir; check(g.dir); } }, "Use Steam install: " + g.dir));
  });

  // ---- download from Steam
  const user = h("input", { type: "text", autocomplete: "username", value: store.cfg.steam_user || "", "aria-label": "Steam account name" });
  const pass = h("input", { type: "password", autocomplete: "current-password", "aria-label": "Steam password" });
  const go = h("button", { class: "btn primary", onclick: async () => {
    try { await invoke("assets_start", { user: user.value, password: pass.value, title: game }); pass.value = ""; } catch (e) { smsg.textContent = errText(e); }
  } }, "Download from Steam");
  const cancel = h("button", { class: "btn", hidden: true, onclick: () => invoke("assets_cancel") }, "Cancel");
  const codeBtn = h("button", { class: "btn", hidden: true, onclick: () => invoke("assets_use_code") }, "Enter a code instead");
  const needLabel = h("span");
  const needInput = h("input", { type: "text", autocomplete: "one-time-code" });
  const needBtn = h("button", { class: "btn primary", onclick: () => { invoke("assets_submit", { text: needInput.value }); needInput.value = ""; } }, "Send");
  const need = h("div", { class: "grid2", hidden: true }, h("label", { class: "field" }, needLabel, needInput), h("div", { class: "row" }, needBtn));
  const bar = h("progress", { max: 100, value: 0, hidden: true });
  const smsg = h("div", { class: "msg muted", role: "status" });
  const log = h("pre", { class: "log" });
  let wasDone = false;
  timers.push(setInterval(async () => {
    const s = await invoke("assets_status");
    const active = ["Preparing", "LoggingIn", "Downloading"].includes(s.phase);
    go.hidden = active; cancel.hidden = !active; codeBtn.hidden = !s.phone;
    bar.hidden = s.phase !== "Downloading"; bar.value = s.percent;
    need.hidden = !s.need;
    if (s.need) {
      needLabel.textContent = s.need === "Guard" ? "Steam Guard code" : "Steam password";
      needInput.type = s.need === "Guard" ? "text" : "password";
      needBtn.textContent = s.need === "Guard" ? "Send code" : "Send password";
    }
    smsg.className = s.phone ? "banner" : "msg " + (s.phase === "Failed" ? "bad" : s.phase === "Done" ? "ok" : "muted");
    smsg.textContent = s.phase === "Idle" ? "" : s.message + (s.phase === "Downloading" ? ` ${s.percent.toFixed(0)}%` : "");
    if (s.log) log.textContent = s.log;
    if (s.phase === "Done" && !wasDone) {
      store.cfg = await invoke("get_config");
      dir.value = g$.dir();
      check(dir.value);
    }
    wasDone = s.phase === "Done";
  }, 800));

  const steam = h("details", {},
    h("summary", {}, "Don't have the files on this computer? Download them from Steam"),
    h("div", { class: "stack" },
      h("p", { class: "muted small" }, `${g$.steam} Your password is used only to sign in and is never saved.`),
      h("div", { class: "grid2" }, h("label", { class: "field" }, h("span", {}, "Steam account name"), user), h("label", { class: "field" }, h("span", {}, "Steam password"), pass)),
      h("div", { class: "row" }, go, cancel, codeBtn), need, bar, smsg,
      h("details", {}, h("summary", { class: "small muted" }, "Details"), log)));

  const el = h("div", { class: "stack" },
    detected,
    h("label", { class: "field" }, h("span", {}, "Game folder"), h("div", { class: "row pick" }, dir, browse)),
    status, steam);

  // initial state: remembered folder, else the first Steam install
  (async () => {
    if (dir.value) await check(dir.value);
    else if (zh[0]) { dir.value = zh[0].dir; await check(dir.value); }
    else { onChange && onChange(false); status.textContent = `Choose the folder that contains ${g$.file}`; }
  })();
  return { el, dispose: () => timers.forEach(clearInterval) };
}

/** What a mod is and how it is played, in one line. */
function modRole(m) {
  const base = m.addon_for ? store.mods.catalog.find((b) => b.id === m.addon_for) : null;
  if (base) return { pill: `Add-on for ${base.name}`, how: base.installed ? `Plays together with ${base.name}, which is already installed.` : `Needs ${base.name} installed first. Rallypoint plays the two together.`, base };
  const adds = store.mods.catalog.filter((x) => x.addon_for === m.id).map((x) => x.name);
  return { pill: "Plays on its own", how: adds.length ? `Play it by itself, or add ${adds.join(" and ")} on top.` : "Play it by itself.", base: null };
}

/** Two letters to stand for a mod where a picture would be: "C&C: Shockwave Chaos" -> "SC". */
function modInitials(name) {
  const words = name.replace(/^C&C:?\s*/i, "").split(/[^A-Za-z0-9]+/).filter(Boolean);
  return ((words[0] || "?")[0] + (words[1] ? words[1][0] : (words[0] || "").slice(1, 2))).toUpperCase();
}
/** One of six tile colours, always the same one for the same mod. */
const modHue = (name) => "hue" + ([...name].reduce((a, c) => (a * 31 + c.charCodeAt(0)) >>> 0, 7) % 6);
const modTile = (name, kind) => h("span", { class: `modtile ${modHue(name)} ${kind || ""}`, "aria-hidden": "true" }, modInitials(name));
/** A Remove button that asks for a second click. */
function removeButton(folder, name) {
  let armed = false;
  const rm = h("button", { class: "btn small danger", onclick: async () => {
    if (!armed) { armed = true; rm.textContent = "Click again to remove"; setTimeout(() => { armed = false; rm.textContent = "Remove"; }, 3000); return; }
    try { await invoke("mods_remove", { folder }); say(name + " removed"); } catch (e) { say(errText(e), "bad"); }
    loadMods();
  } }, "Remove");
  return rm;
}

function modCard(m, compact) {
  const waiting = store.watch && store.watch.id === m.id;
  const busy = store.installing && store.installing.id === m.id;
  const role = modRole(m);
  const page = (cmd) => () => invoke(cmd, { id: m.id }).catch((e) => say(errText(e), "bad"));
  const actions = h("div", { class: "row end" });
  if (busy) {
    actions.append(h("span", { class: "live busy", role: "status" }, h("span", { class: "spin", "aria-hidden": "true" }), "Installing…"));
  } else if (m.installed) {
    actions.append(pill("Installed", "ok"));
    if (!compact) actions.append(removeButton(m.path, m.name));
  } else if (waiting) {
    actions.append(pill("Waiting for download", "warn"));
  } else if (m.pick) {
    // The player brings the files: first get them from the mod's own page, then hand them over.
    actions.append(
      h("button", { class: "btn", onclick: page("open_download_page") }, "Download page"),
      h("button", { class: "btn primary", onclick: () => choose(m.id, false) }, "Choose file…"));
  } else {
    actions.append(h("button", { class: "btn primary", onclick: () => startWatch(m) }, "Get"));
  }
  const missingBase = role.base && !role.base.installed && !m.installed;
  return h("div", { class: "card modcard" + (role.base ? " addon" : "") },
    h("div", { class: "mod" },
      modTile(m.name, m.installed ? "on" : ""),
      h("div", { class: "stack tight" },
        h("div", { class: "row" }, h("h3", {}, m.name), role.base ? pill(role.pill) : null),
        h("p", { class: "muted" }, m.description),
        missingBase ? h("p", { class: "small warn-text" }, role.how) : null,
        !compact && m.notes && !m.installed ? h("details", { class: "help-inline" }, h("summary", {}, "How to install"), h("p", { class: "muted small" }, m.notes)) : null,
        h("div", { class: "mod-links" },
          m.pick && !m.installed && !busy && !waiting ? h("button", { class: "linkbtn", onclick: () => choose(m.id, true) }, "Choose a folder instead") : null,
          h("button", { class: "linkbtn", onclick: page("open_homepage") }, "Mod page"))),
      actions),
    watchBanner(m.id),
    busy ? h("div", { class: "jobline", role: "status" }, h("span", { class: "spin", "aria-hidden": "true" }), h("span", {}, "Unpacking and installing. A large mod takes a minute or two.")) : null);
}

/** The catalog mods that are (or are not) installed, each followed by its add-ons in the same state, so how they relate is plain. */
function modGroups(installed) {
  const all = store.mods.catalog.filter((m) => !!m.installed === installed);
  const ids = new Set(all.map((m) => m.id));
  return all.filter((m) => !m.addon_for || !ids.has(m.addon_for)).flatMap((m) => [
    modCard(m, false),
    ...all.filter((x) => x.addon_for === m.id).map((x) => h("div", { class: "addons" }, modCard(x, false))),
  ]);
}

/** What to do next for the mod being downloaded, shown inside that mod's own card so it is where you just clicked. */
function watchBanner(id) {
  const w = store.watch;
  if (!w || store.installing || w.id !== id) return null;
  return h("div", { class: "banner stack" },
    h("div", {}, h("b", {}, `Download ${w.name} in your browser`), h("p", { class: "muted small" }, "When the download has finished, choose the file here. Shockwave's own Windows launcher is not started; Rallypoint unpacks the installer and loads the game archives.")),
    w.found ? h("div", { class: "row" }, h("span", { class: "small" }, "Found in Downloads: "), h("code", {}, w.found.name), h("button", { class: "btn small primary", onclick: () => installPath(w.found.path, w.id) }, "Install this file")) : null,
    h("div", { class: "row" }, h("button", { class: "btn primary", onclick: () => choose(w.id, false) }, "Choose downloaded file…"), h("button", { class: "btn", onclick: cancelWatch }, "Cancel")));
}

/** A small row of buttons to pick one of a few values (a segmented control). `value` is the picked one. */
function segmented(label, options, current, onPick) {
  const api = { value: current };
  const buttons = options.map(([value, text]) => h("button", { class: "seg", type: "button", "aria-pressed": String(value === current), onclick: () => {
    api.value = value;
    buttons.forEach((b, i) => b.setAttribute("aria-pressed", String(options[i][0] === value)));
    onPick && onPick(value);
  } }, text));
  api.el = h("div", { class: "segs small", role: "group", "aria-label": label }, ...buttons);
  api.disable = (off) => buttons.forEach((b) => { b.disabled = off; });
  return api;
}

/** Game speed for a skirmish, remembered. 100 is normal and sends nothing to the game. */
const SPEEDS = [[50, "Slow (50%)"], [75, "Slower (75%)"], [100, "Normal speed"], [125, "Faster (125%)"], [150, "Fast (150%)"], [200, "Very fast (200%)"]];
function speedSelect() {
  const sel = h("select", { class: "speedsel", "aria-label": "Game speed", title: "Game speed for this skirmish" },
    ...SPEEDS.map(([v, t]) => h("option", { value: String(v) }, t)));
  const saved = prefs.get().skSpeed;
  sel.value = SPEEDS.some(([v]) => v === saved) ? String(saved) : "100";
  const wrap = h("div", { class: "speedpick" }, icon("speed"), sel);
  const mark = () => wrap.classList.toggle("changed", sel.value !== "100"); // a speed other than normal is lit up
  mark();
  sel.addEventListener("change", () => { prefs.set({ skSpeed: +sel.value }); mark(); });
  return wrap;
}
const skirmishSpeed = () => (SPEEDS.some(([v]) => v === prefs.get().skSpeed) ? prefs.get().skSpeed : 100);

/** Starting money for a skirmish, remembered. $10,000 unless the player chose another amount. */
const DEFAULT_CASH = 10000;
const CASHES = [[5000, "$5,000"], [10000, "$10,000"], [20000, "$20,000"], [50000, "$50,000"], [100000, "$100,000"], [250000, "$250,000"], [500000, "$500,000"], [750000, "$750,000"]];
function cashSelect() {
  const sel = h("select", { class: "speedsel", "aria-label": "Starting money", title: "Starting money for this skirmish" },
    ...CASHES.map(([v, t]) => h("option", { value: String(v) }, t)));
  sel.value = String(skirmishCash());
  const wrap = h("div", { class: "speedpick" }, icon("cash"), sel);
  const mark = () => wrap.classList.toggle("changed", sel.value !== String(DEFAULT_CASH)); // money other than the default is lit up
  mark();
  sel.addEventListener("change", () => { prefs.set({ skCash: +sel.value }); mark(); });
  return wrap;
}
/** Screen resolution, remembered with the Play page's choice (the same setting). Compact, beside money and speed.
 *  A custom size set on the Play page is shown as it is; it is edited there. */
function resolutionSelect() {
  const nat = nativeRes(), p = prefs.get();
  const presets = [[3840, 2160], [2560, 1440], [1920, 1080], [1600, 900], [1280, 720]].filter(([w]) => w < nat.w);
  const sel = h("select", { class: "speedsel", "aria-label": "Screen resolution", title: `Screen resolution for this game (your screen is ${nat.w} × ${nat.h})` },
    new Option("Native", "native"), ...presets.map(([w, hh]) => new Option(`${w} × ${hh}`, `${w}x${hh}`)),
    ...(p.res === "custom" ? [new Option(`${p.cw || nat.w} × ${p.ch || nat.h}`, "custom")] : []));
  sel.value = [...sel.options].some((o) => o.value === p.res) ? p.res : "native";
  const wrap = h("div", { class: "speedpick" }, icon("screen"), sel);
  const mark = () => wrap.classList.toggle("changed", sel.value !== "native"); // anything but native is lit up
  mark();
  sel.addEventListener("change", () => { prefs.set({ res: sel.value }); mark(); });
  return wrap;
}

const AI_BOOSTS = [[0, "Off"], [1, "Sharper"], [2, "Strong"], [3, "Relentless"]];
// Stronger AI is one setting everywhere (Play options, the skirmish dialog, the Online page's Room card). In a room the
// master's choice is everyone's: the others see it, locked, and the controls follow it.
const aiRoom = () => { const d = net.last; return d && d.room_master != null ? { master: !!d.room_master, level: d.ai_level ?? 0 } : null; };
let aiHeld = 0, aiAsked = null; // a level just asked for stays on screen until the relay confirms it (or 5 s pass)
// A skirmish is local, so it always uses the player's own level (`aiLocal`); the room's level is only for games through the relay.
const aiLocal = () => (AI_BOOSTS.some(([v]) => v === prefs.get().aiBoost) ? prefs.get().aiBoost : 0);
const aiBoost = () => {
  const r = aiRoom();
  if (r) return aiAsked != null && Date.now() - aiHeld < 5000 ? aiAsked : r.level;
  return aiLocal();
};
/** Why the control is locked, or "" when it is not. */
const aiLockedNote = () => { const r = aiRoom(); return r && !r.master ? "The room master can set this if they choose to" : ""; };
async function setAiBoost(v) {
  const r = aiRoom();
  if (!r) { prefs.set({ aiBoost: v }); return; }
  if (!r.master) return;
  aiHeld = Date.now(); aiAsked = v;
  try { await invoke("relay_set_ai", { level: v }); } catch (e) { aiAsked = null; say(errText(e), "bad"); }
  window.dispatchEvent(new Event("net"));
}
/** Keep a Stronger AI select showing the shared level and locked for players who are not the room master. `after` runs on each refresh. */
function bindAiSelect(sel, after) {
  const sync = () => {
    sel.value = String(aiBoost());
    sel.disabled = !!aiLockedNote();
    if (after) after();
  };
  const onNet = () => { if (!sel.isConnected) window.removeEventListener("net", onNet); else sync(); };
  window.addEventListener("net", onNet);
  sync();
  sel.addEventListener("change", () => setAiBoost(+sel.value));
}
/** The Stronger AI level beside the other skirmish options (money, speed, screen). */
function aiSelect() {
  const sel = h("select", { class: "speedsel", "aria-label": "Stronger AI", title: "How fast the computer players build and gather" },
    ...AI_BOOSTS.map(([v, t]) => h("option", { value: String(v) }, v ? `${t} AI` : "Normal AI")));
  sel.value = String(aiLocal());
  const wrap = h("div", { class: "speedpick" }, icon("ai"), sel);
  const mark = () => wrap.classList.toggle("changed", sel.value !== "0"); // anything but normal is lit up
  mark();
  sel.addEventListener("change", () => { prefs.set({ aiBoost: +sel.value }); mark(); });
  return wrap;
}
const skirmishCash = () => (CASHES.some(([v]) => v === prefs.get().skCash) ? prefs.get().skCash : DEFAULT_CASH);

/** Start the game with the saved display choices and mod selection. `skirmish` (with an optional map, opponents and difficulty)
 *  goes straight into a skirmish. Returns whether the game was started. */
async function startGame({ skirmish = false, map = null, ai = null, difficulty = null, start = null, slots = null } = {}) {
  if (!store.cfg.game_dir) { openWizard(); return false; }
  const p = prefs.get(), nat = nativeRes();
  const { w, h: hh } = p.res === "native" ? nat : p.res === "custom" ? { w: p.cw || nat.w, h: p.ch || nat.h } : (([x, y]) => ({ w: +x, h: +y }))(String(p.res).split("x"));
  const busy = busyOverlay(map ? `Starting a skirmish on ${map}…` : skirmish ? "Starting a skirmish…" : "Starting the game…");
  let started = false;
  try {
    await invoke("launch_game", { gameDir: store.cfg.game_dir, modFolders: launchFolders(), width: w, height: hh, fullscreen: !!p.fs, skirmish, raisedCamera: !!p.camera, skirmishMap: map, skirmishAi: ai, skirmishDifficulty: difficulty, skirmishStart: start, skirmishSpeed: slots ? skirmishSpeed() : null, skirmishCash: slots && skirmishCash() ? skirmishCash() : null, aiBoost: aiLocal(), skirmishSlots: slots });
    started = true;
  } catch (e) { say(errText(e), "bad"); }
  // The game's own window takes a few seconds to appear; keep the notice up until then.
  setTimeout(() => busy.close(), started ? 5000 : 0);
  return started;
}

// ---------------------------------------------------------------- pages
function playPage() {
  const p = prefs.get();
  const nat = nativeRes();
  const picks = h("div", { class: "picks", role: "group", "aria-label": "Extra mods" });
  const game = h("select", { "aria-label": "Game to play" });
  const gameNote = h("span", { class: "muted small" });
  const extrasField = h("div", { class: "field" }, h("span", {}, "Extra mods"), picks, h("span", { class: "muted small" }, "Loaded on top of the game above."));
  const resSel = h("select", { "aria-label": "Resolution" });
  const cw = h("input", { type: "number", min: 640, value: nat.w, "aria-label": "Width" });
  const ch = h("input", { type: "number", min: 480, value: nat.h, "aria-label": "Height" });
  const custom = h("div", { class: "row", hidden: true }, cw, "×", ch);
  const fs = h("input", { type: "checkbox", checked: p.fs, onchange: () => prefs.set({ fs: fs.checked }) });
  const cam = h("input", { type: "checkbox", checked: !!p.camera, onchange: () => prefs.set({ camera: cam.checked }) });
  const aiSel = h("select", { "aria-label": "Stronger AI" }, ...AI_BOOSTS.map(([v, t]) => h("option", { value: String(v) }, t)));
  const aiNote = h("span", {});
  bindAiSelect(aiSel, () => {
    const r = aiRoom();
    aiNote.textContent = !r ? "Need a bit more of a challenge? Make the AI build and gather faster." : r.master ? "Want to change how the AI plays for everyone? You're the room master, so this applies to the whole room." : "The room master can set this if they choose to.";
  });
  const files = h("div", { class: "row between" });
  const heroTitle = h("h2", {}), heroSub = h("p", { class: "muted" });
  // The chosen game's own picture, read from the player's files, fading in from the right edge of the card. Two layers:
  // the new picture fades in while the old one fades out.
  const art = [0, 1].map(() => h("img", { class: "heroart", alt: "", "aria-hidden": "true" }));
  let artKey = "", artFront = 0, hasArt = false;
  const paintArt = () => art.forEach((el, i) => el.classList.toggle("shown", hasArt && i === artFront));
  async function loadArt(folders) {
    const key = `${store.cfg.game_dir}|${folders.join("/")}`;
    if (key === artKey) return;
    artKey = key;
    let url = null;
    if (store.cfg.game_dir) { try { url = await invoke("game_art", { gameDir: store.cfg.game_dir, gameFolders: folders }); } catch { url = null; } }
    if (key !== artKey) return; // the choice changed while it loaded
    hasArt = !!url;
    if (url) { artFront = 1 - artFront; art[artFront].src = url; }
    requestAnimationFrame(paintArt);  // the new picture fades in as the old one fades out
  }
  const play = h("button", { class: "btn primary big", onclick: () => launch(false) }, icon("play"), "Play");
  const hero = h("div", { class: "card hero" }, ...art,
    h("div", { class: "stack tight herotext" }, heroTitle, heroSub, files),
    h("div", { class: "row heroact" }, play, h("span", { class: "muted small" }, "Opens the game's main menu.")));
  const sk = h("button", { class: "btn", onclick: () => launch(true) }, icon("play"), "Start skirmish");
  // Quick Skirmish options, remembered: how many computer opponents (fewer if the map has fewer places) and how hard they are
  const skAi = segmented("Quick Skirmish opponents", [["1", "1"], ["2", "2"], ["3", "3"]], String(Math.min(3, Math.max(1, prefs.get().skAi || 1))), (v) => prefs.set({ skAi: +v }));
  const skDiff = segmented("Quick Skirmish difficulty", [["easy", "Easy"], ["medium", "Medium"], ["brutal", "Brutal"]], ["easy", "medium", "brutal"].includes(prefs.get().skDiff) ? prefs.get().skDiff : "easy", (v) => prefs.set({ skDiff: v }));
  // Quick Skirmish: its own card, with a pointer to the full setup on the Maps page
  const skOptions = h("div", { class: "card skcard" },
    h("div", { class: "stack tight" }, h("h3", {}, "Quick Skirmish"), h("p", { class: "muted" }, "A random match with a random map against the computer, started straight away.")),
    h("div", { class: "skoptions" },
      h("div", { class: "skgroup" }, h("span", { class: "muted small" }, "Opponents"), skAi.el),
      h("div", { class: "skgroup" }, h("span", { class: "muted small" }, "Difficulty"), skDiff.el),
      sk),
    h("p", { class: "tip" }, h("b", {}, "Tip"), h("span", {}, "To pick the map, your general, teams and where everyone starts, set up a skirmish under ", h("button", { class: "linkbtn", onclick: () => go("maps") }, "Maps"), ".")));

  const presets = [[3840, 2160], [2560, 1440], [1920, 1080], [1600, 900], [1280, 720]].filter(([w]) => w <= nat.w);
  resSel.append(new Option(`Native (${nat.w} × ${nat.h})`, "native"), ...presets.filter(([w]) => w !== nat.w).map(([w, hh]) => new Option(`${w} × ${hh}`, `${w}x${hh}`)), new Option("Custom…", "custom"));
  resSel.value = [...resSel.options].some((o) => o.value === p.res) ? p.res : "native";
  const syncRes = () => { custom.hidden = resSel.value !== "custom"; prefs.set({ res: resSel.value }); };
  resSel.addEventListener("change", syncRes); syncRes();
  const size = () => (resSel.value === "native" ? nat : resSel.value === "custom" ? { w: +cw.value, h: +ch.value } : (([w, hh]) => ({ w: +w, h: +hh }))(resSel.value.split("x")));

  function fillMods() {
    const games = gameChoices(), cur = selectedGame();
    game.replaceChildren(...games.map((g) => { const o = new Option(g.installed ? g.name : `${g.name} (not installed)`, g.id); o.disabled = !g.installed; return o; }));
    game.value = cur.id;
    fill(gameNote, cur.note, games.some((g) => !g.installed) ? [" · ", h("button", { class: "linkbtn", onclick: () => go("mods") }, "Get more mods")] : null);
    // what Play will start, at a glance
    heroTitle.textContent = cur.name;
    // the mod folders being played, base first (Chaos plays on Shockwave), for the picture
    const entry = store.mods.catalog.find((m) => m.id === cur.id), baseMod = entry && entry.addon_for ? store.mods.catalog.find((m) => m.id === entry.addon_for) : null;
    loadArt([baseMod && baseMod.path, cur.folder].filter(Boolean));
    hero.classList.toggle("blue", cur.id === GAME_ZH); // Zero Hour is blue; the mods stay red
    const withExtras = extraMods().filter((m) => selectedExtras().includes(m.folder)).map((m) => m.name);
    heroSub.textContent = (cur.id === GAME_ZH ? "Command & Conquer: Generals – Zero Hour" : cur.note) + (withExtras.length ? `, with ${withExtras.join(" and ")}` : "");
    const extras = extraMods();
    extrasField.hidden = !extras.length;
    fill(picks, extras.map((m) => {
      const on = selectedExtras().includes(m.folder);
      const box = h("input", { type: "checkbox", checked: on, onchange: () => toggleExtra(m.folder, box.checked) });
      return h("label", { class: "tile" + (on ? " on" : "") }, box, h("span", { class: "tile-text" }, h("b", {}, m.name), m.mb ? h("span", { class: "muted small" }, `${m.mb} MB`) : null));
    }));
  }
  game.addEventListener("change", () => { prefs.set({ game: game.value }); notifyMods(); });
  refreshers.add(fillMods); fillMods();

  function fillFiles() {
    fill(files,
      store.cfg.game_dir ? h("p", { class: "muted small gamepath" }, store.cfg.game_dir) : h("p", { class: "msg bad" }, "Rallypoint doesn't have your game files yet."),
      store.cfg.game_dir ? null : h("button", { class: "btn", onclick: () => openWizard() }, "Find or download game files"));
  }
  fillFiles();

  async function launch(skirmish) {
    prefs.set({ cw: +cw.value || undefined, ch: +ch.value || undefined, fs: fs.checked }); // a custom size is remembered for launches from elsewhere
    play.disabled = sk.disabled = true;
    const started = await startGame(skirmish ? { skirmish, ai: +skAi.value, difficulty: skDiff.value } : { skirmish });
    setTimeout(() => { play.disabled = sk.disabled = false; }, started ? 5000 : 0);
  }

  const el = h("div", { class: "page" },
    h("div", { class: "page-head" }, h("h1", {}, "Play")),
    hero,
    skOptions,
    h("div", { class: "card" },
      h("h3", {}, "Options"),
      h("div", { class: "grid2" },
        h("label", { class: "field" }, h("span", {}, "Game"), game, gameNote),
        h("label", { class: "field" }, h("span", {}, "Resolution"), resSel)),
      extrasField,
      custom,
      h("div", { class: "settings" },
        setting("Fullscreen", "Fill the screen instead of a window.", h("label", { class: "switch" }, fs)),
        setting("Higher camera", "See further out. Leave it off for a normal online view.", h("label", { class: "switch" }, cam)),
        setting("Stronger AI", aiNote, aiSel)),
      ));
  return { el, dispose: () => refreshers.delete(fillMods) };
}

/** The command bar choice: sharp icons, or Control Bar Pro on top of them (downloaded for the Play page's resolution when
 *  chosen). Shared by the Mods page, where people look for it, and Settings. `fb` says how it went. */
function commandBarSelect(fb) {
  const bar = h("select", { "aria-label": "Command bar", onchange: async () => {
    bar.disabled = true;
    const nat = nativeRes(), r = (prefs.get().res || "native").split("x").map(Number);
    const [w, hh] = r.length === 2 && r[0] > 0 ? r : [nat.w, nat.h];
    fb.busy(bar.value === "pro" ? "Getting Control Bar Pro ready…" : "Saving…");
    try {
      await invoke("set_control_bar", { choice: bar.value, width: w, height: hh });
      store.cfg.control_bar = bar.value;
      await loadMods();
      fb.ok("Saved. It applies the next time you start the game.");
      notifyMods();
    } catch (e) { bar.value = store.cfg.control_bar || "hd"; fb.bad(errText(e)); }
    bar.disabled = false;
  } }, [["hd", "Sharp icons (default)"], ["pro", "Control Bar Pro"]].map(([v, label]) => h("option", { value: v, selected: v === (store.cfg.control_bar || "hd") }, label)));
  return bar;
}

function modsPage() {
  const body = h("div", { class: "stack" });
  const barFb = liveStatus();
  const count = h("p", { class: "muted small" });
  const path = h("input", { type: "text", placeholder: "/path/to/mod.zip", "aria-label": "Path to mod" });
  const dropBusy = h("span", { class: "live busy", hidden: true, role: "status" }, h("span", { class: "spin", "aria-hidden": "true" }), "Installing…");
  const drop = h("div", { class: "drop", id: "dropzone" },
    h("div", { class: "drop-main" },
      h("div", { class: "stack tight" },
        h("b", {}, "Have a mod that is not listed?"),
        h("span", { class: "small" }, "Drop it anywhere on this window, or choose it. A .zip, .7z or .rar, a folder, or an installer .exe."), dropBusy),
      h("div", { class: "row" }, h("button", { class: "btn", onclick: () => choose(null, false) }, "Choose file…"), h("button", { class: "btn ghost", onclick: () => choose(null, true) }, "Choose folder…"))),
    h("details", { class: "help-inline" }, h("summary", {}, "Paste a path instead"),
      h("div", { class: "row pathrow" }, path, h("button", { class: "btn", onclick: () => { if (path.value.trim()) { installPath(path.value.trim()); path.value = ""; } } }, "Install"))),
    h("details", { class: "help-inline" }, h("summary", {}, "It came as an .exe installer"),
      h("p", { class: "muted small" }, "Rallypoint unpacks the game archives (.big and .gib) and never runs a mod's own Windows launcher, so this works on macOS too. Shockwave is one of these."),
      h("p", { class: "muted small" }, "Inno Setup installers need a tool: ", h("code", {}, "brew install innoextract sevenzip"), " on macOS, ", h("code", {}, "sudo apt install innoextract p7zip-full"), " on Linux, or 7-Zip on Windows. Shockwave does not need one. If the mod also offers a plain .zip, take that.")));

  function render() {
    dropBusy.hidden = !(store.installing && store.installing.id === "*");
    const have = modGroups(true), other = store.mods.other, more = modGroups(false);
    const installed = store.mods.catalog.filter((m) => m.installed).length + other.filter((o) => o.folder !== PRO_FOLDER).length;
    count.textContent = installed ? `${installed} installed. Pick what to play on the Play page.` : "Nothing installed yet. Zero Hour plays fine without any.";
    fill(body,
      have.length || other.length ? h("h2", { class: "modhead" }, "Installed") : null,
      have,
      other.map((o) => {
        if (o.folder === PRO_FOLDER) return null; // chosen on the Control Bar HD card below, not a separate mod
        const bundled = isBundled(o.folder);
        return h("div", { class: "card modcard" }, h("div", { class: "mod" },
          modTile(o.folder, "on"),
          h("div", { class: "stack tight" },
            h("div", { class: "row" }, h("h3", {}, o.folder), bundled ? pill("Comes with Rallypoint") : null),
            h("p", { class: "muted small" }, bundled ? "Sharp command-bar icons, loaded with every game. Control Bar Pro redraws the bar and menus for big screens. It is drawn for the Play page's resolution, so pick it again after changing that." : `${o.bigs} game file${o.bigs === 1 ? "" : "s"}, ${o.mb} MB`),
            bundled ? barFb.el : null),
          h("div", { class: "row end" }, bundled ? h("label", { class: "inline" }, h("span", { class: "muted small" }, "Command bar"), commandBarSelect(barFb)) : removeButton(o.folder, o.folder))));
      }),
      more.length ? h("h2", { class: "modhead" }, have.length || other.length ? "More to install" : "Mods you can install") : null,
      more);
  }
  refreshers.add(render); render();
  const el = h("div", { class: "page" },
    h("div", { class: "page-head" }, h("div", { class: "stack tight" }, h("h1", {}, "Mods"), count), h("button", { class: "btn small", onclick: () => invoke("open_mods_folder").catch((e) => say(errText(e), "bad")) }, "Open mods folder")),
    body, drop);
  return { el, dispose: () => { refreshers.delete(render); } };
}

const DEFAULT_PORT = "7777";
/** "host:port" (as stored) into the two boxes; a bare host gets the default port. */
function splitServer(s) {
  const m = (s || "").trim().match(/^(.*):(\d{1,5})$/);
  return m ? { host: m[1], port: m[2] } : { host: (s || "").trim(), port: DEFAULT_PORT };
}
/** The two boxes back into "host:port". A port typed into the host box wins, so "play.example:7788" just works. */
function joinServer(host, port) {
  const typed = host.trim().match(/^(.*):(\d{1,5})$/);
  const h = typed ? typed[1] : host.trim();
  const p = (typed ? typed[2] : port.trim()) || DEFAULT_PORT;
  const ok = h.length > 0 && /^\d{1,5}$/.test(p) && +p >= 1 && +p <= 65535;
  return { ok, text: `${h}:${p}`, host: h, port: p };
}

/** Saved servers live in this launcher's own storage (never the room password). */
function loadServers() {
  try { const v = JSON.parse(localStorage.getItem("servers") || "[]"); return Array.isArray(v) ? v.filter((x) => x && typeof x.server === "string") : []; }
  catch { return []; }
}
function storeServers(list) { try { localStorage.setItem("servers", JSON.stringify(list)); } catch { /* storage unavailable */ } }

// Right-click does nothing in the launcher.
document.addEventListener("contextmenu", (e) => e.preventDefault());

const dur = (secs) => {
  const d = Math.floor(secs / 86400), hr = Math.floor((secs % 86400) / 3600), m = Math.floor((secs % 3600) / 60);
  return d ? `${d}d ${hr}h` : hr ? `${hr}h ${m}m` : `${m}m ${secs % 60}s`;
};
const rate = (kbps) => (kbps >= 1000 ? `${(kbps / 1000).toFixed(1)} Mbit/s` : `${kbps} kbit/s`);

/** What each refusal means and what to do about it, keyed by the status text the launcher shows. */
const WHY = {
  "Wrong room password": "The room already exists with a different password. A room is created by the first player in and removed when everyone has left, so if nobody else is in it, pick a new password or another room name.",
  "That room is full": "A room holds 8 players. Join another room.",
  "Room names use letters, numbers, spaces, - _ . (max 32)": "Use only letters, numbers, spaces and - _ . in the room name, up to 32 characters.",
  "This server's identity changed": "Rallypoint remembers each server's identity from your first visit. A different one is answering now. That is expected if the server's owner reinstalled it. If not, someone may be pretending to be that server.",
  "Too many attempts, try again in a few minutes": "The server limits sign-in attempts from one address. Wait a few minutes before trying again.",
  "Server is full": "The server has no space for another room right now.",
  "This server is private": "This server only accepts players its owner has signed up. Ask them for access.",
  "This computer can't join right now": "The server doesn't accept this computer's saved identity. If you were signed up on this server before, ask its owner whether your access was removed.",
  "Rejected by server": "The server refused the sign-in without a specific reason.",
};

/** How the connection and the server are doing, refreshed while the Online page is open and we are connected. */
// The connection's history for the graphs: a sample every 2 s while connected, kept while the app is open.
const net = { hist: [], last: null };
async function pollNet() {
  let d = null;
  try { d = await invoke("relay_diagnostics"); } catch { /* not connected */ }
  if (!d) net.hist = [];
  else {
    net.hist.push({ rtt: d.rtt_ms, loss: d.loss_pct, up: d.up_kbps, down: d.down_kbps, load: d.server ? d.server.load_pct : null });
    if (net.hist.length > 120) net.hist.shift();
  }
  net.last = d;
  window.dispatchEvent(new Event("net"));
}

/** A small time-series chart on a canvas: filled areas or bars, a faint grid and nothing else to read. Colours come from the theme. */
function drawChart(canvas, series, { floor = 1, max = null, bars = false } = {}) {
  const w = canvas.clientWidth, hgt = canvas.clientHeight;
  if (!w || !hgt) return 0;
  const dpr = window.devicePixelRatio || 1;
  if (canvas.width !== Math.round(w * dpr)) { canvas.width = Math.round(w * dpr); canvas.height = Math.round(hgt * dpr); }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, hgt);
  const css = (n) => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
  const vals = series.flatMap((s) => s.values.filter((v) => v != null));
  const top = max ?? Math.max(floor, Math.ceil((Math.max(0, ...vals) * 1.15) / floor) * floor);
  const pad = 3, N = Math.max(40, ...series.map((q) => q.values.length)), step = (w - 2) / (N - 1);
  const x = (i, n) => w - 1 - (n - 1 - i) * step;
  const y = (v) => hgt - pad - (Math.min(v, top) / top) * (hgt - pad * 2);
  ctx.lineWidth = 1; ctx.strokeStyle = css("--line");
  for (const f of [0, .5, 1]) { const gy = Math.round(hgt - pad - f * (hgt - pad * 2)) + .5; ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(w, gy); ctx.stroke(); }
  for (const s of series) {
    const n = s.values.length;
    if (!n) continue;
    const color = css(s.color);
    if (bars) {
      ctx.fillStyle = color;
      s.values.forEach((v, i) => { if (v) { const bh = Math.max(2, hgt - pad - y(v)); ctx.fillRect(x(i, n) - step / 2, hgt - pad - bh, Math.max(2, step - 1), bh); } });
      continue;
    }
    ctx.beginPath();
    let started = false, last = null;
    s.values.forEach((v, i) => { if (v == null) return; if (!started) { ctx.moveTo(x(i, n), y(v)); started = true; } else ctx.lineTo(x(i, n), y(v)); last = [x(i, n), y(v)]; });
    ctx.lineWidth = 2; ctx.lineJoin = "round"; ctx.strokeStyle = color;
    if (s.dash) ctx.setLineDash([5, 4]);
    ctx.stroke(); ctx.setLineDash([]);
    if (s.fill && last) {
      ctx.lineTo(last[0], hgt - pad); ctx.lineTo(x(s.values.findIndex((v) => v != null), n), hgt - pad); ctx.closePath();
      ctx.globalAlpha = .16; ctx.fillStyle = color; ctx.fill(); ctx.globalAlpha = 1;
    }
    if (last) { ctx.beginPath(); ctx.arc(last[0] - 1.5, last[1], 3, 0, Math.PI * 2); ctx.fillStyle = color; ctx.fill(); }
  }
  return top;
}

function connectionCard() {
  const body = h("dl", { class: "diag" });
  const verdict = h("span", {});
  const row = (label, value, level) => [h("dt", {}, label), h("dd", { class: level || "" }, value)];
  // four graphs, built once and redrawn on every sample
  const mk = (label, extra) => {
    const value = h("span", { class: "stat-value" }), sub = h("span", { class: "muted small" }), canvas = h("canvas", { class: "chart", role: "img", "aria-label": `${label} over the last four minutes` });
    const scale = h("span", { class: "muted small chart-scale" });
    const el = h("div", { class: "stat" }, h("div", { class: "stat-head" }, h("span", { class: "stat-label" }, label), value), canvas, h("div", { class: "stat-foot" }, sub, scale), extra || null);
    return { el, value, sub, canvas, scale, set(text, level) { value.textContent = text; el.className = "stat " + (level || ""); } };
  };
  const g = { lat: mk("Latency"), loss: mk("Packet loss"), traffic: mk("Game traffic"), load: mk("Server load") };
  const charts = h("div", { class: "stats" }, g.lat.el, g.loss.el, g.traffic.el, g.load.el);
  const card = h("div", { class: "card", hidden: true }, h("div", { class: "row between" }, h("h2", {}, "Connection"), verdict), charts, body);
  function draw() {
    const d = net.last;
    card.hidden = !d;
    if (!d) return;
    const hist = net.hist, s = d.server;
    const lat = d.rtt_ms == null ? null : d.avg_ms <= 60 ? "ok" : d.avg_ms <= 120 ? "warn" : "bad";
    const loss = d.loss_pct == null ? null : d.loss_pct === 0 ? "ok" : d.loss_pct < 3 ? "warn" : "bad";
    const load = s ? (s.load_pct < 70 ? "ok" : s.load_pct < 90 ? "warn" : "bad") : null;
    const worst = [lat, loss, load].includes("bad") ? "bad" : [lat, loss, load].includes("warn") ? "warn" : lat ? "ok" : null;
    const why = [lat === "bad" && `latency ${d.avg_ms} ms`, loss === "bad" && `${d.loss_pct}% packet loss`, load === "bad" && `server ${s.load_pct}% full`].filter(Boolean).join(", ");
    verdict.className = "pill " + (worst === "ok" ? "ok" : worst === "bad" ? "bad" : worst === "warn" ? "warn" : "");
    verdict.textContent = worst === "ok" ? "Good" : worst === "warn" ? "Fair" : worst === "bad" ? `Poor: ${why}` : "Measuring…";
    g.lat.set(d.rtt_ms == null ? "…" : `${d.rtt_ms} ms`, lat);
    g.lat.sub.textContent = d.rtt_ms == null ? "" : `average ${d.avg_ms} ms, jitter ${d.jitter_ms ?? 0} ms`;
    g.loss.set(d.loss_pct == null ? "…" : `${d.loss_pct}%`, loss);
    g.loss.sub.textContent = "of the last pings got no answer";
    g.traffic.set(rate(d.up_kbps + d.down_kbps));
    g.traffic.sub.textContent = `${rate(d.down_kbps)} down, ${rate(d.up_kbps)} up`;
    g.load.set(s ? `${s.load_pct}%` : "…", load);
    g.load.sub.textContent = s ? `${s.pps} packets/s, ${rate(s.kbps)}` : "waiting for the server";
    const col = { ok: "--ok-ink", warn: "--warn-ink", bad: "--bad-ink" };
    const t1 = drawChart(g.lat.canvas, [{ values: hist.map((p) => p.rtt), color: col[lat] || "--rose", fill: true }], { floor: 50 });
    const t2 = drawChart(g.loss.canvas, [{ values: hist.map((p) => p.loss), color: col[loss] || "--rose" }], { floor: 5, bars: true });
    const t3 = drawChart(g.traffic.canvas, [{ values: hist.map((p) => p.down), color: "--rose", fill: true }, { values: hist.map((p) => p.up), color: "--text", dash: true }], { floor: 20 });
    const t4 = drawChart(g.load.canvas, [{ values: hist.map((p) => p.load), color: col[load] || "--rose", fill: true }], { max: 100 });
    g.lat.scale.textContent = t1 ? `0 to ${t1} ms` : "";
    g.loss.scale.textContent = t2 ? `0 to ${t2}%` : "";
    g.traffic.scale.textContent = t3 ? `0 to ${rate(t3)}, dashed is up` : "";
    g.load.scale.textContent = t4 ? "0 to 100%" : "";
    fill(body,
      d.desync ? row("Sync check", d.desync.certain
        ? `Out of sync at frame ${d.desync.frame}: ${d.desync.players.filter((p) => p.odd).map((p) => p.vip).join(", ")} differ from ${d.desync.host ? "the host" : "the others"}`
        : `Out of sync at frame ${d.desync.frame}: the players disagree and the server cannot tell who is right`, "bad") : row("Sync check", "all players agree so far", "ok"),
      d.server_identity ? row("Encryption", `on, server identity ${d.server_identity}${d.identity_saved ? " (checked against the saved one)" : " (saved now)"}`, "ok") : null,
      s ? [
        row("Server", `Rallypoint ${s.version}, up ${dur(s.uptime_secs)}`),
        row("Rooms in use", `${s.rooms} of ${s.max_rooms}`),
        row("Players online", `${s.players} of ${s.max_players}`),
        row("In your room", String(s.room_players)),
      ] : row("Server", "waiting for the server's numbers…"));
  }
  window.addEventListener("net", draw);
  window.addEventListener("resize", draw);
  draw();
  return { el: card, dispose: () => { window.removeEventListener("net", draw); window.removeEventListener("resize", draw); } };
}

/** The room's settings. The first player into a room is its master and picks the "Stronger AI" level every player's game
 *  starts with (for computer players in a multiplayer game); everyone else sees it and cannot change it. */
function roomCard() {
  const badge = h("span", {}), note = h("span", {}), fb = liveStatus(), level = h("span", { class: "pill" });
  const sel = h("select", { "aria-label": "Stronger AI for the room", onchange: () => { fb.busy("Saving…"); setAiBoost(+sel.value); } },
    ...AI_BOOSTS.map(([v, t]) => h("option", { value: String(v) }, t)));
  const card = h("div", { class: "card", hidden: true }, h("div", { class: "row between" }, h("h2", {}, "Room"), badge),
    h("div", { class: "settings" }, setting("Stronger AI", note, h("div", { class: "ctl-inline" }, fb.el, sel, level))));
  function draw() {
    const d = net.last;
    card.hidden = !d || d.room_master == null;
    if (card.hidden) return;
    const master = !!d.room_master;
    fill(badge, pill(master ? "You are the room master" : "Room master: another player", master ? "ok" : ""));
    // the relay has confirmed the level that was asked for, or five seconds passed without it
    if (aiAsked != null && (d.ai_level ?? 0) === aiAsked) { aiAsked = null; fb.ok("Saved for the room"); }
    else if (aiAsked != null && Date.now() - aiHeld > 5000) { aiAsked = null; fb.bad("Not confirmed, try again"); }
    sel.value = String(aiBoost());
    // only the master has a control; everyone else sees the level as a value, not a greyed-out menu
    sel.hidden = !master; level.hidden = master;
    level.textContent = (AI_BOOSTS.find(([v]) => v === (d.ai_level ?? 0)) || AI_BOOSTS[0])[1];
    note.textContent = master ? "Want to change how the AI plays for everyone? You're the room master, so this applies to the whole room." : "The room master can set this if they choose to.";
  }
  window.addEventListener("net", draw);
  draw();
  return { el: card, dispose: () => window.removeEventListener("net", draw) };
}

function onlinePage() {
  const conn = connectionCard();
  const ra2 = isRa2();
  const roomSettings = ra2 ? ra2LobbyCard() : roomCard();
  const saved = splitServer(store.cfg.server);
  const server = h("input", { type: "text", placeholder: "play.example.com", value: saved.host, "aria-label": "Server address" });
  const port = h("input", { type: "text", inputmode: "numeric", placeholder: DEFAULT_PORT, value: saved.port, "aria-label": "Server port" });
  const room = h("input", { type: "text", placeholder: "any name", maxlength: ra2 ? 28 : 32, value: store.cfg.room || "", "aria-label": "Room" });
  const pw = h("input", { type: "password", autocomplete: "off", "aria-label": "Room password" });
  const remember = h("input", { type: "checkbox", checked: true });
  const nameBox = h("input", { type: "text", maxlength: 12, placeholder: store.cfg.default_player_name || "Player", value: store.cfg.player_name || "", "aria-label": "Player name" });
  const nameNote = h("span", { class: "muted small" });
  const showName = (used) => { nameNote.textContent = `Other players see ${used} in the game's lobby.`; };
  showName(store.cfg.player_name || store.cfg.default_player_name || "your computer's name");
  nameBox.addEventListener("change", async () => {
    try {
      const used = await invoke("set_player_name", { name: nameBox.value });
      store.cfg.player_name = nameBox.value.trim() === "" ? "" : used;
      nameBox.value = store.cfg.player_name;
      showName(used);
    } catch (e) { say(errText(e), "bad"); }
  });
  const state = h("div", { class: "row" });
  const problem = h("div", { class: "notice", role: "status", hidden: true });
  const saved$ = h("div", { class: "card", hidden: true });
  let lastError = "";
  const key = (x) => ({ server: x.server, room: x.room || "" });
  async function drawServers() {
    const list = loadServers();
    saved$.hidden = !list.length;
    let flags = [];
    try { flags = await invoke("room_passwords_saved", { pairs: list.map((x) => [x.server, x.room || ""]) }); } catch { /* none known */ }
    fill(saved$, h("h2", {}, "Saved servers"), h("div", { class: "rows" }, list.map((x, i) => {
      const sp = splitServer(x.server);
      const has = !!flags[i];
      const use = () => { server.value = sp.host; port.value = sp.port; room.value = x.room || ""; pw.value = ""; pw.placeholder = has ? "Saved password" : ""; if (!has) pw.focus(); };
      return h("div", { class: "rowline" },
        h("div", { class: "stack tight" }, h("strong", {}, x.server), h("span", { class: "muted small" }, x.room ? `Room ${x.room}` : "No room name")),
        h("div", { class: "row" }, has ? pill("Password saved", "ok") : null,
          h("button", { class: "btn small", onclick: () => { use(); if (has) btn.click(); } }, has ? "Connect" : "Use"),
          h("button", { class: "btn small ghost danger", onclick: async () => {
            const l = loadServers(); l.splice(i, 1); storeServers(l);
            try { await invoke("room_password_save", { ...key(x), password: "" }); } catch { /* nothing saved */ }
            drawServers();
          } }, "Remove")));
    })));
  }
  const save = h("button", { class: "btn", onclick: async () => {
    const s = joinServer(server.value, port.value);
    if (!s.ok) return say(s.host ? "The port must be a number from 1 to 65535" : "Enter the server address first", "bad");
    const r = room.value.trim();
    const l = loadServers().filter((x) => !(x.server === s.text && (x.room || "") === r));
    l.push({ server: s.text, room: r });
    storeServers(l);
    let withPw = false;
    try {
      if (remember.checked && pw.value) { await invoke("room_password_save", { server: s.text, room: r, password: pw.value }); withPw = true; }
      else if (!remember.checked) await invoke("room_password_save", { server: s.text, room: r, password: "" });
      else withPw = (await invoke("room_passwords_saved", { pairs: [[s.text, r]] }))[0];
    } catch (e) { return say("Saved the server, but not the password: " + errText(e), "bad"); }
    await drawServers();
    say(withPw ? "Server and password saved" : "Server saved", "ok");
  } }, "Save server");
  drawServers();
  const btn = h("button", { class: "btn primary", onclick: async () => {
    try {
      lastError = "";
      if (btn.dataset.mode === "disconnect") await invoke("relay_disconnect");
      else {
        const s = joinServer(server.value, port.value);
        if (!s.ok) throw new Error(s.host ? "The port must be a number from 1 to 65535" : "Enter the server address first");
        server.value = s.host; port.value = s.port;
        await invoke("save_config", { server: s.text, room: room.value, steamUser: store.cfg.steam_user || "" });
        store.cfg.server = s.text; store.cfg.room = room.value.trim();
        await invoke("relay_connect", { password: pw.value });
      }
    } catch (e) { lastError = errText(e); say(lastError, "bad"); }
    refresh();
  } }, "Connect");
  const nextText = h("span", {});
  const next = h("div", { class: "nextstep", role: "status", hidden: true }, h("div", { class: "stack tight" }, h("strong", {}, "You are connected"), h("span", { class: "muted small" }, nextText)),
    ra2 ? null : h("button", { class: "btn primary", onclick: () => startGame() }, icon("play"), "Start the game"));
  async function refresh() {
    const s = store.relay;
    const connected = s.startsWith("Connected as");
    const on = s === "Connecting..." || connected;
    btn.textContent = on ? "Disconnect" : "Connect";
    btn.dataset.mode = on ? "disconnect" : "connect";
    // Connected, the next thing to do is start the game: that becomes the one primary action and the form is locked,
    // because typing in it changes nothing until you disconnect.
    btn.className = on ? "btn" : "btn primary";
    for (const f of [server, port, room, pw, remember]) f.disabled = on;
    // RA2 shows its own lobby card under this form; Generals still needs the next-step hint.
    next.hidden = !connected || ra2;
    if (connected && !ra2) nextText.textContent = `${store.cfg.room ? `You are in room ${store.cfg.room}` : "You are in the server's open room"}${store.cfg.server ? ` on ${store.cfg.server}` : ""}. Start the game and choose Multiplayer: everyone in this room is in the lobby.`;
    state.replaceChildren(pill(s === "Connecting..." ? "Connecting…" : s, connected ? "ok" : on ? "warn" : s === "Offline" ? "" : "bad"));
    // Why it is not working, in the words of what to check.
    let c = null;
    if (s === "Connecting...") { try { c = await invoke("relay_connection"); } catch { /* none */ } }
    const lines = [];
    if (lastError) lines.push(h("p", {}, lastError));
    if (WHY[s]) lines.push(h("p", {}, h("b", {}, s + ". "), WHY[s]));
    if (c && c.hint) lines.push(h("p", {}, c.hint));
    if (c && !c.hint && c.waited_secs >= 3) lines.push(h("p", { class: "muted" }, `${c.stage[0].toUpperCase() + c.stage.slice(1)}… (${c.waited_secs} s, server at ${c.address})`));
    if (c && c.reconnects) lines.push(h("p", { class: "muted" }, `The server went quiet and Rallypoint has rejoined ${c.reconnects} time${c.reconnects === 1 ? "" : "s"}.`));
    if (connected) {
      let cc = null; try { cc = await invoke("relay_connection"); } catch { /* none */ }
      if (cc && cc.address_changed) lines.push(h("p", {}, h("b", {}, "Your address in the room changed. "), "A game that is already running still uses the old one. Close it and start it again so other players can see you."));
      else if (cc && cc.reconnects) lines.push(h("p", { class: "muted" }, `Reconnected ${cc.reconnects} time${cc.reconnects === 1 ? "" : "s"} after the server went quiet. If the lobby looks empty, close the game and start it again.`));
    }
    if (s === "This server's identity changed") {
      lines.push(h("div", { class: "row" }, h("button", { class: "btn small", onclick: async () => {
        try { await invoke("relay_forget_key"); await invoke("relay_disconnect"); say("Forgotten. Press Connect to trust the server as it is now.", "ok"); } catch (e) { say(errText(e), "bad"); }
        refresh();
      } }, "Trust its new identity")));
    }
    problem.hidden = !lines.length;
    problem.className = "notice" + (connected ? "" : s === "Connecting..." ? " warn" : " bad");
    fill(problem, lines);
  }
  refreshers.add(refresh); refresh();
  // Enter in any of the four fields connects (not while connected: Disconnect is a deliberate click)
  for (const f of [server, port, room, pw]) {
    f.addEventListener("keydown", (e) => { if (e.key === "Enter" && !e.isComposing && btn.dataset.mode !== "disconnect" && !btn.disabled) { e.preventDefault(); btn.click(); } });
  }
  const el = h("div", { class: "page" },
    h("div", { class: "page-head" }, h("h1", {}, "Online")),
    h("div", { class: "card" },
      h("div", { class: "row between" }, h("h2", {}, "Join a room"), state),
      h("div", { class: "hostport" }, h("label", { class: "field" }, h("span", {}, "Server"), server), h("label", { class: "field" }, h("span", {}, "Port"), port)),
      h("div", { class: "grid2" }, h("label", { class: "field" }, h("span", {}, "Room"), room), h("label", { class: "field" }, h("span", {}, "Room password"), pw)),
      h("div", { class: "row between" }, h("label", { class: "switch" }, remember, "Remember the password"), h("div", { class: "row" }, save, btn)),
      next,
      problem,
      h("details", { class: "help-inline" }, h("summary", {}, "How rooms work"),
        h("p", { class: "muted small" }, ra2 ? "Everyone who joins the same room with the same password lands in the same lobby, and the first one in hosts. Red Alert 2 rooms are separate from Generals rooms of the same name." : "Join a room on a Rallypoint server, then use the game's LAN lobby: everyone in the same room with the same password ends up in the same game. A room that doesn't exist yet is created with your password and disappears when everyone has left. A saved password stays in Rallypoint's settings file on this computer, readable only by you."))),
    roomSettings.el,
    saved$,
    conn.el,
    h("div", { class: "card" }, h("h2", {}, "Your name"), h("div", { class: "settings" }, setting("Name in the lobby", nameNote, nameBox))));
  return { el, dispose: () => { refreshers.delete(refresh); conn.dispose(); roomSettings.dispose(); } };
}

function diagnosePanel() {
  const out = h("div", {});
  const fb = liveStatus();
  const run = h("button", { class: "btn", onclick: async () => {
    run.disabled = true;
    fb.busy("Checking…");
    try {
      const r = await invoke("diagnose", { gameDir: store.cfg.game_dir || "" });
      fill(out,
        r.checks.map((c) => h("div", { class: "mappack" }, h("div", {}, h("strong", {}, c.name), h("div", { class: "muted small" }, c.detail)), pill(c.ok ? "OK" : "Problem", c.ok ? "ok" : "bad"))),
        r.crash ? [h("p", { class: "muted small" }, "The game's last crash report (ReleaseCrashInfo.txt):"), h("pre", { class: "crash" }, r.crash)] : h("p", { class: "muted small" }, "The game has not written a crash report."),
        r.engine_log ? [h("p", { class: "muted small" }, "What the game printed on its last run:"), h("pre", { class: "crash" }, r.engine_log)] : null,
        h("div", { class: "row" }, h("button", { class: "btn small", onclick: () => invoke("open_game_data_folder").catch((e) => fb.bad(errText(e))) }, "Open the game's data folder")));
      const bad = r.checks.filter((c) => !c.ok).length;
      fb.set(bad ? `${bad} problem${bad === 1 ? "" : "s"} found` : "No problems found", bad ? "bad" : "ok");
    } catch (e) { fb.bad(errText(e)); }
    run.disabled = false;
  } }, "Check my install");
  return h("div", { class: "stack" }, setting("Check my install", h("span", {}, "If the game stops with \u201cYou have encountered a serious error\u201d, this looks for missing game files and a missing original Generals. ", fb.el), run), out);
}

/** The Enhanced renderer's effects: a switch for each on/off one, a menu for the rest. Saved one by one; they apply at the next start. */
const EFFECT_TEXT = {
  PostProcessing: ["Post processing", "Bloom, ambient occlusion and anti-aliasing. Off turns all three off."],
  Bloom: ["Bloom", "Bright lights and explosions glow."],
  AmbientOcclusion: ["Ambient occlusion", "Soft shade where things meet the ground."],
  AmbientOcclusionQuality: ["Ambient occlusion quality", "Full resolution is sharper and slower.", { half: "Half resolution", full: "Full resolution" }],
  AntiAliasingFXAA: ["Anti-aliasing (FXAA)", "Smooths jagged edges."],
  SoftShadows: ["Soft shadows", "Shadow edges soften with distance."],
  ShadowQuality: ["Shadow quality", "Sharper shadows cost more graphics memory.", { low: "Low", medium: "Medium", high: "High" }],
  DynamicLights: ["Dynamic lights", "Explosions and muzzle flashes light up what is around them."],
  PixelLighting: ["Per-pixel lighting", "Units and buildings get a soft sun highlight."],
  AnisotropicFiltering: ["Texture filtering", "Ground textures stay sharp at a low angle.", { 0: "Off", 4: "4x", 8: "8x", 16: "16x" }],
  CloudShadows: ["Cloud shadows", "Moving cloud shadows on the ground."],
  Clouds: ["Clouds", "Visible clouds when you zoom far out."],
  UpdatedWater: ["Updated water", "Waves, depth colour and foam."],
  WaterReflections: ["Water reflections", "The scene mirrored in the water."],
  Shockwaves: ["Explosion shockwaves", "A heat ripple rings out from big explosions."],
  ScorchRelief: ["Scorch craters", "Scorch marks are lit as shallow craters."],
  BloomStrength: ["Glow strength", "How much bright lights and explosions glow. Needs Bloom.", { subtle: "Subtle", normal: "Normal", strong: "Strong" }],
  ColorLook: ["Colour look", "Vivid has more colour and contrast. Cinematic has cool shadows and warm highlights.", { standard: "Standard", vivid: "Vivid", cinematic: "Cinematic" }],
  Sharpen: ["Sharpening", "A crisper picture after the anti-aliasing.", { off: "Off", low: "Low", high: "High" }],
  Vignette: ["Vignette", "The corners of the battlefield darken a little. Menus and the control bar stay as they are."],
  SoftParticles: ["Soft particles", "Smoke, dust and fire fade where they meet the ground instead of cutting a hard line."],
  SmokeShading: ["Smoke shading", "Smoke and dust are darker near the ground and lighter on top, so a column looks like it has depth."],
};
const EFFECT_PRESETS = [["performance", "Performance"], ["balanced", "Balanced (default)"], ["cinematic", "Cinematic"]];
const betaTag = () => h("span", { class: "pill warn beta" }, "Beta");
/** How the effects are laid out: what they change, not the order the engine lists them in. */
const EFFECT_GROUPS = [
  ["Picture", ["PostProcessing", "AntiAliasingFXAA", "Sharpen", "ColorLook", "Vignette"]],
  ["Light and glow", ["Bloom", "BloomStrength", "DynamicLights", "PixelLighting", "AmbientOcclusion", "AmbientOcclusionQuality"]],
  ["Shadows and sky", ["SoftShadows", "ShadowQuality", "CloudShadows", "Clouds"]],
  ["Smoke and explosions", ["SoftParticles", "SmokeShading", "Shockwaves", "ScorchRelief"]],
  ["Water and ground", ["UpdatedWater", "WaterReflections", "AnisotropicFiltering"]],
];
/** An effect that does nothing while another one is off (so its row is dimmed until that one is on). */
const EFFECT_NEEDS = {
  Bloom: "PostProcessing", AmbientOcclusion: "PostProcessing", AntiAliasingFXAA: "PostProcessing", Sharpen: "PostProcessing", ColorLook: "PostProcessing",
  Vignette: "PostProcessing", DynamicLights: "PostProcessing", BloomStrength: "Bloom", AmbientOcclusionQuality: "AmbientOcclusion",
  ShadowQuality: "SoftShadows", WaterReflections: "UpdatedWater", SmokeShading: "SoftParticles",
};
/** The Enhanced renderer's effects: a preset, then every effect by what it changes. Saved one by one; they apply at the next start. */
function effectsPanel(fb) {
  const groups = h("div", { class: "fxgroups" });
  const value = (key) => ((store.cfg.effects || []).find((e) => e[0] === key) || [])[1];
  const isOn = (key) => { for (let k = key; k; k = EFFECT_NEEDS[k]) if (k !== key && value(k) === "no") return false; return true; };
  const saved = () => fb.ok("Saved. It applies the next time you start the game.");
  const presetHost = h("div", {});
  const drawPreset = () => {
    const cur = store.cfg.effect_preset || "balanced";
    const seg = segmented("Effects preset", cur === "custom" ? [...EFFECT_PRESETS, ["custom", "Custom"]] : EFFECT_PRESETS, cur, async (name) => {
      if (name === "custom") return;
      try {
        store.cfg.effects = await invoke("set_effect_preset", { name });
        store.cfg.effect_preset = name;
        draw();
        saved();
      } catch (e) { fb.bad(errText(e)); }
    });
    fill(presetHost, seg.el);
  };
  const save = async (key, v) => {
    try {
      store.cfg.effect_preset = await invoke("set_effect", { key, value: v });
      const row = (store.cfg.effects || []).find((e) => e[0] === key);
      if (row) row[1] = v;
      draw();
      saved();
    } catch (e) { fb.bad(errText(e)); }
  };
  const row = (key) => {
    const [title, desc, choices] = EFFECT_TEXT[key] || [key, null];
    const v = value(key), live = isOn(key);
    let ctl;
    if (!choices) {
      const box = h("input", { type: "checkbox", "aria-label": title, checked: v === "yes", disabled: !live, onchange: () => save(key, box.checked ? "yes" : "no") });
      ctl = h("label", { class: "switch" }, box);
    } else {
      const sel = h("select", { "aria-label": title, disabled: !live, onchange: () => save(key, sel.value) },
        Object.entries(choices).map(([val, label]) => h("option", { value: val, selected: val === v }, label)));
      ctl = sel;
    }
    const el = setting(title, live ? desc : `Needs ${(EFFECT_TEXT[EFFECT_NEEDS[key]] || [EFFECT_NEEDS[key]])[0].toLowerCase()}.`, ctl);
    if (!live) el.classList.add("off");
    return el;
  };
  const draw = () => {
    drawPreset();
    const have = (store.cfg.effects || []).map((e) => e[0]);
    const grouped = new Set(EFFECT_GROUPS.flatMap((g) => g[1]));
    const rest = have.filter((k) => !grouped.has(k));
    fill(groups, [...EFFECT_GROUPS, ...(rest.length ? [["More", rest]] : [])].map(([name, keys]) => {
      const mine = keys.filter((k) => have.includes(k));
      return mine.length ? h("section", { class: "fxgroup" }, h("h3", {}, name), h("div", { class: "settings" }, mine.map(row))) : null;
    }));
  };
  draw();
  return h("div", { class: "card" },
    h("div", { class: "row between" }, h("h2", {}, "Enhanced effects ", betaTag()), presetHost),
    h("p", { class: "muted small" }, "Performance drops the costly effects. Cinematic adds a stronger glow, cool shadows, sharpening and a vignette."),
    groups);
}

/** Settings card for the graphics fix: how far the camera zooms out, and the high-quality options. */
function graphicsCards() {
  const fb = liveStatus();
  const choices = [[300, "Standard (original game)"], [510, "Far (recommended)"], [1000, "Very far"]];
  const cur = store.cfg.camera_height || 510;
  const camera = h("select", { "aria-label": "Camera distance", onchange: async () => {
    try {
      await invoke("set_camera_height", { height: +camera.value });
      store.cfg.camera_height = +camera.value;
      fb.ok("Saved. It applies the next time you start the game.");
    } catch (e) { fb.bad(errText(e)); }
  } }, choices.map(([v, label]) => h("option", { value: String(v), selected: v === cur }, label)));
  const apply = h("button", { class: "btn", onclick: async () => {
    apply.disabled = true;
    fb.busy("Applying…");
    try { await invoke("graphics_apply_high"); fb.ok("Applied. Your original Options.ini is kept as Options.ini.rallypoint-backup."); } catch (e) { fb.bad(errText(e)); }
    apply.disabled = false;
  } }, "Apply");
  const arch = h("select", { "aria-label": "Game engine", onchange: async () => {
    try {
      await invoke("set_engine_arch", { arch: arch.value });
      store.cfg.engine_arch = arch.value;
      fb.ok("Saved. It applies the next time you start the game.");
    } catch (e) { fb.bad(errText(e)); }
  } }, [["x64", "64-bit (default)"], ["x86", "32-bit (the older build)"]].map(([v, label]) => h("option", { value: v, selected: v === (store.cfg.engine_arch || "x64") }, label)));
  const bar = commandBarSelect(fb);

  // Renderer: two tiles to choose between, each saying what you get.
  const mac = store.cfg.os === "macos";
  const effectsFb = liveStatus();
  const effects = effectsPanel(effectsFb);
  effects.append(effectsFb.el);
  const tiles = [
    ["classic", "Classic", null, "The game as it has always looked. The safe choice."],
    ["enhanced", "Enhanced", betaTag(), mac
      ? "Bloom, ambient occlusion, light from explosions, shockwaves, shaded smoke and scorch craters."
      : "Vulkan, with real shadows, new water, bloom, shockwaves and shaded smoke. Needs the 64-bit engine and a Vulkan 1.3 driver, else Classic is used."],
  ].map(([value, name, tag, desc]) => h("button", { class: "choice", type: "button", role: "radio", "aria-checked": String(value === (store.cfg.renderer || "classic")), onclick: async () => {
    try {
      await invoke("set_renderer", { renderer: value });
      store.cfg.renderer = value;
      tiles.forEach((t, i) => t.setAttribute("aria-checked", String(["classic", "enhanced"][i] === value)));
      effects.hidden = value !== "enhanced";
      fb.ok("Saved. It applies the next time you start the game.");
    } catch (e) { fb.bad(errText(e)); }
  } }, h("span", { class: "choice-name" }, name, tag), h("span", { class: "muted small" }, desc)));
  effects.hidden = (store.cfg.renderer || "classic") !== "enhanced";
  const renderer = store.cfg.renderer_available ? h("div", { class: "card" },
    h("h2", {}, "Renderer"),
    h("div", { class: "choices", role: "radiogroup", "aria-label": "Renderer" }, tiles),
    h("p", { class: "muted small" }, "Either way only the picture changes, so you can play online with anyone.")) : null;
  return [
    renderer,
    store.cfg.renderer_available ? effects : null,
    h("div", { class: "card" }, h("h2", {}, "Camera and quality"),
      h("div", { class: "settings" },
        setting("Command bar", "Control Bar Pro redraws the bar and its menus for big screens. It is drawn for one resolution, picked from the one set on the Play page: choose it again after changing that. If it looks wrong with a mod, switch back.", bar),
        setting("Camera distance", "How far out you can zoom. The original game stops at 300.", camera),
        setting("High-quality settings", "Shadows, soft water edges, trees, heat effects, extra animations and 5000 particles. Volumes and controls are not changed.", apply),
        store.cfg.engines_bundled ? setting("Game engine", "The 64-bit engine is new. If a game misbehaves, switch to the 32-bit one and tell us what happened.", arch) : null),
      fb.el),
  ];
}

/** Says what goes into the debug zip before it is made; the replay (it can hold chat) is only added if ticked. */
function openBundleDialog(fb) {
  const replay = h("input", { type: "checkbox" });
  const go = h("button", { class: "btn primary", onclick: async () => {
    go.disabled = true; finish(); fb.busy("Collecting the logs…");
    try { const p = await invoke("make_debug_bundle", { includeReplay: replay.checked }); fb.ok(`Saved ${p}. Send it to the developer.`); } catch (e) { fb.bad(errText(e)); }
  } }, "Create the zip");
  const cancel = h("button", { class: "btn", onclick: () => finish() }, "Cancel");
  const overlay = h("div", { class: "overlay", role: "dialog", "aria-modal": "true", "aria-label": "Debug logs" },
    h("div", { class: "wizard bundledialog" },
      h("div", { class: "wiz-head" }, h("h2", {}, "Debug logs")),
      h("div", { class: "wiz-body" },
        h("p", {}, "This makes one zip in your Downloads folder. Nothing is sent anywhere: you choose who gets it."),
        h("div", { class: "stack tight" },
          h("p", { class: "small" }, h("b", {}, "Included: "), "the game's last two logs, the sync logs, your settings, the game's crash report and options file, and a list of your game and mod files."),
          h("p", { class: "small" }, h("b", {}, "Left out: "), "passwords, your server serial and keys, and your Steam account name. Your user name is replaced by <home> in the text files."),
          h("p", { class: "small" }, h("b", {}, "Still in it: "), "the server and room names, and the other players' lobby names.")),
        h("label", { class: "switch" }, replay, "Also include the last replay and crash dump (they can contain the game's chat and some of its memory)")),
      h("div", { class: "wiz-foot" }, cancel, go)));
  const onKey = (e) => { if (e.key === "Escape") finish(); };
  function finish() { document.removeEventListener("keydown", onKey); overlay.remove(); }
  document.addEventListener("keydown", onKey);
  document.getElementById("root").append(overlay);
  go.focus();
}

/** Settings card for finding out why a multiplayer game crashed or went out of sync. */
function syncCard() {
  const fb = liveStatus();
  const result = h("div", {});
  const on = h("input", { type: "checkbox", "aria-label": "Record sync logs while I play", checked: !!store.cfg.sync_logs, onchange: async () => {
    try { await invoke("set_sync_logs", { on: on.checked }); store.cfg.sync_logs = on.checked; fb.ok(on.checked ? "On. The next game you start records its first ten seconds." : "Off."); } catch (e) { fb.bad(errText(e)); }
  } });
  const compare = h("button", { class: "btn", onclick: async () => {
    let picked;
    try { picked = await tauri.dialog.open({ multiple: false, directory: false, title: "Choose the other player's sync logs (a .zip of their sync-logs folder)", filters: [{ name: "Zip", extensions: ["zip"] }] }); } catch (e) { return fb.bad(errText(e)); }
    if (!picked) return;
    compare.disabled = true; fb.busy("Comparing…"); fill(result);
    try {
      const r = await invoke("sync_compare", { other: typeof picked === "string" ? picked : picked.path });
      if (r.kind === "Same") fb.ok(`The same for all ${r.frames} frames logged. These two games did not diverge.`);
      else if (r.kind === "Nothing") fb.bad(r.why);
      else {
        fb.bad(`First difference at frame ${r.at.frame}, line ${r.at.line}.`);
        fill(result, h("p", { class: "muted small" }, "Yours:"), h("pre", { class: "crash" }, r.at.mine), h("p", { class: "muted small" }, "Theirs:"), h("pre", { class: "crash" }, r.at.theirs), h("p", { class: "muted small" }, "Send this to the developer with both folders."));
      }
    } catch (e) { fb.bad(errText(e)); }
    compare.disabled = false;
  } }, "Compare…");
  const bundle = h("button", { class: "btn primary", onclick: () => openBundleDialog(fb) }, "Download");
  return h("div", { class: "card" }, h("h2", {}, "Logs"),
    setting("Record sync logs", "Writes the first ten seconds of every game in detail, then a snapshot every few seconds, so two players can see where their games first differed, even minutes in. Both players should have it on.", h("label", { class: "switch" }, on)),
    setting("Debug logs", "One zip in your Downloads folder with the game logs, sync logs, settings (never passwords or keys) and crash report. You see what goes in before it is made.", bundle),
    setting("Compare with another player", "Choose the zip of their sync-logs folder.", compare),
    setting("Sync log folder", null, h("button", { class: "btn", onclick: () => invoke("open_sync_logs_folder").catch((e) => fb.bad(errText(e))) }, "Open")),
    fb.el, result);
}

/** Steam's downloader can remember a sign-in (a token, not the password). Rallypoint no longer keeps it; this removes an old one. */
function forgetSteamRow() {
  const fb = liveStatus();
  const btn = h("button", { class: "btn", onclick: async () => {
    btn.disabled = true;
    try { const n = await invoke("steam_forget_login"); fb.ok(n ? "Removed the saved Steam sign-in." : "There was no saved Steam sign-in."); } catch (e) { fb.bad(errText(e)); }
    btn.disabled = false;
  } }, "Forget");
  return setting("Saved Steam sign-in", h("span", {}, "Rallypoint clears it when a download ends. This removes one an earlier version left. ", fb.el), btn);
}

/** Tabs for a page with more than one screenful: one section shows at a time. `sections` is [id, label, build()]. */
function tabbed(label, sections, current, onPick) {
  const body = h("div", { class: "tabbody" });
  const show = (id) => {
    tabs.forEach((t, i) => { const on = sections[i][0] === id; t.setAttribute("aria-selected", String(on)); t.tabIndex = on ? 0 : -1; });
    fill(body, (sections.find((x) => x[0] === id) || sections[0])[2]());
    onPick && onPick(id);
  };
  const tabs = sections.map(([id, text], i) => h("button", { class: "tab", type: "button", role: "tab", onclick: () => show(id), onkeydown: (e) => {
    const step = e.key === "ArrowRight" ? 1 : e.key === "ArrowLeft" ? -1 : 0;
    if (!step) return;
    const next = (i + step + sections.length) % sections.length;
    show(sections[next][0]); tabs[next].focus();
  } }, text));
  const bar = h("div", { class: "tabs", role: "tablist", "aria-label": label }, tabs);
  show(sections.some((x) => x[0] === current) ? current : sections[0][0]);
  return { bar, body };
}

function settingsPage() {
  const files = gameFilesPanel(() => updateSidebar());
  const t = tabbed("Settings", [
    ["game", "Game", () => [h("div", { class: "card" }, h("h2", {}, "Game files"), files.el, forgetSteamRow())]],
    ["graphics", "Graphics", () => graphicsCards()],
    ["multiplayer", "Multiplayer", () => [syncCard()]],
    ["rallypoint", "Rallypoint", () => [
      updatesCard(),
      h("div", { class: "card" }, h("h2", {}, "Troubleshooting"), diagnosePanel(),
        firstRunSetupRow()),
      h("p", { class: "muted small about" }, `Rallypoint ${store.cfg.version} on ${store.cfg.os}. Launcher and server are MIT licensed; the game engine is GPL-3.0 with EA's additional terms. Not affiliated with or endorsed by Electronic Arts.`)]],
  ], store.settingsTab || "game", (id) => { store.settingsTab = id; });
  const el = h("div", { class: "page" },
    h("div", { class: "page-head" }, h("h1", {}, "Settings")),
    t.bar, t.body);
  return { el, dispose: files.dispose };
}

function firstRunSetupRow() {
  return setting("First-run setup", "Walk through the setup again for this game.", h("button", { class: "btn", onclick: () => openWizard() }, "Run"));
}

// ---------------------------------------------------------------- Red Alert 2
// The second game: the same shell and components, its own Play, Maps, Online lobby and Settings. Nothing of the
// Generals-only features (mods, World Builder, command bar, renderer, sync logs) appears here, nor Stronger AI:
// RA2 games always start with the computer at its normal strength (ai_boost 0) and repairing its own buildings.
const isRa2 = () => store.cfg.game === "ra2";
/** The whole shell takes the game's palette (style.css :root[data-game]). */
const applyGameTheme = () => { document.documentElement.dataset.game = isRa2() ? "ra2" : "generals"; };
const RA2_NAME = "Red Alert 2: Yuri's Revenge";
const RA2_SIDES = ["America", "Korea", "France", "Germany", "Great Britain", "Libya", "Iraq", "Cuba", "Russia", "Yuri"]; // the engine's countries in order (ra2launch::SIDES)
const RA2_COLORS = ["Yellow", "Red", "Blue", "Green", "Orange", "Teal", "Purple", "Pink"];
const RA2_PC = [3, 1, 0, 2, 4, 6, 5, 7]; // each RA2 colour's nearest place colour (style.css .pcN, PLACE_COLORS)
const RA2_SPEEDS = [[0, "Fastest"], [1, "Faster"], [2, "Fast"], [3, "Medium"], [4, "Slow"], [5, "Slower"], [6, "Slowest"]];
const RA2_CREDITS = [2500, 5000, 7500, 10000, 20000, 50000];
const RA2_DIFFS = [["easy", "Easy"], ["medium", "Medium"], ["brutal", "Brutal"]];
const RA2_TEAMS = [[-1, "No team"], [0, "Team A"], [1, "Team B"], [2, "Team C"], [3, "Team D"]];
// Engine Options combobox (main.cpp) plus common desktop sizes; filter against the screen in ra2ResolutionPick.
const RA2_WINDOW_PRESETS = [[640, 480], [800, 600], [1024, 768], [1280, 720], [1600, 900], [1920, 1080], [2560, 1440], [3840, 2160]];
const RA2_UI_SCALES = [[0, "Automatic"], [720, "Large"], [900, "Medium"], [1080, "Small"]];
const ra2Speed = () => (RA2_SPEEDS.some(([v]) => v === prefs.get().ra2Speed) ? prefs.get().ra2Speed : 2);
const ra2Credits = () => (RA2_CREDITS.includes(prefs.get().ra2Credits) ? prefs.get().ra2Credits : 10000);
const ra2Flag = (key, fallback = true) => (typeof prefs.get()[key] === "boolean" ? prefs.get()[key] : fallback);
/** CnCNet-style crate density: Off / Normal / Increased (min 8) / Extreme (min 40). */
const RA2_CRATE_DENSITIES = [["off", "Off"], ["normal", "Normal"], ["increased", "Increased"], ["extreme", "Extreme"]];
const RA2_CRATE_MIN = { increased: 8, extreme: 40 };
function ra2CrateDensity() {
  const d = prefs.get().ra2CrateDensity;
  if (RA2_CRATE_DENSITIES.some(([v]) => v === d)) return d;
  return ra2Flag("ra2Crates") ? "normal" : "off";
}
/** The crate density a room's settings describe. */
const crateDensOf = (r) => (r.crates === false ? "off" : r.crate_minimum === 40 ? "extreme" : r.crate_minimum === 8 ? "increased" : "normal");
/** Match options for a launch / lobby room from remembered prefs. */
function ra2MatchOpts() {
  const dens = ra2CrateDensity();
  return {
    crates: dens !== "off",
    crate_minimum: dens in RA2_CRATE_MIN ? RA2_CRATE_MIN[dens] : null,
    superweapons: ra2Flag("ra2Superweapons"),
    allies: ra2Flag("ra2Allies"),
    auto_repair: true,
    ai_boost: 0,
  };
}
/** The remembered rules, in the shape a room's settings have. */
const ra2RulesNow = () => ({ credits: ra2Credits(), speed: ra2Speed(), ...ra2MatchOpts() });
/** Remember a change to the rules for the next skirmish or room. */
function ra2SaveRules(p) {
  const n = {};
  if ("credits" in p) n.ra2Credits = p.credits;
  if ("speed" in p) n.ra2Speed = p.speed;
  if ("crates" in p) { n.ra2CrateDensity = crateDensOf(p); n.ra2Crates = p.crates; }
  if ("superweapons" in p) n.ra2Superweapons = p.superweapons;
  if ("allies" in p) n.ra2Allies = p.allies;
  prefs.set(n);
}
const RA2_CREDIT_OPTS = RA2_CREDITS.map((c) => [c, `$${c.toLocaleString("en-US")}`]);
/** The match rules: three values and two switches, the same in a skirmish and a room. With `set(patch)` they are
 *  controls; without it (a guest in a room) they are the host's choices as plain text in the same places. */
function ra2Rules(cur, set) {
  const text = (opts, v) => (opts.find(([x]) => x === v) || [v, String(v)])[1];
  const field = (name, opts, v, patch) => h(set ? "label" : "div", { class: "field" }, h("span", {}, name),
    set ? pick(name, opts, v, (x) => set(patch(x))) : h("span", { class: "fieldvalue" }, text(opts, v)));
  const flag = (name, desc, key) => {
    const on = cur[key] !== false;
    if (!set) return h("div", { class: "field" }, h("span", {}, name), h("span", { class: "fieldvalue" + (on ? "" : " off") }, on ? "On" : "Off"));
    const box = h("input", { type: "checkbox", role: "switch", checked: on, onchange: () => set({ [key]: box.checked }) });
    return h("label", { class: "rulesw" }, h("span", { class: "stack tight" }, h("b", {}, name), h("span", { class: "muted small" }, desc)), h("span", { class: "switch small" }, box));
  };
  return h("div", { class: "rules" + (set ? "" : " readonly"), role: "group", "aria-label": "Match rules" },
    field("Starting credits", RA2_CREDIT_OPTS, cur.credits, (x) => ({ credits: +x })),
    field("Game speed", RA2_SPEEDS, cur.speed, (x) => ({ speed: +x })),
    field("Crates", RA2_CRATE_DENSITIES, crateDensOf(cur), (x) => ({ crates: x !== "off", crate_minimum: x in RA2_CRATE_MIN ? RA2_CRATE_MIN[x] : null })),
    flag("Superweapons", "Nukes, weather storms and the rest", "superweapons"),
    flag("Allies", "Alliances can change mid-game", "allies"));
}
const fileOf = (path) => String(path).split(/[\\/]/).pop();
const sizeText = (kb) => (kb >= 1024 ? `${(kb / 1024).toFixed(1)} MB` : `${kb} KB`);
const playersText = (n) => (n >= 2 ? `${n} players` : "No start positions");

/** A drop-down of [value, text] pairs; values are numbers or strings, compared as text. */
function pick(label, options, value, onchange) {
  const el = h("select", { "aria-label": label, onchange: () => onchange(el.value) }, ...options.map(([v, t]) => new Option(t, String(v))));
  el.value = String(value);
  return el;
}
/** The four choices every seat has. `change(patch)` gets the one that changed. */
function seatCells(who, seat, starts, change) {
  return [
    pick(`Side for ${who}`, [[-1, "Random side"], ...RA2_SIDES.map((s, i) => [i, s])], seat.side, (v) => change({ side: +v })),
    pick(`Colour for ${who}`, [[-1, "Any colour"], ...RA2_COLORS.map((c, i) => [i, c])], seat.color, (v) => change({ color: +v })),
    pick(`Start for ${who}`, [[-1, "Random"], ...Array.from({ length: starts }, (_, n) => [n + 1, `Start ${n + 1}`])], seat.start, (v) => change({ start: +v })),
    pick(`Team for ${who}`, RA2_TEAMS, seat.team, (v) => change({ team: +v })),
  ];
}
/** The same four as text, for another player's seat. */
const seatText = (s) => [RA2_SIDES[s.side] || "Random side", RA2_COLORS[s.color] || "Any colour", s.start > 0 ? `Start ${s.start}` : "Random", (RA2_TEAMS.find(([v]) => v === s.team) || RA2_TEAMS[0])[1]]
  .map((t) => h("span", { class: "skcell" }, t));
const ra2Swatch = (color) => h("i", { class: "swatch" + (color >= 0 ? "" : " any"), "aria-hidden": "true" });
const ra2RowClass = (color) => (color >= 0 ? ` pc${RA2_PC[color]}` : "");
const markerColor = (color) => (color >= 0 ? PLACE_COLORS[RA2_PC[color]] : css("--muted"));

async function loadRa2Maps() {
  try { store.ra2Maps = await invoke("ra2_maps"); } catch (e) { store.ra2Maps = []; say(errText(e), "bad"); }
  return store.ra2Maps;
}
const playable = (maps) => (maps || []).filter((m) => m.players >= 2);

/** Start Red Alert 2 with a setup (a skirmish, or the lobby's game). */
async function launchRa2(setup) {
  const busy = busyOverlay(`Starting ${RA2_NAME}…`);
  let ok = false;
  try { await invoke("ra2_launch", { setup }); ok = true; } catch (e) { say(errText(e), "bad"); }
  setTimeout(() => busy.close(), ok ? 5000 : 0);
  return ok;
}

/** The skirmish table: you, then one row per other start on the map (a computer player or closed). Assigning a start
 *  or a colour someone else has takes it from them (a start goes back to random, a colour is swapped). */
function ra2Skirmish(onChange) {
  let maps = [], view = null, rows = [], active = 0, spots = [], token = 0;
  const canvas = h("canvas", { class: "mapview-canvas", width: MAP_VIEW, height: MAP_VIEW, role: "img", "aria-label": "Map preview: click a start to give it to the selected player" });
  const facts = h("p", { class: "factline" });
  const note = h("p", { class: "muted small", role: "status" });
  const mapSel = h("select", { "aria-label": "Map", onchange: () => { prefs.set({ ra2Map: mapSel.value }); load(); } });
  const table = h("div", { class: "skgrid ra2", role: "group", "aria-label": "Skirmish players" });
  const summary = h("span", { class: "muted small" });
  const hint = h("p", { class: "muted small maphint" }, "Click a start on the map to place the highlighted player.");
  const starts = () => (view && view.info ? Math.min(8, view.info.starts.length) : 0);

  function newRows(n) {
    const wantAi = Math.min(Math.max(1, prefs.get().ra2Ai || 1), n - 1), diff = RA2_DIFFS.some(([d]) => d === prefs.get().ra2Diff) ? prefs.get().ra2Diff : "medium";
    const mine = store.cfg.ra2.color >= 0 ? store.cfg.ra2.color : 0;
    rows = Array.from({ length: n }, (_, k) => ({ kind: k === 0 ? "you" : k <= wantAi ? "ai" : "closed", difficulty: diff, side: k === 0 ? store.cfg.ra2.side : -1, color: -1, start: -1, team: -1 }));
    // every open place gets its own colour, so the map and the table agree from the start
    rows[0].color = mine;
    rows.forEach((r, k) => { if (k && r.kind !== "closed") r.color = freeColor(k); });
    active = 0;
  }
  function freeColor(k, want = -1) {
    const used = new Set(rows.filter((r, j) => j !== k && r.kind !== "closed").map((r) => r.color).filter((c) => c >= 0));
    if (want >= 0 && !used.has(want)) return want;
    for (let c = 0; c < RA2_COLORS.length; c++) if (!used.has(c)) return c;
    return -1;
  }
  function change(k, patch) {
    const r = rows[k];
    if ("start" in patch && patch.start > 0) rows.forEach((o, j) => { if (j !== k && o.start === patch.start) o.start = -1; });
    if ("color" in patch && patch.color >= 0) rows.forEach((o, j) => { if (j !== k && o.color === patch.color) o.color = r.color; });
    Object.assign(r, patch);
    draw();
  }
  function setActive(k) {
    if (active === k) return;
    active = k;
    table.querySelectorAll(".skrow2").forEach((el, j) => el.classList.toggle("active", j === k));
    paint();
  }
  function draw() {
    const n = starts();
    if (!rows.length) {
      fill(table, h("p", { class: "muted small" }, !maps.length ? "" : "This map has no start positions for a skirmish."));
      summary.textContent = "";
    } else {
      const head = h("div", { class: "skrowhead muted small" }, h("span", {}), h("span", {}, "Player"), h("span", {}, "Side"), h("span", {}, "Colour"), h("span", {}, "Start"), h("span", {}, "Team"));
      fill(table, head, rows.map((r, k) => {
        const closed = r.kind === "closed";
        const who = k === 0 ? h("span", { class: "skwho" }, "You")
          : pick(`Place ${k + 1}`, [...RA2_DIFFS.map(([d, t]) => [d, `${t} AI`]), ["closed", "Closed"]], closed ? "closed" : r.difficulty, (v) => {
            if (v === "closed") change(k, { kind: "closed" });
            else { prefs.set({ ra2Diff: v }); change(k, { kind: "ai", difficulty: v, color: closed ? freeColor(k) : r.color }); }
          });
        const cells = closed ? [h("span", {}), h("span", {}), h("span", {}), h("span", {})] : seatCells(k === 0 ? "you" : `place ${k + 1}`, r, n, (p) => change(k, p));
        return h("div", { class: "skrow2" + (closed ? " closed" : ra2RowClass(r.color)) + (k === active ? " active" : ""), onclick: () => setActive(k), onfocusin: () => setActive(k) }, ra2Swatch(closed ? -1 : r.color), who, cells);
      }));
      const ai = rows.filter((r) => r.kind === "ai").length;
      summary.textContent = ai ? `You and ${ai} computer player${ai === 1 ? "" : "s"}` : "No computer players";
    }
    hint.hidden = !rows.length;
    paint();
    show();
    onChange && onChange();
  }
  function paint() {
    if (!view) { const c = canvas.getContext("2d"); c.fillStyle = css("--bg"); c.fillRect(0, 0, MAP_VIEW, MAP_VIEW); spots = []; return; }
    spots = paintMap(canvas, view, (n) => {
      const k = rows.findIndex((r) => r.kind !== "closed" && r.start === n);
      return k < 0 ? null : { color: markerColor(rows[k].color), halo: k === active };
    });
  }
  canvas.addEventListener("click", (e) => { const s = spotUnder(canvas, spots, e); const r = rows[active]; if (s && r && r.kind !== "closed") change(active, { start: r.start === s.n ? -1 : s.n }); });
  canvas.addEventListener("mousemove", (e) => { const s = spotUnder(canvas, spots, e); canvas.style.cursor = s ? "pointer" : "default"; canvas.title = s ? `Start ${s.n}: click to give it to the selected player` : ""; });

  async function load() {
    const file = mapSel.value, mine = ++token;
    view = null; rows = []; fill(facts); note.textContent = file ? "Loading…" : ""; errBox.hidden = true;
    canvas.parentNode && canvas.parentNode.classList.toggle("loading", !!file);
    draw();
    if (!file) return;
    try {
      const v = await invoke("ra2_map_view", { file });
      if (mine !== token) return;
      view = v;
      const n = starts();
      if (n >= 2) newRows(n);
      const m = maps.find((x) => fileOf(x.path) === file);
      fill(facts, [`${v.cells_w} × ${v.cells_h}`, playersText(v.info ? v.info.starts.length : 0)].map((b) => h("span", {}, b)));
      note.textContent = !v.rgba ? "This map has no preview picture." : "";
      canvas.parentNode.classList.remove("loading");
      draw();
    } catch (e) {
      if (mine !== token) return;
      note.textContent = ""; canvas.parentNode.classList.remove("loading");
      errBox.hidden = false;
      fill(errBox, callout("bad", `This map could not be read (${errText(e)}). Pick another one, or try it again.`, h("button", { class: "btn small", onclick: load }, "Try again")));
      onChange && onChange();
    }
  }
  async function reload() {
    maps = playable(await loadRa2Maps());
    loaded = true;
    const want = prefs.get().ra2Map;
    fill(mapSel, maps.map((m) => new Option(`${m.name} (${m.players})`, fileOf(m.path))));
    mapSel.disabled = !maps.length;
    mapSel.value = maps.some((m) => fileOf(m.path) === want) ? want : maps.length ? fileOf(maps[0].path) : "";
    show();
    await load();
  }
  const setup = () => ({
    map: mapSel.value, seed: 0,
    ...ra2RulesNow(),
    players: rows.filter((r) => r.kind !== "closed").map((r) => ({ name: r.kind === "you" ? (store.cfg.player_name || store.cfg.default_player_name || "Player") : "", ...(r.kind === "ai" ? { ai: r.difficulty } : {}), side: r.side, color: r.color, start: r.start, team: r.team })),
  });
  /** Why Play cannot start this setup yet ("" when it can). */
  const why = () => !store.cfg.ra2.game_dir ? "Find your game files first."
    : !loaded ? "Reading your maps…" : !maps.length ? "Add a map first." : errBox.hidden === false ? "Pick a map that can be read." : !view ? "Loading the map…" : !rows.length ? "This map has no start positions."
    : !rows.some((r) => r.kind === "ai") ? "Give the computer at least one place." : "";
  const empty = emptyState("maps", "No skirmish maps yet", "Maps with at least two start positions show up here. Add community packs or your own files.", h("button", { class: "btn", onclick: () => go("maps") }, "Open Maps"));
  const skel = h("div", { class: "ra2setup", "aria-hidden": "true" }, h("span", { class: "skeleton sk-map" }), h("div", { class: "stack" }, h("span", { class: "skeleton sk-line" }), skeletonLines(4)));
  const errBox = h("div", { hidden: true });
  const rules = ra2Rules(ra2RulesNow(), (p) => { ra2SaveRules(p); onChange && onChange(); });
  const body = h("div", { class: "ra2setup" },
    h("div", { class: "mapcol" }, h("div", { class: "stage" }, canvas), facts, hint),
    h("div", { class: "mapside" },
      h("label", { class: "field" }, h("span", {}, "Map"), mapSel),
      h("div", { class: "sidehead" }, h("h3", {}, "Players"), summary), table, note, errBox,
      h("h3", { class: "ruleshead" }, "Rules"), rules));
  const el = h("div", { class: "stack" }, skel, empty, body);
  let loaded = false;
  const show = () => { skel.hidden = loaded; empty.hidden = !loaded || !!maps.length; body.hidden = !loaded || !maps.length; };
  /** What Play will start, in one line, for the footer beside it. */
  const what = () => {
    const m = maps.find((x) => fileOf(x.path) === mapSel.value), ai = rows.filter((r) => r.kind === "ai").length;
    if (!m || !ai) return "";
    const speedName = (RA2_SPEEDS.find(([v]) => v === ra2Speed()) || [0, ""])[1].toLowerCase();
    return `${m.name}: you against ${ai} computer player${ai === 1 ? "" : "s"}, $${ra2Credits().toLocaleString("en-US")}, ${speedName} speed`;
  };
  reload();
  return { el, setup, why, what, reload, remember: () => prefs.set({ ra2Ai: rows.filter((r) => r.kind === "ai").length }) };
}

/** Drawn flags for the ten countries (index = RA2_SIDES); -1 is Random. Our own SVG, nothing from the game's files. */
const RA2_FLAGS = {
  "-1": '<rect width="60" height="40" fill="#5b6478"/><text x="30" y="29" font-size="26" font-family="sans-serif" font-weight="700" fill="#fff" text-anchor="middle">?</text>',
  0: '<rect width="60" height="40" fill="#fff"/>' + [0, 2, 4, 6].map((i) => `<rect y="${i * 40 / 7}" width="60" height="${40 / 7}" fill="#b22234"/>`).join("") + '<rect width="27" height="22.9" fill="#3c3b6e"/>' + [0, 1, 2].flatMap((r) => [0, 1, 2, 3].map((c) => `<circle cx="${5 + c * 6 + (r % 2) * 3}" cy="${5 + r * 6}" r="1.3" fill="#fff"/>`)).join(""),
  1: '<rect width="60" height="40" fill="#fff"/><path d="M21 20a9 9 0 0 1 18 0z" fill="#cd2e3a"/><path d="M39 20a9 9 0 0 1-18 0z" fill="#0047a0"/><g stroke="#111" stroke-width="2"><path d="M7 8l6-4M9 11l6-4M47 4l6 4M45 7l6 4M7 32l6 4M9 29l6 4M47 36l6-4M45 33l6-4"/></g>',
  2: '<rect width="20" height="40" fill="#0055a4"/><rect x="20" width="20" height="40" fill="#fff"/><rect x="40" width="20" height="40" fill="#ef4135"/>',
  3: '<rect width="60" height="40" fill="#ffce00"/><rect width="60" height="26.7" fill="#dd0000"/><rect width="60" height="13.3" fill="#000"/>',
  4: '<rect width="60" height="40" fill="#012169"/><path d="M0 0L60 40M60 0L0 40" stroke="#fff" stroke-width="8"/><path d="M0 0L60 40M60 0L0 40" stroke="#c8102e" stroke-width="3"/><path d="M30 0v40M0 20h60" stroke="#fff" stroke-width="12"/><path d="M30 0v40M0 20h60" stroke="#c8102e" stroke-width="7"/>',
  5: '<rect width="60" height="40" fill="#239e46"/><circle cx="28" cy="20" r="8" fill="#fff"/><circle cx="31" cy="20" r="6.6" fill="#239e46"/><path d="M36 20l4 1.5-2.5-3.2v3.4z" fill="#fff"/>',
  6: '<rect width="60" height="40" fill="#fff"/><rect width="60" height="13.3" fill="#ce1126"/><rect y="26.7" width="60" height="13.3" fill="#000"/><g fill="#007a3d"><rect x="14" y="17" width="6" height="6"/><rect x="27" y="17" width="6" height="6"/><rect x="40" y="17" width="6" height="6"/></g>',
  7: '<rect width="60" height="40" fill="#fff"/>' + [0, 2, 4].map((i) => `<rect y="${i * 8}" width="60" height="8" fill="#002a8f"/>`).join("") + '<path d="M0 0L26 20 0 40z" fill="#cf142b"/><circle cx="8" cy="20" r="4" fill="#fff"/>',
  8: '<rect width="60" height="40" fill="#fff"/><rect y="13.3" width="60" height="13.3" fill="#0039a6"/><rect y="26.7" width="60" height="13.3" fill="#d52b1e"/>',
  9: '<rect width="60" height="40" fill="#3b1a5e"/><circle cx="30" cy="20" r="12" fill="none" stroke="#c9a6ff" stroke-width="2.5"/><path d="M30 10l8.7 15h-17.4z" fill="#c9a6ff"/><circle cx="30" cy="20" r="2.5" fill="#3b1a5e"/>',
};
const ra2FlagUrl = (v) => "data:image/svg+xml;utf8," + encodeURIComponent(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 60 40" preserveAspectRatio="xMidYMid slice">${RA2_FLAGS[v] || RA2_FLAGS[-1]}</svg>`);

/** Quick Skirmish for Red Alert 2: the same card as Generals (opponents, difficulty, one button), with your country,
 *  the map, your start, speed and starting credits beside them. Everything is remembered. */
function ra2PlayPage() {
  const status = liveStatus();
  let maps = null; // null while they are being read
  const p = prefs.get();
  const skAi = segmented("Quick Skirmish opponents", [1, 2, 3].map((n) => [String(n), String(n)]), String(Math.min(3, Math.max(1, p.ra2Ai || 1))), (v) => prefs.set({ ra2Ai: +v }));
  const skDiff = segmented("Quick Skirmish difficulty", RA2_DIFFS, RA2_DIFFS.some(([d]) => d === p.ra2Diff) ? p.ra2Diff : "medium", (v) => { prefs.set({ ra2Diff: v }); update(); });
  const side = () => (typeof prefs.get().ra2QuickSide === "number" ? prefs.get().ra2QuickSide : store.cfg.ra2.side);
  const country = h("select", { "aria-label": "Your country", onchange: () => { prefs.set({ ra2QuickSide: +country.value }); paintFlag(); } },
    new Option("Random", "-1"), ...RA2_SIDES.map((t, i) => new Option(t, String(i))));
  country.value = String(side());
  const flagBig = h("img", { class: "flagcard", alt: "", "aria-hidden": "true" });
  const heroArt = h("img", { class: "heroart shown", alt: "", "aria-hidden": "true" });
  function paintFlag() {
    flagBig.src = ra2FlagUrl(+country.value);
    // Random has no flag worth blowing up: the hero gets plain rays instead
    heroArt.src = +country.value < 0 ? "data:image/svg+xml;utf8," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 60 40" preserveAspectRatio="xMidYMid slice"><rect width="60" height="40" fill="#5b6478"/><path d="M60 20L0 0v6zM60 20L0 14v6zM60 20L0 28v6z" fill="#fff" opacity=".25"/></svg>') : flagBig.src;
  }
  paintFlag();
  const mapSel = h("select", { "aria-label": "Map", onchange: () => { prefs.set({ ra2QuickMap: mapSel.value }); update(); } });
  const startSel = h("select", { "aria-label": "Your start position", onchange: () => prefs.set({ ra2QuickStart: +startSel.value }) },
    new Option("Random start", "0"), ...Array.from({ length: 8 }, (_, n) => new Option(`Start ${n + 1}`, String(n + 1))));
  startSel.value = String(Math.min(8, Math.max(0, prefs.get().ra2QuickStart || 0)));
  const selOf = (label, opts, cur, key) => { const el = h("select", { "aria-label": label, onchange: () => ra2SaveRules({ [key]: +el.value }) }, ...opts.map(([v, t]) => new Option(t, String(v)))); el.value = String(cur); return el; };
  const speed = selOf("Game speed", RA2_SPEEDS, ra2Speed(), "speed");
  const credits = selOf("Starting credits", RA2_CREDIT_OPTS, ra2Credits(), "credits");

  /** The map and places for this click: the chosen map, or a random one with room for everyone. */
  function pick() {
    const n = +skAi.value, start = +startSel.value, file = mapSel.value;
    const fits = (m) => m.players > n && m.players >= start;
    const chosen = file && maps.find((m) => fileOf(m.path) === file);
    const pool = maps.filter(fits);
    const m = chosen || (pool.length ? pool[Math.floor(Math.random() * pool.length)] : maps[Math.floor(Math.random() * maps.length)]);
    return { m, ai: Math.min(n, m.players - 1), start: start <= m.players ? start : 0 };
  }
  const setup = () => {
    const { m, ai, start } = pick();
    const mine = store.cfg.ra2.color >= 0 ? store.cfg.ra2.color : 0;
    const free = RA2_COLORS.map((_, c) => c).filter((c) => c !== mine);
    return { map: fileOf(m.path), seed: 0, ...ra2RulesNow(), players: [
      { name: store.cfg.player_name || store.cfg.default_player_name || "Player", side: side(), color: mine, start: start || -1, team: -1 },
      ...Array.from({ length: ai }, (_, k) => ({ name: "", ai: skDiff.value, side: -1, color: free[k], start: -1, team: -1 })),
    ] };
  };
  const play = h("button", { class: "btn primary big", onclick: async () => {
    play.disabled = true;
    const ok = await launchRa2(setup());
    setTimeout(update, ok ? 5000 : 0);
  } }, icon("play"), "Start skirmish");
  function update() {
    const why = !store.cfg.ra2_engine ? "This build has no Red Alert 2 engine yet." : !store.cfg.ra2.game_dir ? "Find your game files first." : maps === null ? "Reading your maps…" : !maps.length ? "Add a map first." : "";
    play.disabled = !!why;
    status.set(why);
  }
  const hasFiles = !!store.cfg.ra2.game_dir;
  if (hasFiles) loadRa2Maps().then((all) => {
    maps = playable(all);
    fill(mapSel, new Option("Random map", ""), maps.map((m) => new Option(`${m.name} (${m.players})`, fileOf(m.path))));
    const want = prefs.get().ra2QuickMap;
    mapSel.value = maps.some((m) => fileOf(m.path) === want) ? want : "";
    update();
  });

  const disp = h("span", { class: "muted small" });
  const redrawDisp = () => fill(disp, ra2DisplayLabel(store.cfg.ra2, nativeRes()));
  redrawDisp();
  window.addEventListener("ra2display", redrawDisp);
  const hero = h("div", { class: "card hero" }, heroArt,
    h("div", { class: "stack tight herotext" }, h("h2", { title: store.cfg.ra2.game_dir || "" }, "Yuri's Revenge"), h("p", { class: "muted" }, "Command & Conquer: Red Alert 2"),
      h("div", { class: "row between" }, h("p", { class: "muted small gamepath" }, disp, " ", h("button", { class: "linkbtn", onclick: () => { store.settingsTab = "display"; go("settings"); } }, "Change")))));
  const setupCard = callout("warn", "Rallypoint needs your own copy of Red Alert 2 and Yuri's Revenge. Steam and EA installs are found for you.", h("button", { class: "btn primary", onclick: () => openWizard() }, "Find game files"));
  const skOptions = h("div", { class: "card skcard" },
    h("div", { class: "stack tight" }, h("h3", {}, "Quick Skirmish"), h("p", { class: "muted" }, "A match against the computer, started straight away.")),
    h("div", { class: "skoptions" },
      h("div", { class: "skgroup" }, h("span", { class: "muted small" }, "Opponents"), skAi.el),
      h("div", { class: "skgroup" }, h("span", { class: "muted small" }, "Difficulty"), skDiff.el),
      h("div", { class: "row skplay" }, status.el, play)));
  const optCard = h("div", { class: "card" }, h("h3", {}, "Options"),
    h("div", { class: "grid2" },
      h("label", { class: "field" }, h("span", {}, "Country"), h("div", { class: "countryrow" }, flagBig, country)),
      h("label", { class: "field" }, h("span", {}, "Map"), mapSel),
      h("label", { class: "field" }, h("span", {}, "Your start"), startSel),
      h("label", { class: "field" }, h("span", {}, "Starting credits"), credits),
      h("label", { class: "field" }, h("span", {}, "Game speed"), speed)));
  const el = h("div", { class: "page" }, pageHead("Play"), hero, ...(hasFiles ? [skOptions, optCard] : [setupCard]));
  update();
  return { el, dispose: () => window.removeEventListener("ra2display", redrawDisp) };
}

/** The full skirmish setup for one map, in a dialog (the Generals map viewer's counterpart). */
function openRa2Skirmish(m) {
  prefs.set({ ra2Map: fileOf(m.path) });
  const status = liveStatus();
  const sk = ra2Skirmish(() => update());
  const play = h("button", { class: "btn primary big playbtn", onclick: async () => {
    play.disabled = true;
    sk.remember();
    if (await launchRa2(sk.setup())) finish(); else update();
  } }, icon("play"), "Play skirmish");
  function update() { const why = !store.cfg.ra2_engine ? "This build has no Red Alert 2 engine yet." : sk.why(); play.disabled = !!why; status.set(why); }
  const overlay = h("div", { class: "overlay", role: "dialog", "aria-modal": "true", "aria-label": "Skirmish setup" },
    h("div", { class: "wizard mapviewer" },
      h("div", { class: "wiz-head mvhead" }, h("div", { class: "mvtitle" }, h("h2", {}, "Skirmish setup"))),
      h("div", { class: "wiz-body" }, sk.el),
      h("div", { class: "wiz-foot" }, status.el, h("button", { class: "btn", onclick: () => finish() }, "Close"), play)));
  const onKey = (e) => { if (e.key === "Escape" && e.target.tagName !== "SELECT") finish(); };
  function finish() { document.removeEventListener("keydown", onKey); overlay.remove(); }
  document.addEventListener("keydown", onKey);
  document.getElementById("root").append(overlay);
  update();
}

/** Maps for Red Alert 2, laid out like the Generals page: search, player chips, sort, one row per map with Skirmish,
 *  then a "Get more maps" card. The list is read when the page opens and again whenever the window is focused again. */
function ra2MapsPage() {
  const LIMIT = 150;
  let maps = [], q = "", pf = "", sortBy = "name", shown = LIMIT, community = null;
  const search = h("input", { type: "text", placeholder: "Search maps by name", "aria-label": "Search maps by name", oninput: () => { q = search.value.trim().toLowerCase(); shown = LIMIT; draw(); } });
  const searchClear = h("button", { class: "searchclear", "aria-label": "Clear the search", hidden: true, onclick: () => { q = ""; search.value = ""; shown = LIMIT; draw(); search.focus(); } }, "×");
  const chips = h("div", { class: "chips", role: "group", "aria-label": "Number of players" });
  const sortSel = h("select", { "aria-label": "Sort maps", onchange: () => { sortBy = sortSel.value; shown = LIMIT; draw(); } },
    new Option("Name", "name"), new Option("Fewest players", "players"), new Option("Most players", "players-desc"), new Option("Largest file", "size"));
  const result = h("p", { class: "muted small mapresult", role: "status" });
  const clear = h("button", { class: "btn small ghost", hidden: true, onclick: () => { q = ""; pf = ""; search.value = ""; shown = LIMIT; draw(); } }, "Clear filters");
  const list = h("div", { class: "maplist" });
  const empty = h("div", { class: "listempty", role: "status", hidden: true });
  const more = h("button", { class: "btn small", hidden: true, onclick: () => { shown += LIMIT; draw(); } }, "Show more");
  const byName = (a, b) => a.name.toLowerCase().localeCompare(b.name.toLowerCase());
  const base = () => maps.filter((m) => !q || m.name.toLowerCase().includes(q));
  const visible = () => {
    const v = base().filter((m) => pf === "" || m.players === Number(pf));
    if (sortBy === "size") v.sort((a, b) => b.size_kb - a.size_kb || byName(a, b));
    else if (sortBy.startsWith("players")) { const d = sortBy === "players" ? 1 : -1; v.sort((a, b) => d * (a.players - b.players) || byName(a, b)); }
    else v.sort(byName);
    return v;
  };
  function drawChips() {
    // only the player counts that the search leaves, so a chip never leads to nothing
    const have = new Map();
    for (const m of base()) have.set(m.players, (have.get(m.players) || 0) + 1);
    if (pf !== "" && !have.has(Number(pf))) pf = "";
    const chip = (value, label) => h("button", { class: "chip", "aria-pressed": String(pf === value), onclick: () => { pf = value; shown = LIMIT; draw(); } }, label);
    fill(chips, h("span", { class: "muted small chiplabel" }, "Players"), chip("", "Any"),
      ...[...have.keys()].sort((a, b) => a - b).map((n) => {
        const c = chip(String(n), [n < 2 ? "Solo" : String(n), h("span", { class: "chipcount", "aria-hidden": "true" }, String(have.get(n)))]);
        const what = `${have.get(n)} ${n < 2 ? "solo" : n + "-player"} map${have.get(n) === 1 ? "" : "s"}`;
        c.title = what; c.setAttribute("aria-label", what);
        return c;
      }));
  }
  function draw() {
    const v = visible();
    drawChips();
    result.textContent = maps.length ? (v.length === maps.length ? `${maps.length} maps` : `${v.length} of ${maps.length} maps`) : "";
    clear.hidden = !(q || pf !== "");
    searchClear.hidden = !q;
    empty.hidden = !!v.length; list.hidden = !v.length;
    fill(empty, maps.length
      ? [h("strong", {}, "No maps match"), h("span", { class: "muted small" }, q ? `Nothing is called “${search.value.trim()}” with these filters.` : "No map fits these filters."), h("button", { class: "btn small", onclick: () => clear.click() }, "Clear filters")]
      : [h("strong", {}, "No maps yet"), h("span", { class: "muted small" }, "Install the community packs below, or put .map, .mpr and .yrm files in your map folder.")]);
    fill(list, v.slice(0, shown).map((m) => h("div", { class: "maprow ra2" },
      h("span", { class: "maptext" }, h("span", { class: "mapname" }, m.name)),
      h("span", { class: "small mapplayers" }, m.players >= 2 ? `${m.players} players` : "Solo"),
      h("span", { class: "muted small mapsize" }, sizeText(m.size_kb)),
      m.players >= 2 ? h("button", { class: "btn small", "aria-label": `Set up a skirmish on ${m.name}`, title: "Choose countries, colours, teams and start positions", onclick: () => openRa2Skirmish(m) }, icon("play"), "Skirmish") : h("span", {}))));
    more.hidden = v.length <= shown;
    more.textContent = `Show more (${v.length - shown} left)`;
  }
  async function reload(first) {
    if (first) fill(empty, h("div", { class: "maps-skel", "aria-hidden": "true" }, skeletonLines(6)), h("span", { class: "sr", role: "status" }, "Reading your maps")), empty.hidden = false, list.hidden = true;
    try { community = await invoke("ra2_community_packs"); } catch (_) { community = null; }
    maps = await loadRa2Maps();
    draw();
    drawPacks();
  }
  // The folder can change while the launcher is in the background (a download, a copied map): look again on return.
  const onFocus = () => reload(false);
  window.addEventListener("focus", onFocus);

  const packStatus = h("p", { class: "muted small" });
  const installPacks = h("button", { class: "btn primary", onclick: async () => {
    installPacks.disabled = true;
    packStatus.textContent = "Installing the community map packs…";
    try {
      const [packs, files] = await invoke("ra2_install_community_packs");
      say(`Installed ${packs} pack${packs === 1 ? "" : "s"} (${files} maps)`, "ok");
      await reload(false);
    } catch (e) { say(errText(e), "bad"); packStatus.textContent = errText(e); installPacks.disabled = false; }
  } }, "Install community packs");
  async function openFolder() { try { await invoke("ra2_open_maps_folder"); } catch (e) { say(errText(e), "bad"); } }
  function drawPacks() {
    const c = community, have = c && c.installed_files || 0;
    const total = c ? (c.packs || []).reduce((n, x) => n + (x.maps || 0), 0) : 0;
    installPacks.hidden = !c || !c.available || have > 0;
    installPacks.disabled = false;
    packStatus.textContent = !c || !c.available ? "Put map files in your map folder and they show up above." : have ? `Community packs are installed (${have} files).` : `${(c.packs || []).length} community packs with ${total} maps are ready to install.`;
  }
  reload(true);
  const el = h("div", { class: "page" },
    pageHead("Maps"),
    h("div", { class: "skbanner" }, h("span", { class: "skbanner-icon", "aria-hidden": "true" }, icon("play")), h("div", {}, h("h3", {}, "Play a skirmish on any map"), h("p", { class: "muted small" }, "Press Skirmish on a map below to choose who plays, their countries, colours, teams and where they start."))),
    h("div", { class: "card" },
      h("div", { class: "organiser" },
        h("h2", {}, "Your maps"),
        h("div", { class: "organiser-bar" }, h("div", { class: "searchbox" }, search, searchClear)),
        chips,
        h("div", { class: "listbox" },
          h("div", { class: "listhead" }, h("div", { class: "row" }, result, clear), h("label", { class: "sortpick" }, h("span", { class: "muted small" }, "Sort by"), sortSel)),
          list, empty),
        h("div", { class: "row center" }, more))),
    h("div", { class: "card" }, h("h2", {}, "Get more maps"), packStatus, h("div", { class: "row" }, installPacks, h("button", { class: "btn", onclick: openFolder }, icon("folder"), "Open map folder"))));
  return { el, dispose: () => window.removeEventListener("focus", onFocus) };
}

/** The online lobby: the host's map with its starts, who is in the room, their seats, the options, and Start. Shown
 *  while connected. Ready belongs to the current settings: any change by the host clears it (see `ra2lobby`). */
function ra2LobbyCard() {
  let sig = "", seeded = false, hostSeeded = false, maps = null, hostAi = [], mySeat = null, active = -1, cleared = false;
  const views = {}; // map file -> map_view result, or { error }
  const card = h("div", { class: "card", hidden: true });
  const fb = liveStatus();
  const sendSeat = () => invoke("ra2_lobby_seat", { seat: mySeat }).catch((e) => fb.bad(errText(e)));
  async function sendRoom(settings) {
    try {
      const opts = {
        crates: settings.crates !== false,
        superweapons: settings.superweapons !== false,
        allies: settings.allies !== false,
        auto_repair: true,
        crate_minimum: settings.crates === false ? null : (settings.crate_minimum ?? null),
        ai_boost: 0,
      };
      await invoke("ra2_lobby_room", {
        map: settings.map, credits: settings.credits, speed: settings.speed,
        ...opts, ai: hostAi,
      });
    } catch (e) { fb.bad(errText(e)); }
  }
  async function loadView(file) {
    views[file] = null;
    try { views[file] = await invoke("ra2_map_view", { file }); } catch (e) { views[file] = { error: errText(e) }; }
    sig = ""; draw();
  }
  async function draw() {
    const d = store.lobby;
    card.hidden = !store.relay.startsWith("Connected as");
    if (card.hidden) { sig = ""; seeded = hostSeeded = false; return; }
    if (!d) { if (sig !== "wait") { sig = "wait"; fill(card, h("h2", {}, "Lobby"), h("p", { class: "muted small row" }, h("span", { class: "spin", "aria-hidden": "true" }), "Joining the lobby…")); } return; }
    const me = d.players.find((p) => p.me);
    if (!seeded) { seeded = true; mySeat = { side: store.cfg.ra2.side, color: store.cfg.ra2.color, start: -1, team: -1, ready: false }; sendSeat(); }
    else if (me) {
      // the lobby's copy wins: a change by the host clears ready there
      if (mySeat.ready && !me.ready) cleared = true;
      mySeat = { side: me.side, color: me.color, start: me.start, team: me.team, ready: me.ready };
    }
    if (d.am_host && maps === null) { maps = []; maps = playable(await loadRa2Maps()); }
    if (d.am_host && !d.settings && !hostSeeded && maps && maps.length) {
      hostSeeded = true;
      const want = prefs.get().ra2Map, m = maps.find((x) => fileOf(x.path) === want) || maps[0];
      sendRoom({ map: fileOf(m.path), ...ra2RulesNow() });
    }
    if (d.settings && !(d.settings.map in views)) loadView(d.settings.map);
    if (!d.am_host || (active >= 0 && !(d.settings && d.settings.ai[active]))) active = -1;
    const next = JSON.stringify({ p: d.players, s: d.settings, h: d.am_host, b: d.blocked, m: maps && maps.length, a: active, c: cleared });
    if (next === sig) return;
    sig = next;
    render(store.lobby);
  }
  /** Who is on start n: a name, a colour, and whether it is the selected place (you, or the host's chosen computer). */
  function ownerOf(d, n) {
    const s = d.settings;
    const p = d.players.find((x) => (x.me ? mySeat.start : x.start) === n);
    if (p) return { name: p.me ? "you" : p.name || "a player", color: p.me ? mySeat.color : p.color, mine: !!p.me && active < 0 };
    const k = s ? s.ai.findIndex((a) => a.start === n) : -1;
    if (k >= 0) return { name: `computer ${k + 1}`, color: s.ai[k].color, mine: k === active };
    return null;
  }
  function mapPanel(d) {
    const s = d.settings, v = s && views[s.map];
    const canvas = h("canvas", { class: "mapview-canvas", width: MAP_VIEW, height: MAP_VIEW, role: "img", "aria-label": s ? `Map preview: ${s.map_name}` : "Map preview" });
    const marks = h("div", { class: "startmarks" });
    const note = h("p", { class: "muted small" });
    if (!v || v.error) {
      const c = canvas.getContext("2d"); c.fillStyle = css("--bg"); c.fillRect(0, 0, MAP_VIEW, MAP_VIEW);
      note.textContent = !s ? "" : !v ? "Loading the map…" : `You don't have this map, or it can't be read: ${v.error}`;
    } else {
      const spots = paintMap(canvas, v, (n) => { const o = ownerOf(d, n); return o ? { color: markerColor(o.color), halo: o.mine } : null; });
      // the markers as buttons over the picture, so they can be reached with the keyboard
      fill(marks, spots.map((sp) => {
        const o = ownerOf(d, sp.n), mine = o && o.mine;
        const b = h("button", { type: "button", class: "startmark", "data-key": `start-${sp.n}`, disabled: !!o && !mine,
          "aria-label": `Start ${sp.n}, ${o ? o.name : "free"}`, title: o ? (mine ? `Start ${sp.n}: click to go back to a random start` : `Start ${sp.n}: ${o.name}`) : `Start ${sp.n}: click to take it`,
          onclick: () => takeStart(d, sp.n, mine) });
        b.style.left = `${(sp.x / MAP_VIEW) * 100}%`; b.style.top = `${(sp.y / MAP_VIEW) * 100}%`;
        return b;
      }));
      note.textContent = !v.rgba ? "This map has no preview picture." : "";
    }
    return h("div", { class: "mapcol" }, h("div", { class: "stage" }, h("div", { class: "markwrap" }, canvas, marks)), note);
  }
  function takeStart(d, n, mine) {
    const start = mine ? -1 : n;
    if (active >= 0) { hostAi[active].start = start; sendRoom(d.settings); return; }
    mySeat.start = start; sendSeat();
    render(store.lobby);
  }
  function readyButton() {
    const b = h("button", { type: "button", class: "btn readybtn" + (mySeat.ready ? "" : " primary"), "data-key": "ready", "aria-pressed": String(mySeat.ready), onclick: () => {
      mySeat.ready = !mySeat.ready; cleared = false;
      sendSeat(); render(store.lobby);
    } }, mySeat.ready ? icon("check") : null, mySeat.ready ? "Ready" : "Ready up");
    return b;
  }
  function render(d) {
    const s = d.settings, starts = s ? s.starts : 8;
    const hostName = (d.players.find((p) => p.host) || {}).name || "another player";
    const badge = pill(d.am_host ? "You are the host" : `Host: ${hostName}`, d.am_host ? "ok" : "");
    // the host's choices: map, credits, speed
    let options;
    if (d.am_host) {
      const all = maps || [];
      const cur = s || { map: "", ...ra2RulesNow() };
      const set = (patch) => {
        if ("map" in patch) prefs.set({ ra2Map: patch.map });
        ra2SaveRules(patch);
        sendRoom({ ...cur, ...patch });
      };
      const mapSel = pick("Map", all.map((m) => [fileOf(m.path), `${m.name} (${m.players})`]), cur.map, (v) => set({ map: v }));
      mapSel.disabled = !all.length;
      options = h("div", { class: "mapside" },
        h("label", { class: "field" }, h("span", {}, "Map"), all.length ? mapSel : h("span", { class: "muted small" }, "No skirmish maps yet. Add some on the Maps page.")),
        h("h3", { class: "ruleshead" }, "Rules"), ra2Rules(cur, set));
    } else {
      options = s ? h("div", { class: "mapside" },
        h("div", { class: "stack tight" }, h("h3", {}, s.map_name), h("p", { class: "muted small" }, `${s.starts} starts, chosen by the host`)),
        h("h3", { class: "ruleshead" }, "Rules"), ra2Rules(s, null))
        : h("div", { class: "mapside" }, h("p", { class: "muted small" }, "Waiting for the host to choose a map."));
    }
    const select = (k) => () => { if (d.am_host && active !== k) { active = k; render(store.lobby); } };
    const head = h("div", { class: "skrowhead muted small" }, h("span", {}), h("span", {}, "Player"), h("span", {}, "Side"), h("span", {}, "Colour"), h("span", {}, "Start"), h("span", {}, "Team"), h("span", {}, "Status"));
    const people = d.players.map((p) => {
      const seat = p.me ? mySeat : p;
      const name = h("span", { class: "skwho" }, p.name || "Player", p.me ? h("span", { class: "muted small" }, " (you)") : null);
      const cells = p.me ? seatCells("you", mySeat, starts, (patch) => { Object.assign(mySeat, patch); sendSeat(); render(store.lobby); }) : seatText(p);
      const state = p.host ? pill("Host") : pill(p.ready ? "Ready" : "Not ready", p.ready ? "ok" : "");
      const pickable = d.am_host && p.me;
      return h("div", { class: "skrow2" + (pickable ? "" : " static") + (pickable && active < 0 ? " active" : "") + ra2RowClass(seat.color), onclick: pickable ? select(-1) : null, onfocusin: pickable ? select(-1) : null }, ra2Swatch(seat.color), name, cells, state);
    });
    const ais = (s ? s.ai : []).map((a, k) => {
      const who = d.am_host ? pick(`Computer ${k + 1}`, RA2_DIFFS.map(([v, t]) => [v, `${t} AI`]), a.difficulty, (v) => { hostAi[k].difficulty = v; sendRoom(s); })
        : h("span", { class: "skwho" }, `${(RA2_DIFFS.find(([v]) => v === a.difficulty) || RA2_DIFFS[0])[1]} AI`);
      const cells = d.am_host ? seatCells(`computer ${k + 1}`, a, starts, (patch) => { Object.assign(hostAi[k], patch); sendRoom(s); }) : seatText(a);
      const state = d.am_host ? h("button", { class: "btn small ghost", onclick: (e) => { e.stopPropagation(); hostAi.splice(k, 1); if (active === k) active = -1; sendRoom(s); } }, "Remove") : h("span", {});
      return h("div", { class: "skrow2" + (d.am_host ? "" : " static") + (k === active ? " active" : "") + ra2RowClass(a.color), onclick: d.am_host ? select(k) : null, onfocusin: d.am_host ? select(k) : null }, ra2Swatch(a.color), who, cells, state);
    });
    if (d.am_host && s) hostAi = s.ai.map((a) => ({ ...a }));
    const room = d.players.length + (s ? s.ai.length : 0);
    const addAi = d.am_host && s ? h("button", { class: "btn small", disabled: room >= starts, title: room >= starts ? "Every start on this map is taken" : "", onclick: () => { hostAi.push({ difficulty: "medium", side: -1, color: -1, start: -1, team: -1 }); sendRoom(s); } }, icon("plus"), "Add a computer player") : null;
    let foot;
    if (d.am_host) {
      const start = h("button", { class: "btn primary", disabled: !!d.blocked, onclick: async () => {
        start.disabled = true;
        try { const setup = await invoke("ra2_lobby_start"); await launchRa2(setup); } catch (e) { fb.bad(errText(e)); }
        start.disabled = false;
      } }, icon("play"), "Start the game");
      foot = h("div", { class: "row between lobbyfoot" }, h("span", { class: "muted small" }, d.blocked || "Everyone is ready."), h("div", { class: "row" }, fb.el, start));
    } else {
      const say = mySeat.ready ? "Waiting for the host to start." : cleared ? "The host changed the game. Ready up again when you are set." : s ? "Ready up when you are set." : "Waiting for the host to choose a map.";
      const ready = readyButton();
      ready.disabled = !s;
      foot = h("div", { class: "row between lobbyfoot" }, h("span", { class: cleared && !mySeat.ready ? "small" : "muted small", role: "status" }, say), h("div", { class: "row" }, fb.el, ready));
    }
    // the card is redrawn whenever the room changes: keep the keyboard where it was
    const f = card.contains(document.activeElement) ? document.activeElement : null;
    const key = f && (f.dataset.key || f.getAttribute("aria-label"));
    fill(card,
      h("div", { class: "row between" }, h("h2", {}, "Lobby"), badge),
      h("div", { class: "ra2setup" }, mapPanel(d), options),
      h("div", { class: "skgrid ra2 lobby", role: "group", "aria-label": "Players in the room" }, head, people, ais),
      addAi ? h("div", { class: "row" }, addAi) : null,
      d.players.length < 2 ? h("p", { class: "muted small" }, "Nobody else is here yet. Share the server, room and password.") : null,
      foot);
    const again = key && [...card.querySelectorAll("[data-key], [aria-label]")].find((el) => (el.dataset.key || el.getAttribute("aria-label")) === key);
    if (again) again.focus();
  }
  window.addEventListener("lobby", draw);
  draw();
  return { el: card, dispose: () => window.removeEventListener("lobby", draw) };
}

function ra2DisplayLabel(r, nat) {
  const res = !r.width ? `Screen size ${nat.w} × ${nat.h}` : `${r.width} × ${r.height}`;
  const scale = r.logical_height ? `, ${r.logical_height}p interface` : "";
  return `${res}${scale}, ${r.windowed ? "windowed" : "fullscreen"}`;
}

async function ra2SaveSettings(patch, fb) {
  Object.assign(store.cfg.ra2, patch);
  const c = store.cfg.ra2;
  try {
    await invoke("ra2_set_settings", {
      width: c.width, height: c.height, windowed: c.windowed, side: c.side, color: c.color,
      logical_height: c.logical_height || 0, sound: c.sound !== false,
    });
    if (fb) fb.ok("Saved");
    window.dispatchEvent(new CustomEvent("ra2display"));
  } catch (e) {
    if (fb) fb.bad(errText(e));
    else say(errText(e), "bad");
  }
}

function ra2ResolutionPick(onSave) {
  const r = store.cfg.ra2, nat = nativeRes();
  const sizes = RA2_WINDOW_PRESETS.filter(([w, hh]) => w <= nat.w && hh <= nat.h);
  const extra = r.width && !sizes.some(([w, hh]) => w === r.width && hh === r.height)
    ? [[`${r.width}x${r.height}`, `${r.width} × ${r.height}`]] : [];
  return pick("Window size",
    [["0x0", `Screen size (${nat.w} × ${nat.h})`], ...sizes.map(([w, hh]) => [`${w}x${hh}`, `${w} × ${hh}`]), ...extra],
    `${r.width}x${r.height}`,
    (v) => { const [w, hh] = v.split("x").map(Number); onSave({ width: w, height: hh }); });
}

/** Resolution, windowed and UI-scale rows for Settings or the setup wizard. */
function ra2DisplayFields(fb) {
  const r = store.cfg.ra2;
  const save = (patch) => ra2SaveSettings(patch, fb);
  const res = ra2ResolutionPick(save);
  const win = h("input", { type: "checkbox", checked: r.windowed, "aria-label": "Windowed", onchange: () => save({ windowed: win.checked }) });
  const ui = pick("UI scale", RA2_UI_SCALES, r.logical_height || 0, (v) => save({ logical_height: +v }));
  return h("div", { class: "settings" },
    setting("Window size", "Pick Screen size to match your display.", res),
    setting("Windowed", "Play in a window. Off plays full screen.", h("label", { class: "switch" }, win)),
    setting("Picture size", "Small shows more of the map, Large makes everything bigger.", ui));
}

function ra2DisplayCard(fb) {
  fb = fb || liveStatus();
  return h("div", { class: "card" }, h("div", { class: "row between" }, h("h2", {}, "Display"), fb.el), ra2DisplayFields(fb));
}

/** Game sound on or off. */
function ra2SoundCard() {
  const fb = liveStatus();
  const box = h("input", { type: "checkbox", checked: store.cfg.ra2.sound !== false, "aria-label": "Sound", onchange: () => ra2SaveSettings({ sound: box.checked }, fb) });
  return h("div", { class: "card" }, h("div", { class: "row between" }, h("h2", {}, "Sound"), fb.el),
    h("div", { class: "settings" }, setting("Game sound", "Turn off to play in silence.", h("label", { class: "switch" }, box))));
}

/** Side and colour defaults for skirmish / lobby seats. */
function ra2PlayerDefaultsCard() {
  const r = store.cfg.ra2;
  const fb = liveStatus();
  const save = (patch) => ra2SaveSettings(patch, fb);
  const side = pick("Default side", [[-1, "Random"], ...RA2_SIDES.map((s, i) => [i, s])], r.side, (v) => save({ side: +v }));
  const color = pick("Default colour", [[-1, "Any free one"], ...RA2_COLORS.map((c, i) => [i, c])], r.color, (v) => save({ color: +v }));
  return h("div", { class: "card" }, h("div", { class: "row between" }, h("h2", {}, "You"), fb.el), h("div", { class: "settings" },
    nameSetting(),
    setting("Side", "The side you start with in new games.", side),
    setting("Colour", "If someone has it already, you get the next free one.", color)));
}

/** The lobby name, shared by both games. */
function nameSetting() {
  const box = h("input", { type: "text", maxlength: 12, placeholder: store.cfg.default_player_name || "Player", value: store.cfg.player_name || "", "aria-label": "Player name" });
  const note = h("span", { class: "muted small" });
  const show = (used) => { note.textContent = `Other players see ${used}.`; };
  show(store.cfg.player_name || store.cfg.default_player_name || "your computer's name");
  box.addEventListener("change", async () => {
    try {
      const used = await invoke("set_player_name", { name: box.value });
      store.cfg.player_name = box.value.trim() === "" ? "" : used;
      box.value = store.cfg.player_name;
      show(used);
    } catch (e) { say(errText(e), "bad"); }
  });
  return setting("Name", note, box);
}

function ra2SettingsPage() {
  const files = gameFilesPanel(() => updateSidebar(), "ra2");
  const displayFb = liveStatus();
  const tab = store.settingsTab === "rallypoint" ? "rallypoint" : (store.settingsTab === "display" ? "display" : "game");
  const t = tabbed("Settings", [
    ["game", "Game", () => [h("div", { class: "card" }, h("h2", {}, "Game files"), files.el, forgetSteamRow()), ra2PlayerDefaultsCard(), ra2SoundCard()]],
    ["display", "Display", () => [ra2DisplayCard(displayFb)]],
    ["rallypoint", "Rallypoint", () => [
      updatesCard(),
      h("div", { class: "card" }, h("h2", {}, "Troubleshooting"), firstRunSetupRow()),
      h("p", { class: "muted small about" }, `Rallypoint ${store.cfg.version} on ${store.cfg.os}. Launcher and server are MIT licensed. Not affiliated with or endorsed by Electronic Arts.`)]],
  ], tab, (id) => { store.settingsTab = id; });
  const el = h("div", { class: "page" }, pageHead("Settings", "Game files, screen and updates for Red Alert 2."), t.bar, t.body);
  return { el, dispose: files.dispose };
}

/** Change game: a new shell with that game's pages. Rooms are per game, so the relay is left. The choice is remembered for next launch. */
let switching = false;
async function switchGame(id) {
  if (id === store.cfg.game || switching) return;
  const fromSwitch = !!(document.activeElement && document.activeElement.closest(".gameswitch"));
  try { await invoke("set_game", { game: id }); } catch (e) { say(errText(e), "bad"); return; }
  switching = true;
  // the page steps out, the palette cross-fades (style.css @property tokens), the new game's page steps in
  const still = matchMedia("(prefers-reduced-motion: reduce)").matches;
  const old = document.querySelector(".app");
  const sw = old.querySelector(".gameswitch");
  if (sw) { sw.dataset.cur = id; sw.querySelectorAll(".gseg").forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.game === id))); } // the marker starts sliding now
  if (!still) { old.classList.add("leaving"); await new Promise((r) => setTimeout(r, 150)); }
  store.cfg.game = id;
  document.documentElement.classList.add("swapping"); // the palette fades only on a switch, never at start
  setTimeout(() => document.documentElement.classList.remove("swapping"), 700);
  applyGameTheme();
  store.relay = "Offline"; store.lobby = null; net.last = null; net.hist = [];
  current && current.dispose && current.dispose(); current = null;
  old.remove();
  shell();
  const app = document.querySelector(".app");
  // keep the old switch, so its marker finishes the slide it started instead of jumping
  const fresh = app.querySelector(".gameswitch");
  if (sw && fresh) { sw.dataset.key = fresh.dataset.key; fresh.replaceWith(sw); }
  app.classList.add("entering");
  setTimeout(() => { app.classList.remove("entering"); switching = false; }, still ? 0 : 450);
  go("play");
  if (fromSwitch) { const b = document.querySelector('.gseg[aria-pressed="true"]'); b && b.focus(); }
  // First time in RA2 (or Generals if that wizard was never finished): walk through setup.
  if (needsSetup()) openWizard();
}

/** Whether the current game still needs its first-run wizard. */
function needsSetup() {
  return isRa2() ? !store.cfg.ra2.setup_done : !store.cfg.setup_done;
}

// ---------------------------------------------------------------- shell
const PAGES = { play: ["Play", "play", playPage], mods: ["Mods", "mods", modsPage], maps: ["Maps", "maps", mapsPage], online: ["Online", "online", onlinePage], settings: ["Settings", "settings", settingsPage] };
const RA2_PAGES = { play: ["Play", "play", ra2PlayPage], maps: ["Maps", "maps", ra2MapsPage], online: ["Online", "online", onlinePage], settings: ["Settings", "settings", ra2SettingsPage] };
const pages = () => (isRa2() ? RA2_PAGES : PAGES);
let current = null, sideFiles, sideNet;

function go(name) {
  if (!pages()[name]) name = "play";
  store.page = name;
  current && current.dispose && current.dispose();
  pageStatus = liveStatus();
  current = pages()[name][2]();
  const head = current.el.querySelector(".page-head");
  if (head) head.after(h("div", { class: "page-status" }, pageStatus.el));
  document.getElementById("pagehost").replaceChildren(current.el);
  document.getElementById("main").dataset.page = name; // the page's own colour (style.css: main[data-page])
  document.querySelectorAll(".nav").forEach((n) => (n.dataset.page === name ? n.setAttribute("aria-current", "page") : n.removeAttribute("aria-current")));
}

function updateSidebar() {
  if (!sideFiles) return;
  const sw = document.querySelector(".gameswitch");
  if (sw && sw.dataset.key !== railKey()) sw.replaceWith(gameSwitch());
  const dir = isRa2() ? store.cfg.ra2.game_dir : store.cfg.game_dir;
  sideFiles.replaceChildren(status(dir ? "Game files ready" : "Game files needed", dir ? "ok" : "warn"));
  const s = store.relay;
  // under "Online": how the connection is doing, as three small readings
  const d = net.last, on = !!d && s.startsWith("Connected as");
  const lvl = (v, warn, bad) => (v == null ? "" : v > bad ? "bad" : v > warn ? "warn" : "ok");
  const reading = (value, unit, label, level, hint) => h("span", { class: "reading " + level, title: hint }, h("b", {}, value, h("i", {}, unit)), h("span", {}, label));
  const meta = on ? h("div", { class: "netstats" },
    reading(d.rtt_ms == null ? "…" : String(d.rtt_ms), d.rtt_ms == null ? "" : " ms", "ping", lvl(d.rtt_ms, 60, 120), "Ping to the server"),
    reading(d.server ? String(d.server.room_players) : "…", "", "players", "", "Players in your room"),
    reading(d.loss_pct == null ? "…" : String(d.loss_pct), d.loss_pct == null ? "" : "%", "loss", lvl(d.loss_pct, 0, 2), "Packets that got no answer")) : null;
  fill(sideNet, status(s.startsWith("Connected as") ? "Online" : s === "Offline" ? "Offline" : s === "Connecting..." ? "Connecting…" : "Not connected", s.startsWith("Connected as") ? "ok" : s === "Offline" || s === "Connecting..." ? "" : "bad"), meta);
}
window.addEventListener("net", () => updateSidebar());

// ---------------------------------------------------------------- the game card (who you are playing, and the one place to change it)
const GAME_INFO = {
  generals: { name: "Generals Zero Hour", kicker: "Generals", short: "Zero Hour", label: "Zero Hour", abbr: "ZH", key: "1" },
  ra2: { name: "Red Alert 2", kicker: "Yuri's Revenge", short: "Red Alert 2", label: "Red Alert 2", abbr: "RA2", key: "2" },
};
const gameReady = (id) => !!(id === "ra2" ? store.cfg.ra2.game_dir : store.cfg.game_dir);
/** The game's emblem: Generals keeps the Rallypoint badge, Red Alert 2 gets a star on its own blue. */
function gameTile(id) { return h("span", { class: "gtile t-" + id, "aria-hidden": "true" }, id === "ra2" ? "RA2" : h("img", { src: "logo.png", alt: "" })); }
const shortcutLabel = (id) => `${/Mac|iPhone|iPad/.test(navigator.platform) ? "\u2318" : "Ctrl+"}${GAME_INFO[id].key}`;
const railKey = () => `${store.cfg.game}${gameReady("generals")}${gameReady("ra2")}`;
/** Two games, both always in view: a two-part switch under the brand. The pressed one is the game on screen. */
function gameSwitch() {
  const cur = store.cfg.game in GAME_INFO ? store.cfg.game : "generals";
  return h("div", { class: "gameswitch", role: "group", "aria-label": "Game", "data-key": railKey(), "data-cur": cur }, Object.keys(GAME_INFO).map((id) => {
    const ready = gameReady(id), i = GAME_INFO[id];
    return h("button", { class: "gseg", type: "button", "data-game": id, "aria-pressed": String(id === cur),
      "aria-label": `${i.name}${ready ? "" : ", needs game files"}`, title: `${i.name} (${shortcutLabel(id)})${ready ? "" : ". Needs game files"}`,
      onclick: () => switchGame(id) },
      h("span", { class: "long" }, i.label), h("span", { class: "short", "aria-hidden": "true" }, i.abbr));
  }));
}
/** First run: which game is this setup for? Picking the other one swaps to its own setup. */
function wizardGamePick(cur) {
  return h("div", { class: "stack tight" }, h("p", { class: "muted small" }, "Playing both? Set up one now, the other any time from the switch at the top of the sidebar."),
    h("div", { class: "wizpick", role: "radiogroup", "aria-label": "Which game are you setting up?" }, Object.keys(GAME_INFO).map((id) =>
      h("button", { class: "gameopt", type: "button", role: "radio", "aria-checked": String(id === cur), onclick: () => { if (id !== cur) { closeWizard(); switchGame(id); } } },
        gameTile(id), h("span", { class: "gtext" }, h("small", {}, GAME_INFO[id].kicker), h("b", {}, GAME_INFO[id].short))))));
}
/** Ready step: offer the other game's setup when it has not been done. */
function otherGameOffer(finish) {
  const other = isRa2() ? "generals" : "ra2";
  const done = other === "ra2" ? store.cfg.ra2.setup_done : store.cfg.setup_done;
  return done ? null : h("p", { class: "muted small" }, `Also play ${GAME_INFO[other].name}? `, h("button", { class: "linkbtn", onclick: () => finish(() => switchGame(other)) }, "Set it up next"), ".");
}
/** Ctrl or Command with 1 or 2 changes the game from anywhere. */
document.addEventListener("keydown", (e) => {
  if (!(e.ctrlKey || e.metaKey) || e.altKey || e.shiftKey || wizardEl || !store.cfg) return;
  const id = Object.keys(GAME_INFO).find((g) => GAME_INFO[g].key === e.key);
  if (id) { e.preventDefault(); switchGame(id); }
});

function shell() {
  sideFiles = h("div"); sideNet = h("div");
  const nav = Object.entries(pages()).map(([id, [label, ic]]) => h("button", { class: "nav", "data-page": id, onclick: () => go(id) }, icon(ic), h("span", {}, label)));
  document.getElementById("root").append(h("div", { class: "app" },
    h("aside", { class: "sidebar" },
      h("div", { class: "brand" }, h("img", { src: "logo.png", alt: "" }), h("div", { class: "brand-text" }, h("b", {}, "Rallypoint"), h("span", {}, "Command & Conquer"))), gameSwitch(),
      nav, h("div", { class: "spacer" }), h("div", { class: "side-foot" }, sideFiles, sideNet, h("div", { class: "side-version", title: "The version of Rallypoint you are running" }, `Rallypoint ${store.cfg.version}`))),
    h("main", { id: "main" }, h("div", { id: "updatehost" }), h("div", { id: "pagehost" }))));
  updateSidebar();
}

// ---------------------------------------------------------------- first-run wizard
let wizardEl = null;
function closeWizard() { wizardEl && wizardEl.remove(); wizardEl = null; }

function openWizard() {
  closeWizard();
  if (isRa2()) return openRa2Wizard();
  const steps = ["Welcome", "Game files", "Display", "Mods", "Maps", "Ready"];
  const w = { i: 0, filesOk: !!store.cfg.game_dir, dispose: null };
  const overlay = h("div", { class: "overlay", role: "dialog", "aria-modal": "true", "aria-label": "Set up Rallypoint" });
  wizardEl = overlay;
  document.getElementById("root").append(overlay);

  const finish = async (then) => {
    await invoke("set_setup_done", { done: true }).catch(() => {});
    store.cfg.setup_done = true;
    w.dispose && w.dispose();
    closeWizard();
    go("play");
    updateSidebar();
    then && then();
  };

  function body(step) {
    w.dispose = null;
    if (step === 0) {
      return h("div", { class: "stack" }, h("div", { class: "intro" }, h("img", { src: "logo.png", alt: "" }),
        h("div", { class: "stack" }, h("h1", {}, "Set up Rallypoint"),
          h("p", { class: "muted" }, "Rallypoint runs Command & Conquer: Generals – Zero Hour on your Mac or PC, at any screen size. It needs your own copy of the game. This takes about two minutes and everything can be changed later."),
          h("ol", { class: "checklist" }, h("li", {}, "Find or download your game files"), h("li", {}, "Choose your screen size"), h("li", {}, "Add mods and maps, if you want them")))), wizardGamePick("generals"));
    }
    if (step === 1) {
      const panel = gameFilesPanel((ok) => { w.filesOk = ok; nav(); });
      w.dispose = panel.dispose;
      return h("div", { class: "stack" }, h("h2", {}, "Your game files"), h("p", { class: "muted" }, "Rallypoint needs your own copy of Zero Hour (the folder with INIZH.big). If you have it on Steam on Windows it's found automatically; otherwise point to it or download it below."), panel.el);
    }
    if (step === 2) {
      const nat = nativeRes(), p = prefs.get();
      const resSel = h("select", { "aria-label": "Resolution", onchange: () => prefs.set({ res: resSel.value }) },
        new Option(`Native (${nat.w} × ${nat.h})`, "native"), ...[[3840, 2160], [2560, 1440], [1920, 1080], [1600, 900], [1280, 720]].filter(([x]) => x < nat.w).map(([x, y]) => new Option(`${x} × ${y}`, `${x}x${y}`)));
      resSel.value = [...resSel.options].some((o) => o.value === p.res) ? p.res : "native";
      const fs = h("input", { type: "checkbox", checked: p.fs, onchange: () => prefs.set({ fs: fs.checked }) });
      const cam = h("input", { type: "checkbox", checked: !!p.camera, onchange: () => prefs.set({ camera: cam.checked }) });
      const haveIcons = store.mods.other.some((o) => o.folder === ICON_FOLDER);
      const note = h("p", { class: "muted small" }, haveIcons ? "Sharp icons installed. They load with the game." : "Downloading sharp command-bar icons…");
      if (!haveIcons) ensureIcons().then(() => { note.textContent = "Sharp icons installed. They load with the game."; }).catch((e) => { note.textContent = errText(e); });
      return h("div", { class: "stack" }, h("h2", {}, "Display"),
        h("p", { class: "muted" }, "Rallypoint works at any resolution, including 4K and ultrawide. The camera keeps a normal view distance and the menus are scaled, not stretched. Sharp command-bar icons are downloaded for you."),
        h("label", { class: "field" }, h("span", {}, "Resolution"), resSel),
        h("label", { class: "switch" }, fs, "Fullscreen"),
        h("label", { class: "switch" }, cam, "Higher camera"),
        note);
    }
    if (step === 3) {
      const box = h("div", { class: "stack" });
      const render = () => fill(box, store.mods.catalog.map((m) => modCard(m, true)));
      refreshers.add(render); render();
      w.dispose = () => refreshers.delete(render);
      return h("div", { class: "stack" }, h("h2", {}, "Mods (optional)"), h("p", { class: "muted" }, "Mods change the game. Get one now, or add any later from the Mods tab, where you can also drop in your own."), box);
    }
    if (step === 4) {
      const panel = mapsCard();
      w.dispose = panel.dispose;
      return h("div", { class: "stack" }, h("h2", {}, "Maps (optional)"),
        h("p", { class: "muted" }, "Zero Hour comes with a few maps. These packs from GenTool's download page add the ranked 1v1, 2v2 and 3v3 maps that most online players use. You can add more any time from the Mods tab."), panel.el);
    }
    const pr = prefs.get(), nat = nativeRes();
    return h("div", { class: "stack" }, h("h1", {}, "Ready to play"),
      h("dl", { class: "summary" },
        h("dt", {}, "Game files"), h("dd", {}, store.cfg.game_dir || "not set yet"),
        h("dt", {}, "Display"), h("dd", {}, (pr.res === "native" ? `Native ${nat.w} × ${nat.h}` : pr.res.replace("x", " × ")) + (pr.fs ? ", fullscreen" : ", windowed") + (store.mods.other.some((o) => o.folder === ICON_FOLDER) ? ", sharp icons" : "") + (pr.camera ? ", higher camera" : "")),
        h("dt", {}, "Game"), h("dd", {}, [selectedGame().name, ...selectedExtras()].join(", ")),
        h("dt", {}, "Maps"), h("dd", {}, store.maps && store.maps.count ? `${store.maps.count} installed` : "none added")),
      h("p", { class: "muted small" }, "To play with friends, open the Online tab and enter a server and room."), otherGameOffer(finish),
      h("div", { class: "row" }, h("button", { class: "btn primary big", onclick: () => finish() }, icon("play"), "Open Rallypoint"), h("button", { class: "btn big", onclick: () => finish(() => go("mods")) }, "Browse mods")));
  }

  let nextBtn, backBtn;
  function nav() {
    if (!nextBtn) return;
    nextBtn.disabled = w.i === 1 && !w.filesOk;
    nextBtn.textContent = ["Start setup", "Use these game files", "Continue to mods", "Continue to maps", "Review"][w.i] || "Continue";
    backBtn.hidden = w.i === 0;
  }
  function render() {
    w.dispose && w.dispose();
    nextBtn = h("button", { class: "btn primary", onclick: () => { w.i++; render(); } }, "Continue");
    backBtn = h("button", { class: "btn", onclick: () => { w.i--; render(); } }, "Back");
    const last = w.i === steps.length - 1;
    overlay.replaceChildren(h("div", { class: "wizard" },
      h("div", { class: "wiz-head" }, h("div", { class: "row between" }, h("span", { class: "muted small" }, `Step ${w.i + 1} of ${steps.length}: ${steps[w.i]}`), last ? null : h("button", { class: "btn small ghost", onclick: () => finish() }, "Skip setup")),
        h("div", { class: "chevs", role: "img", "aria-label": `Step ${w.i + 1} of ${steps.length}` }, steps.map((_, k) => h("i", { class: k < w.i ? "done" : k === w.i ? "now" : "" })))),
      h("div", { class: "wiz-body" }, body(w.i)),
      h("div", { class: "wiz-foot" }, backBtn, last ? h("span") : nextBtn)));
    nav();
  }
  render();
}

/** First run for Red Alert 2: same shape as the Generals wizard (welcome, files, display, maps, ready). */
function openRa2Wizard() {
  const steps = ["Welcome", "Game files", "Display", "Maps", "Ready"];
  const w = { i: 0, filesOk: !!store.cfg.ra2.game_dir, dispose: null, mapsNote: "" };
  const overlay = h("div", { class: "overlay", role: "dialog", "aria-modal": "true", "aria-label": "Set up Red Alert 2" });
  wizardEl = overlay;
  document.getElementById("root").append(overlay);

  const finish = async (then) => {
    await invoke("set_ra2_setup_done", { done: true }).catch(() => {});
    store.cfg.ra2.setup_done = true;
    w.dispose && w.dispose();
    closeWizard();
    go("play");
    updateSidebar();
    then && then();
  };

  function body(step) {
    w.dispose = null;
    if (step === 0) {
      return h("div", { class: "stack" }, h("div", { class: "intro" }, gameTile("ra2"),
        h("div", { class: "stack" }, h("h1", {}, `Set up ${RA2_NAME}`),
          h("p", { class: "muted" }, "Needs your own copy of the game. About a minute; change anything later in Settings."),
          h("ol", { class: "checklist" },
            h("li", {}, "Find or download your game files"),
            h("li", {}, "Choose your screen size"),
            h("li", {}, "Add community maps (optional)")))), wizardGamePick("ra2"));
    }
    if (step === 1) {
      const panel = gameFilesPanel((ok) => { w.filesOk = ok; nav(); }, "ra2");
      w.dispose = panel.dispose;
      return h("div", { class: "stack" }, h("h2", {}, "Your game files"),
        h("p", { class: "muted" }, "Choose the folder with your game, or download it from Steam below. Steam and EA installs are found for you."), panel.el);
    }
    if (step === 2) {
      return h("div", { class: "stack" }, h("h2", {}, "Display"), ra2DisplayFields(null));
    }
    if (step === 3) {
      const note = h("p", { class: "muted small", role: "status" }, w.mapsNote);
      const install = h("button", { class: "btn primary", onclick: async () => {
        install.disabled = true;
        note.textContent = "Installing…";
        try {
          const [packs, files] = await invoke("ra2_install_community_packs");
          w.mapsNote = `Installed ${packs} pack${packs === 1 ? "" : "s"} (${files} maps).`;
          note.textContent = w.mapsNote;
          say(w.mapsNote, "ok");
        } catch (e) {
          w.mapsNote = errText(e);
          note.textContent = w.mapsNote;
          say(w.mapsNote, "bad");
        } finally { install.disabled = false; }
      } }, "Install community packs");
      invoke("ra2_community_packs").then((c) => {
        if (!c || !c.available) { note.textContent = "No packs in this build."; return; }
        const total = (c.packs || []).reduce((n, p) => n + (p.maps || 0), 0);
        if (!w.mapsNote) {
          note.textContent = c.installed_files
            ? `${c.installed_files} already installed.`
            : `${(c.packs || []).length} packs · ${total} maps`;
        }
      }).catch(() => { note.textContent = "Packs not available in this build."; });
      return h("div", { class: "stack" }, h("h2", {}, "Maps (optional)"),
        h("p", { class: "muted" }, "Extra classic multiplayer maps. Skip if you only want the ones that came with the game."),
        h("div", { class: "row" }, install), note);
    }
    const r = store.cfg.ra2;
    return h("div", { class: "stack" }, h("h1", {}, "Ready to play"),
      h("dl", { class: "summary" },
        h("dt", {}, "Game files"), h("dd", {}, r.game_dir || "not set yet"),
        h("dt", {}, "Display"), h("dd", {}, ra2DisplayLabel(r, nativeRes())),
        h("dt", {}, "Maps"), h("dd", {}, w.mapsNote || "add packs later from Maps")), otherGameOffer(finish),
      h("div", { class: "row" },
        h("button", { class: "btn primary big", onclick: () => finish() }, icon("play"), "Open Rallypoint"),
        h("button", { class: "btn big", onclick: () => finish(() => go("maps")) }, "Browse maps")));
  }

  let nextBtn, backBtn;
  function nav() {
    if (!nextBtn) return;
    nextBtn.disabled = w.i === 1 && !w.filesOk;
    nextBtn.textContent = ["Start setup", "Use these game files", "Continue to maps", "Review"][w.i] || "Continue";
    backBtn.hidden = w.i === 0;
  }
  function render() {
    w.dispose && w.dispose();
    nextBtn = h("button", { class: "btn primary", onclick: () => { w.i++; render(); } }, "Continue");
    backBtn = h("button", { class: "btn", onclick: () => { w.i--; render(); } }, "Back");
    const last = w.i === steps.length - 1;
    overlay.replaceChildren(h("div", { class: "wizard" },
      h("div", { class: "wiz-head" }, h("div", { class: "row between" },
        h("span", { class: "muted small" }, `Step ${w.i + 1} of ${steps.length}: ${steps[w.i]}`),
        last ? null : h("button", { class: "btn small ghost", onclick: () => finish() }, "Skip setup")),
        h("div", { class: "chevs", role: "img", "aria-label": `Step ${w.i + 1} of ${steps.length}` },
          steps.map((_, k) => h("i", { class: k < w.i ? "done" : k === w.i ? "now" : "" })))),
      h("div", { class: "wiz-body" }, body(w.i)),
      h("div", { class: "wiz-foot" }, backBtn, last ? h("span") : nextBtn)));
    nav();
  }
  render();
}

/** Release notes are markdown. Render the small subset GitHub notes use, as elements, never as HTML. */
function renderNotes(text) {
  const body = (text || "").trim();
  const root = h("div", { class: "notes" });
  if (!body) {
    root.append(h("p", {}, "No release notes were published for this version."));
    return root;
  }
  function inline(s) {
    const out = [];
    const re = /\*\*([^*]+)\*\*|`([^`]+)`/g;
    let i = 0, m;
    while ((m = re.exec(s))) {
      if (m.index > i) out.push(s.slice(i, m.index));
      out.push(m[1] != null ? h("strong", {}, m[1]) : h("code", {}, m[2]));
      i = m.index + m[0].length;
    }
    if (i < s.length) out.push(s.slice(i));
    return out;
  }
  let list = null;
  for (const line of body.split("\n")) {
    const heading = /^(#{1,3})\s+(.*)$/.exec(line);
    const item = /^\s*[-*]\s+(.*)$/.exec(line);
    if (/^\s*---\s*$/.test(line)) { list = null; root.append(h("hr")); continue; }
    if (heading) { list = null; root.append(h("h3", {}, ...inline(heading[2]))); continue; }
    if (item) {
      if (!list) { list = h("ul"); root.append(list); }
      list.append(h("li", {}, ...inline(item[1])));
      continue;
    }
    if (!line.trim()) { list = null; continue; }
    list = null;
    root.append(h("p", {}, ...inline(line)));
  }
  return root;
}

/** Shown once, on the first start after an update. */
function showWhatsNew(w) {
  const close = () => overlay.remove();
  const overlay = h("div", { class: "overlay", role: "dialog", "aria-modal": "true", "aria-label": `What's new in ${w.version}` },
    h("div", { class: "wizard whatsnew" },
      h("div", { class: "wiz-head" }, h("h2", {}, `Rallypoint ${w.version}`), h("p", { class: "muted small" }, "You're up to date. Here's what changed.")),
      h("div", { class: "wiz-body" }, renderNotes(w.notes)),
      h("div", { class: "wiz-foot" }, h("span"), h("button", { class: "btn primary", onclick: close }, "Continue"))));
  document.getElementById("root").append(overlay);
}

/** The release notes shipped with this version (the last three releases), opened from Settings > Updates. */
function showChangelog(notes, title = "Changelog") {
  const close = () => overlay.remove();
  const overlay = h("div", { class: "overlay", role: "dialog", "aria-modal": "true", "aria-label": title },
    h("div", { class: "wizard whatsnew" },
      h("div", { class: "wiz-head" }, h("h2", {}, title)),
      h("div", { class: "wiz-body" }, renderNotes(notes)),
      h("div", { class: "wiz-foot" }, h("span"), h("button", { class: "btn primary", onclick: close }, "Close"))));
  document.getElementById("root").append(overlay);
}

// ---------------------------------------------------------------- start
async function start() {
  store.cfg = await invoke("get_config");
  store.games = await invoke("detect_games").catch(() => []);
  store.ra2Games = await invoke("ra2_detect").catch(() => []);
  applyGameTheme();
  shell();
  await loadMods();
  if (!isRa2()) ensureIcons().catch((e) => { if (!wizardEl) toast(errText(e), "bad"); });
  await loadMaps();
  go("play");
  setInterval(pollNet, 2000);
  pollNet();
  setInterval(async () => {
    try { store.relay = await invoke("relay_status"); } catch { /* keep the last status */ }
    updateSidebar();
    notifyOnlineOnly();
    await pollLobby();
  }, 1000);
  if (needsSetup()) openWizard();
  if (store.cfg.check_updates !== false) checkUpdate();
  // A launcher left open still hears about a release: look again every six hours.
  setInterval(() => { if (store.cfg.check_updates !== false) checkUpdate(true); }, 6 * 3600 * 1000);
  invoke("whats_new_take").then((w) => { if (w) showWhatsNew(w); }).catch(() => {});

  // Dropping a file or folder on the window installs it as a mod.
  try {
    tauri.webview.getCurrentWebview().onDragDropEvent((e) => {
      const z = document.getElementById("dropzone");
      if (e.payload.type === "over" && z) z.classList.add("over");
      else if (e.payload.type === "drop" && !isRa2()) {
        z && z.classList.remove("over");
        if (store.page !== "mods") go("mods");
        (e.payload.paths || []).forEach(installPath);
      } else z && z.classList.remove("over");
    });
  } catch { /* drag and drop is a convenience; the path box still works */ }
}
/** The RA2 lobby, once a second while connected in RA2 mode; a start from the host launches the game here too. */
async function pollLobby() {
  let d = null;
  if (isRa2() && store.relay.startsWith("Connected as")) { try { d = await invoke("ra2_lobby_poll"); } catch { d = null; } }
  store.lobby = d;
  window.dispatchEvent(new Event("lobby"));
  if (d && d.start) launchRa2(d.start);
}
function notifyOnlineOnly() { if (store.page === "online") refreshers.forEach((f) => f.name === "refresh" && f()); }

start().catch((e) => document.getElementById("root").append(h("pre", { class: "log" }, "Couldn't start: " + errText(e))));
