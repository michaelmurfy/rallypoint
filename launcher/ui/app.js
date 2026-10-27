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
  mods: "M12 2 2 7l10 5 10-5-10-5zm0 11.5L4.6 9.8 2 11l10 5 10-5-2.6-1.2L12 13.5zm0 4-7.4-3.7L2 15l10 5 10-5-2.6-1.2L12 17.5z",
  online: "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zm6.9 6h-3a15.7 15.7 0 0 0-1.4-3.6A8 8 0 0 1 18.9 8zM12 4c.8 1.2 1.5 2.5 1.9 4h-3.8c.4-1.5 1.1-2.8 1.9-4zM4.3 14a8.200 8.200 0 0 1 0-4h3.400a16.500 16.500 0 0 0 0 4H4.300zm.8 2h3a15.700 15.700 0 0 0 1.400 3.600A8 8 0 0 1 5.100 16zm3-8h-3a8 8 0 0 1 4.400-3.600A15.700 15.700 0 0 0 8.100 8zM12 20c-.8-1.200-1.500-2.500-1.900-4h3.800c-.4 1.500-1.100 2.800-1.900 4zm2.300-6H9.700a14.700 14.700 0 0 1 0-4h4.600a14.700 14.700 0 0 1 0 4zm.2 5.600c.6-1.100 1.100-2.300 1.400-3.600h3a8 8 0 0 1-4.400 3.600zm1.800-5.600a16.500 16.500 0 0 0 0-4h3.400a8.200 8.200 0 0 1 0 4h-3.400z",
  maps: "M20.5 3l-.2.1L15 5.2 9 3 3.400 4.900a.5.5 0 0 0-.4.5v15.100a.5.5 0 0 0 .5.5l.2-.1L9 18.800l6 2.100 5.600-1.900a.5.5 0 0 0 .4-.5V3.500a.5.5 0 0 0-.5-.5zM15 19l-6-2.100V5l6 2.100V19z",
  settings: "M19.400 13a7.500 7.500 0 0 0 0-2l2.100-1.600-2-3.400-2.500 1a7.600 7.600 0 0 0-1.700-1L15 3.300h-4L10.700 6a7.600 7.600 0 0 0-1.700 1l-2.500-1-2 3.400L6.600 11a7.500 7.500 0 0 0 0 2l-2.100 1.600 2 3.400 2.500-1c.5.400 1.100.7 1.700 1l.3 2.700h4l.3-2.700c.6-.3 1.200-.6 1.700-1l2.500 1 2-3.400L19.400 13zM13 15.500a3.500 3.500 0 1 1 0-7 3.500 3.500 0 0 1 0 7z",
  check: "M9 16.200 4.800 12l-1.400 1.400L9 19 21 7l-1.400-1.400z",
  plus: "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6z",
  prev: "M15.400 7.400 14 6l-6 6 6 6 1.400-1.400L10.800 12z",
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

const errText = (e) => (typeof e === "string" ? e : (e && e.message) || String(e));

// ---------------------------------------------------------------- state
const store = { cfg: null, games: [], mods: { catalog: [], other: [] }, relay: "Offline", page: "play", watch: null, update: null, updateNote: "", maps: null };
const nativeRes = () => ({ w: Math.round(screen.width * devicePixelRatio), h: Math.round(screen.height * devicePixelRatio) });

const ICON_FOLDER = "Control Bar HD";
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
const extraMods = () => store.mods.other.filter((o) => o.folder !== ICON_FOLDER).map((o) => ({ folder: o.folder, name: o.folder, mb: o.mb }));
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
function launchFolders() {
  const g = selectedGame();
  return [...(store.mods.other.some((o) => o.folder === ICON_FOLDER) ? [ICON_FOLDER] : []), ...(g.folder ? [g.folder] : []), ...selectedExtras()];
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

function renderUpdate() {
  const host = document.getElementById("updatehost");
  if (!host) return;
  const u = store.update;
  if (!u || store.updateDismissed) return fill(host);
  const install = h("button", { class: "btn primary", onclick: async () => {
    install.disabled = true;
    try { store.updateNote = await runUpdate(); } catch (e) { store.updateNote = errText(e); }
    install.disabled = false;
    renderUpdate();
  } }, "Update and restart");
  fill(host, h("div", { class: "banner stack updatebar" },
    h("div", {}, h("b", {}, `Rallypoint ${u.latest} is available`),
      h("p", { class: "muted small" }, store.updateNote || `You have ${u.current}.${u.size_mb ? ` The download is about ${u.size_mb} MB.` : ""}`)),
    h("div", { class: "row" },
      u.installer ? install : null,
      h("button", { class: u.installer ? "btn" : "btn primary", onclick: () => invoke("open_release_page").catch((e) => say(errText(e), "bad")) }, "What's new"),
      h("button", { class: "btn ghost", onclick: () => { store.updateDismissed = true; renderUpdate(); } }, "Later"))));
}
/** Ask GitHub for a newer release. Returns an error message, or null. The startup check stays quiet unless there is an update. */
async function checkUpdate() {
  try { store.update = await invoke("update_check"); } catch (e) { return "Couldn't check for updates: " + errText(e); }
  store.updateDismissed = false;
  store.updateNote = "";
  renderUpdate();
  return null;
}

/** Settings card: Check now with the answer beside it, and the install actions once there is something to install. */
/** One line of a settings card: what it is and what it does on the left, its control on the right. */
function setting(title, desc, ...controls) {
  return h("div", { class: "setting" },
    h("div", { class: "setting-text" }, h("div", { class: "setting-title" }, title), desc ? h("div", { class: "muted small" }, desc) : null),
    h("div", { class: "setting-ctl" }, ...controls));
}

function updatesCard() {
  const fb = liveStatus();
  const actions = h("div", { class: "row end" });
  const auto = h("input", { type: "checkbox", "aria-label": "Check for updates when Rallypoint starts", checked: store.cfg.check_updates !== false, onchange: () => { store.cfg.check_updates = auto.checked; invoke("set_check_updates", { on: auto.checked }); } });
  const install = h("button", { class: "btn primary", onclick: async () => {
    install.disabled = true;
    try { fb.ok(await runUpdate()); } catch (e) { fb.bad(errText(e)); }
    install.disabled = false;
  } }, "Update and restart");
  const notes = h("button", { class: "btn", onclick: () => invoke("open_release_page").catch((e) => fb.bad(errText(e))) }, "What's new");
  const check = h("button", { class: "btn", onclick: async () => {
    check.disabled = true;
    fill(actions);
    fb.busy("Checking…");
    const err = await checkUpdate();
    check.disabled = false;
    if (err) fb.bad(err); else report();
  } }, "Check now");
  function report() {
    const u = store.update;
    fb.set(u ? `Rallypoint ${u.latest} is available${u.size_mb ? ` (${u.size_mb} MB)` : ""}` : "You have the latest version", "ok");
    fill(actions, u && u.installer ? install : null, u ? notes : null);
  }
  if (store.update) report();
  return h("div", { class: "card" }, h("h2", {}, "Updates"),
    setting(`Rallypoint ${store.cfg.version}`, fb.el, check),
    actions,
    setting("Check when Rallypoint starts", null, h("label", { class: "switch" }, auto)));
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
function mapsCard() {
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
  function render() {
    const m = store.maps;
    if (!m) return;
    info.textContent = m.count ? `${m.count} map${m.count === 1 ? "" : "s"} installed in ${m.dir}` : "No extra maps installed yet.";
    fill(list, m.packs.map((p) => {
      const row = h("div", { class: "mappack" },
        h("div", {}, h("h3", {}, p.name), h("p", { class: "muted small" }, `${p.description} About ${p.size_mb} MB.`)),
        p.manual
          ? h("div", { class: "row" },
            h("button", { class: "btn small", onclick: () => invoke("maps_open_pack_page", { id: p.id }).catch((e) => say(errText(e), "bad")) }, "Open download page"),
            h("button", { class: "btn small", onclick: () => { active = p.id; render(); pickMaps(false, p.id); } }, "Choose the file…"))
          : h("button", { class: "btn small", onclick: () => start(p) }, "Install"));
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
  const el = h("div", { class: "stack" }, info, list, strip,
    h("div", { class: "row" },
      h("button", { class: "btn small", onclick: () => { active = null; render(); pickMaps(false); } }, "Add maps from a file…"),
      h("button", { class: "btn small", onclick: () => { active = null; render(); pickMaps(true); } }, "Add a folder of maps…")));
  return { el, dispose: () => { clearInterval(timer); refreshers.delete(render); mapsNotice = null; } };
}

/** The map picture's side in the preview window, in pixels (also in style.css: .mapview-canvas). */
const MAP_VIEW = 400;
/** One colour per place in a skirmish: on its row's swatch (style.css .pc0 to .pc7) and on its start marker. */
const PLACE_COLORS = ["#4d8dff", "#e5484d", "#46a758", "#f5c542", "#f08c3c", "#9a6bdc", "#3dc7c0", "#e5509b"];

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
  const close = h("button", { class: "btn", onclick: () => finish() }, "Close");

  // The game (Zero Hour or a mod) the skirmish is played with. Changing it changes the generals on offer.
  const gameSel = h("select", { "aria-label": "Game", onchange: async () => { prefs.set({ game: gameSel.value }); notifyMods(); await loadGenerals(); drawTable(); paint(shown); } });
  function drawGames() {
    const cur = selectedGame();
    gameSel.replaceChildren(...gameChoices().map((g) => { const o = new Option(g.installed ? g.name : `${g.name} (not installed)`, g.id); o.disabled = !g.installed; return o; }));
    gameSel.value = cur.id;
  }
  async function loadGenerals() {
    try { generalsList = await invoke("skirmish_generals", { gameDir: store.cfg.game_dir, modFolders: launchFolders() }); } catch { generalsList = []; }
    // a general the new game does not have goes back to random
    for (const r of rows) if (r.faction && !generalsList.some((g) => g.id === r.faction)) r.faction = null;
  }

  // The table: you first, then one row per other place on the map.
  const table = h("div", { class: "skgrid", role: "group", "aria-label": "Skirmish players" });
  const summary = h("span", { class: "muted small" });
  const diffs = [["easy", "Easy AI"], ["medium", "Medium AI"], ["brutal", "Brutal AI"]];
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
        h("div", { class: "mapside" }, h("div", { class: "sidehead" }, h("h3", {}, "Players"), summary), table, note, play)),
      h("div", { class: "wiz-foot" }, h("label", { class: "switch" }, listed, "Show in the game's map list"), close)));
  const onKey = (e) => { if (e.target.tagName === "SELECT") return; if (e.key === "Escape") finish(); else if (e.key === "ArrowLeft") go(-1); else if (e.key === "ArrowRight") go(1); };
  function finish() { document.removeEventListener("keydown", onKey); overlay.remove(); }
  function go(d) { const n = i + d; if (n >= 0 && n < entries.length) { i = n; show(); } }
  const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

  // Where the start markers are on the canvas, for drawing them and for clicks.
  let spots = [];
  function paint(v) {
    if (!v) return;
    shown = v;
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
    spots = [];
    if (info) {
      ctx.font = "700 13px system-ui, sans-serif"; ctx.textAlign = "center"; ctx.textBaseline = "middle";
      for (const st of info.starts) {
        const x = ox + st.x * cw, y = oy + (1 - st.y) * ch;
        const owner = rows.findIndex((r) => r.kind !== "closed" && r.start === st.number);
        spots.push({ x, y, n: st.number });
        const taken = owner >= 0, mine = owner === active;
        // a taken start wears its place's colour; the selected place's start also gets a halo
        if (taken && mine) { ctx.beginPath(); ctx.arc(x, y, 21, 0, Math.PI * 2); ctx.fillStyle = PLACE_COLORS[owner]; ctx.globalAlpha = .3; ctx.fill(); ctx.globalAlpha = 1; }
        ctx.beginPath(); ctx.arc(x, y, taken ? 14 : 11, 0, Math.PI * 2);
        ctx.fillStyle = taken ? PLACE_COLORS[owner] : "rgba(20, 8, 9, .72)"; ctx.fill();
        ctx.lineWidth = taken ? 2.5 : 1.5; ctx.strokeStyle = taken ? "#fff" : "rgba(255, 255, 255, .7)"; ctx.stroke();
        ctx.fillStyle = "#fff"; ctx.fillText(String(st.number), x, y + 1);
      }
    }
  }
  // Click a start position on the picture to give it to the selected place in the table (again to make it random).
  const spotAt = (e) => {
    const r = canvas.getBoundingClientRect(), k = MAP_VIEW / r.width;
    const x = (e.clientX - r.left) * k, y = (e.clientY - r.top) * k;
    return spots.find((s) => Math.hypot(s.x - x, s.y - y) <= 18);
  };
  canvas.addEventListener("click", (e) => { const s = spotAt(e); if (s && rows[active] && rows[active].kind !== "closed") assign(active, rows[active].start === s.n ? null : s.n); });
  canvas.addEventListener("mousemove", (e) => { const s = spotAt(e); canvas.style.cursor = s ? "pointer" : "default"; canvas.title = s ? `Start ${s.n}: click to give it to the selected place` : ""; });

  async function show() {
    const m = entries[i], mine = ++token;
    listed.checked = m.enabled;
    position.textContent = `${i + 1} of ${entries.length}`;
    title.textContent = m.name;
    prev.disabled = i === 0; next.disabled = i === entries.length - 1;
    note.textContent = "Loading…";
    fill(factline);
    shown = null; spots = []; rows = [];
    drawTable();
    try {
      const v = await invoke("map_view", { name: m.name });
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
    const bits = [m.pack];
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
function mapOrganiser() {
  const LIMIT = 150;
  let all = [], q = "", pack = "", view = "all", shown = LIMIT, confirmDelete = false;
  let counts = null, pf = "", sortBy = "name"; // counts: map name -> players, once read (null while counting)
  const summary = h("span", { class: "pill" });
  const search = h("input", { type: "text", placeholder: "Search maps", "aria-label": "Search maps", oninput: () => { q = search.value.trim().toLowerCase(); shown = LIMIT; draw(); } });
  const packSel = h("select", { "aria-label": "Show maps from", onchange: () => { pack = packSel.value; shown = LIMIT; draw(); } });
  const seg = (id, label) => h("button", { class: "seg", "data-view": id, onclick: () => { view = id; shown = LIMIT; draw(); } }, label);
  const segs = h("div", { class: "segs", role: "group", "aria-label": "Show" }, seg("all", "All"), seg("on", "On"), seg("off", "Off"));
  const sortSel = h("select", { "aria-label": "Sort maps", onchange: () => { sortBy = sortSel.value; shown = LIMIT; draw(); } },
    new Option("Sort: name", "name"), new Option("Sort: fewest players", "players"), new Option("Sort: most players", "players-desc"), new Option("Sort: largest file", "size"));
  const chips = h("div", { class: "chips", role: "group", "aria-label": "Number of players" });
  const result = h("p", { class: "muted small mapresult", role: "status" });
  const clear = h("button", { class: "btn small ghost", hidden: true, onclick: () => { q = ""; pack = ""; view = "all"; pf = ""; search.value = ""; shown = LIMIT; draw(); } }, "Clear filters");
  const list = h("div", { class: "maplist" });
  const more = h("button", { class: "btn small", hidden: true, onclick: () => { shown += LIMIT; draw(); } }, "Show more");
  const on = h("button", { class: "btn small", onclick: () => bulk(true) }, "Switch on shown");
  const off = h("button", { class: "btn small", onclick: () => bulk(false) }, "Switch off shown");
  const del = h("button", { class: "btn small ghost danger", onclick: () => remove() }, "Delete shown…");
  const empty = h("div", { class: "notice", hidden: true });
  const players = (m) => (counts && m.name in counts ? counts[m.name] : null);
  const playerText = (n) => (n === null ? "" : n === 0 ? "Solo" : `${n} players`);
  const base = () => all.filter((m) => (!q || m.name.toLowerCase().includes(q)) && (!pack || m.pack === pack) && (view === "all" || (view === "on") === m.enabled));
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
      ...[...have.keys()].sort((a, b) => a - b).map((n) => chip(String(n), h("span", {}, n === 0 ? "Solo" : String(n), h("span", { class: "chipcount" }, String(have.get(n)))))),
      counts ? null : h("span", { class: "muted small" }, "counting…"));
  }
  function drawSummary() {
    const n = all.filter((m) => m.enabled).length;
    summary.textContent = all.length ? `${n} of ${all.length} switched on` : "";
    summary.className = "pill" + (n ? " ok" : "");
  }
  function draw() {
    const packs = [...new Set(all.map((m) => m.pack))].sort();
    if (pack && !packs.includes(pack)) pack = "";
    packSel.replaceChildren(new Option("All packs", ""), ...packs.map((p) => new Option(`${p} (${all.filter((m) => m.pack === p).length})`, p)));
    packSel.value = pack;
    segs.querySelectorAll(".seg").forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.view === view)));
    const v = visible();
    drawSummary();
    drawChips();
    result.textContent = all.length ? (v.length === all.length ? `${all.length} maps` : `${v.length} of ${all.length} maps`) : "";
    clear.hidden = !(q || pack || view !== "all" || pf !== "");
    empty.hidden = !!v.length;
    fill(empty, all.length ? "No maps match." : h("span", {}, "No maps yet. Install a pack above, or add your own maps from a file or folder."));
    fill(list, v.slice(0, shown).map((m) => {
      const box = h("input", { type: "checkbox", checked: m.enabled, "aria-label": `Use ${m.name}`, onchange: async () => {
        try { await invoke("maps_set_enabled", { names: [m.name], enabled: box.checked }); m.enabled = box.checked; row.classList.toggle("on", m.enabled); drawSummary(); }
        catch (e) { box.checked = m.enabled; say(errText(e), "bad"); }
      } });
      const idx = v.indexOf(m);
      const row = h("label", { class: "maprow" + (m.enabled ? " on" : "") }, box, h("span", { class: "maptext" }, h("span", { class: "mapname" }, m.name), h("span", { class: "muted small mappack-tag" }, m.pack)), h("span", { class: "small mapplayers" }, playerText(players(m))), h("span", { class: "muted small mapsize" }, m.size_kb >= 1024 ? `${(m.size_kb / 1024).toFixed(1)} MB` : `${m.size_kb} KB`),
        h("button", { class: "btn small", "aria-label": `Set up a skirmish on ${m.name}`, title: "Choose players, generals, teams and start positions", onclick: (e) => { e.preventDefault(); e.stopPropagation(); openMapViewer(v, idx, () => { row.classList.toggle("on", m.enabled); box.checked = m.enabled; drawSummary(); }); } }, icon("play"), "Skirmish"));
      return row;
    }));
    more.hidden = v.length <= shown;
    more.textContent = `Show more (${v.length - shown} left)`;
    [on, off, del].forEach((b) => { b.disabled = !v.length; });
    del.textContent = confirmDelete ? `Click again to delete ${v.length} map${v.length === 1 ? "" : "s"}` : "Delete shown…";
  }
  async function bulk(enabled) {
    const names = visible().filter((m) => m.enabled !== enabled).map((m) => m.name);
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
    const names = visible().map((m) => m.name);
    try { const n = await invoke("maps_delete", { names }); say(`${n} map${n === 1 ? "" : "s"} deleted`, "ok"); } catch (e) { say(errText(e), "bad"); }
    await reloadOrganiser();
  }
  async function reloadOrganiser() {
    try { all = await invoke("maps_organiser"); } catch (e) { say(errText(e), "bad"); all = []; }
    draw();
    // reading each map's start positions takes a moment the first time; the list is usable meanwhile
    invoke("maps_players").then((c) => { counts = c; draw(); }).catch(() => { counts = {}; draw(); });
  }
  refreshers.add(reloadOrganiser);
  reloadOrganiser();
  const el = h("div", { class: "organiser" },
    h("div", { class: "row between" }, h("h2", {}, "Your maps"), summary),
    h("div", { class: "organiser-bar" }, search, packSel, segs),
    chips,
    empty,
    h("div", { class: "listbox" },
      h("div", { class: "listhead" }, h("div", { class: "row" }, result, clear), sortSel),
      list,
      h("div", { class: "listfoot" }, h("div", { class: "row" }, on, off), del)),
    h("div", { class: "row center" }, more),
    h("p", { class: "muted small" }, "Switched-off maps are moved aside, not deleted. Changes apply the next time the game starts."));
  return { el, dispose: () => refreshers.delete(reloadOrganiser) };
}

function mapsPage() {
  const packs = mapsCard();
  const org = mapOrganiser();
  const el = h("div", { class: "page" },
    h("div", { class: "page-head" }, h("h1", {}, "Maps")),
    h("div", { class: "skbanner" }, h("span", { class: "skbanner-icon", "aria-hidden": "true" }, icon("play")), h("div", {}, h("h3", {}, "Play a skirmish on any map"), h("p", { class: "muted small" }, "Press Skirmish on a map below to choose who plays, their generals, teams and where they start."))),
    h("div", { class: "card" }, org.el),
    h("details", { class: "card help", open: true }, h("summary", {}, "Get more maps"), packs.el));
  return { el, dispose: () => { packs.dispose(); org.dispose(); } };
}

// ---------------------------------------------------------------- components
/** A chip for the state of one thing (a mod is installed, waiting, or blocked). */
function pill(text, kind) { return h("span", { class: "pill " + (kind || "") }, text); }
/** A plain status line with a dot, for ambient state in the sidebar. */
function status(text, kind) { return h("span", { class: "status " + (kind || "") }, text); }

/** "Choose or download the game files". `onChange(ok)` fires whenever validity changes. */
function gameFilesPanel(onChange) {
  const timers = [];
  const dir = h("input", { type: "text", placeholder: "Folder that contains INIZH.big", value: store.cfg.game_dir || "", "aria-label": "Game folder" });
  const status = h("div", { class: "msg muted", role: "status" });
  const detected = h("div", { class: "stack" });

  async function check(path) {
    const r = await invoke("check_game_dir", { path });
    status.className = "msg " + (r.ok ? "ok" : path.trim() ? "bad" : "muted");
    status.textContent = (r.ok ? "✓ " : path.trim() ? "✗ " : "") + r.message;
    if (r.ok) {
      await invoke("save_game_dir", { path });
      store.cfg.game_dir = path.trim();
    }
    onChange && onChange(r.ok);
    return r.ok;
  }
  dir.addEventListener("change", () => check(dir.value));

  const zh = store.games.filter((g) => g.zero_hour);
  zh.forEach((g) => {
    detected.append(h("button", { class: "btn", onclick: () => { dir.value = g.dir; check(g.dir); } }, "Use Steam install: " + g.dir));
  });

  // ---- download from Steam
  const user = h("input", { type: "text", autocomplete: "username", value: store.cfg.steam_user || "", "aria-label": "Steam account name" });
  const pass = h("input", { type: "password", autocomplete: "current-password", "aria-label": "Steam password" });
  const go = h("button", { class: "btn primary", onclick: async () => {
    try { await invoke("assets_start", { user: user.value, password: pass.value }); pass.value = ""; } catch (e) { smsg.textContent = errText(e); }
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
      dir.value = store.cfg.game_dir;
      check(dir.value);
    }
    wasDone = s.phase === "Done";
  }, 800));

  const steam = h("details", {},
    h("summary", {}, "Don't have the files on this computer? Download them from Steam"),
    h("div", { class: "stack" },
      h("p", { class: "muted small" }, "Downloads Generals and Zero Hour (about 4 GB) through Steam. You must own both on Steam. Your password is used only to sign in and is never saved."),
      h("div", { class: "grid2" }, h("label", { class: "field" }, h("span", {}, "Steam account name"), user), h("label", { class: "field" }, h("span", {}, "Steam password"), pass)),
      h("div", { class: "row" }, go, cancel, codeBtn), need, bar, smsg,
      h("details", {}, h("summary", { class: "small muted" }, "Details"), log)));

  const el = h("div", { class: "stack" },
    detected,
    h("label", { class: "field" }, h("span", {}, "Game folder"), dir),
    status, steam);

  // initial state: remembered folder, else the first Steam install
  (async () => {
    if (dir.value) await check(dir.value);
    else if (zh[0]) { dir.value = zh[0].dir; await check(dir.value); }
    else { onChange && onChange(false); status.textContent = "Choose the folder that contains INIZH.big"; }
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

function modCard(m, compact) {
  const waiting = store.watch && store.watch.id === m.id;
  const busy = store.installing && store.installing.id === m.id;
  const role = modRole(m);
  const actions = h("div", { class: "row" });
  if (busy) {
    actions.append(h("span", { class: "live busy", role: "status" }, h("span", { class: "spin", "aria-hidden": "true" }), "Installing…"));
  } else if (m.installed) {
    actions.append(pill("Installed", "ok"));
    if (!compact) {
      let armed = false;
      const rm = h("button", { class: "btn small danger", onclick: async () => {
        if (!armed) { armed = true; rm.textContent = "Click again to remove"; setTimeout(() => { armed = false; rm.textContent = "Remove"; }, 3000); return; }
        try { await invoke("mods_remove", { folder: m.path }); say(m.name + " removed"); } catch (e) { say(errText(e), "bad"); }
        loadMods();
      } }, "Remove");
      actions.append(rm);
    }
  } else if (waiting) {
    actions.append(pill("Waiting for download", "warn"));
  } else if (m.pick) {
    // The player brings the files: nothing to wait for in the launcher.
    actions.append(
      h("button", { class: "btn primary", onclick: () => choose(m.id, false) }, "Choose file…"),
      h("button", { class: "btn ghost", onclick: () => choose(m.id, true) }, "Choose folder…"));
  } else {
    actions.append(h("button", { class: "btn primary", onclick: () => startWatch(m) }, "Get"));
  }
  const missingBase = role.base && !role.base.installed && !m.installed;
  return h("div", { class: "card" + (role.base ? " addon" : "") },
    h("div", { class: "mod" },
      h("div", { class: "stack tight" },
        h("div", { class: "row" }, h("h3", {}, m.name), role.base ? pill(role.pill) : null),
        h("p", { class: "muted" }, m.description),
        missingBase ? h("p", { class: "small warn-text" }, role.how) : null,
        !compact && m.notes && !m.installed ? h("details", { class: "help-inline" }, h("summary", {}, "How to install"), h("p", { class: "muted small" }, m.notes)) : null),
      actions),
    h("div", { class: "mod-links" },
      m.pick && !m.installed && !busy ? h("button", { class: "linkbtn", onclick: () => invoke("open_download_page", { id: m.id }).catch((e) => say(errText(e), "bad")) }, "Download page") : null,
      h("button", { class: "linkbtn", onclick: () => invoke("open_homepage", { id: m.id }).catch((e) => say(errText(e), "bad")) }, "Mod page")),
    watchBanner(m.id),
    busy ? h("div", { class: "jobline", role: "status" }, h("span", { class: "spin", "aria-hidden": "true" }), h("span", {}, "Unpacking and installing. A large mod takes a minute or two.")) : null);
}

/** Mods first, each followed by the add-ons that need it, so how they relate is plain. */
function modGroups() {
  const all = store.mods.catalog;
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

/** Start the game with the saved display choices and mod selection. `skirmish` (with an optional map, opponents and difficulty)
 *  goes straight into a skirmish. Returns whether the game was started. */
async function startGame({ skirmish = false, map = null, ai = null, difficulty = null, start = null, slots = null } = {}) {
  if (!store.cfg.game_dir) { openWizard(); return false; }
  const p = prefs.get(), nat = nativeRes();
  const { w, h: hh } = p.res === "native" ? nat : p.res === "custom" ? { w: p.cw || nat.w, h: p.ch || nat.h } : (([x, y]) => ({ w: +x, h: +y }))(String(p.res).split("x"));
  const busy = busyOverlay(map ? `Starting a skirmish on ${map}…` : skirmish ? "Starting a skirmish…" : "Starting the game…");
  let started = false;
  try {
    await invoke("launch_game", { gameDir: store.cfg.game_dir, modFolders: launchFolders(), width: w, height: hh, fullscreen: !!p.fs, skirmish, raisedCamera: !!p.camera, skirmishMap: map, skirmishAi: ai, skirmishDifficulty: difficulty, skirmishStart: start, skirmishSlots: slots });
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
  const files = h("div", { class: "row between" });
  const heroTitle = h("h2", {}), heroSub = h("p", { class: "muted" });
  // The chosen game's own picture, read from the player's files, fading in from the right edge of the card. Two layers:
  // the new picture fades in while the old one fades out.
  const art = [0, 1].map(() => h("img", { class: "heroart", alt: "", "aria-hidden": "true" }));
  let artKey = "", artFront = 0, hasArt = false;
  const paintArt = () => art.forEach((el, i) => el.classList.toggle("shown", hasArt && i === artFront));
  async function loadArt(folder) {
    const key = `${store.cfg.game_dir}|${folder || ""}`;
    if (key === artKey) return;
    artKey = key;
    let url = null;
    if (store.cfg.game_dir) { try { url = await invoke("game_art", { gameDir: store.cfg.game_dir, gameFolder: folder || null }); } catch { url = null; } }
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
    loadArt(cur.folder);
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
      h("div", { class: "optlist" },
        h("div", { class: "opt" }, h("label", { class: "switch" }, fs, "Fullscreen"), h("span", { class: "muted small" }, "Fill the screen instead of a window.")),
        h("div", { class: "opt" }, h("label", { class: "switch" }, cam, "Higher camera"), h("span", { class: "muted small" }, "See further out. Leave it off for a normal online view."))),
      h("p", { class: "muted small" }, "Any resolution works, including 4K and ultrawide. An add-on such as Chaos is laid over the mod it needs.")));
  return { el, dispose: () => refreshers.delete(fillMods) };
}

function modsPage() {
  const body = h("div", { class: "stack" });
  const path = h("input", { type: "text", placeholder: "…or paste a path", "aria-label": "Path to mod" });
  const dropBusy = h("span", { class: "live busy", hidden: true, role: "status" }, h("span", { class: "spin", "aria-hidden": "true" }), "Installing…");
  const drop = h("div", { class: "drop", id: "dropzone" },
    h("b", {}, "Add another mod from a file"), dropBusy,
    h("span", { class: "small" }, "Drop it here, or choose it: .zip, .7z, .rar, a folder, or an installer .exe"),
    h("div", { class: "row" }, h("button", { class: "btn", onclick: () => choose(null, false) }, "Choose file…"), h("button", { class: "btn", onclick: () => choose(null, true) }, "Choose folder…")),
    h("div", { class: "row" }, path, h("button", { class: "btn small", onclick: () => { if (path.value.trim()) { installPath(path.value.trim()); path.value = ""; } } }, "Install path")));
  const help = h("details", { class: "card help" },
    h("summary", {}, "A mod came as an .exe installer?"),
    h("p", { class: "muted" }, "Rallypoint unpacks the game archives and does not run a mod's own Windows launcher. Shockwave is one of these: drop the installer (or the .zip it came in) here and the archives load with Rallypoint's engine, on macOS and Windows."),
    h("ol", {},
      h("li", {}, "For Inno Setup installers, a tool helps: ", h("code", {}, "brew install innoextract sevenzip"), " on macOS, ", h("code", {}, "sudo apt install innoextract p7zip-full"), " on Linux, or 7-Zip on Windows. Shockwave does not need one."),
      h("li", {}, "Drop the .exe above. Rallypoint keeps the game files (.big and .gib) and skips the launcher program."),
      h("li", {}, "Many mods also offer a plain .zip or \"no installer\" download. Choose that when you can.")));

  function render() {
    dropBusy.hidden = !(store.installing && store.installing.id === "*");
    fill(body,
      modGroups(),
      store.mods.other.length ? h("h2", {}, "Your other mods") : null,
      store.mods.other.map((o) => {
        let armed = false;
        const rm = h("button", { class: "btn small danger", onclick: async () => {
          if (!armed) { armed = true; rm.textContent = "Click again to remove"; setTimeout(() => { armed = false; rm.textContent = "Remove"; }, 3000); return; }
          try { await invoke("mods_remove", { folder: o.folder }); say(o.folder + " removed"); } catch (e) { say(errText(e), "bad"); }
          loadMods();
        } }, "Remove");
        const bundled = o.folder === ICON_FOLDER;
        return h("div", { class: "card" }, h("div", { class: "mod" },
          h("div", { class: "stack tight" },
            h("div", { class: "row" }, h("h3", {}, o.folder), bundled ? pill("Bundled", "ok") : null),
            h("p", { class: "muted small" }, bundled ? "Sharp command-bar icons. Installed by Rallypoint and loaded with every game." : `${o.bigs} game file${o.bigs === 1 ? "" : "s"}, ${o.mb} MB`)),
          bundled ? null : rm));
      }));
  }
  refreshers.add(render); render();
  const el = h("div", { class: "page" },
    h("div", { class: "page-head" }, h("h1", {}, "Mods"), h("button", { class: "btn small", onclick: () => invoke("open_mods_folder").catch((e) => say(errText(e), "bad")) }, "Open mods folder")),
    body, drop, help);
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

function onlinePage() {
  const conn = connectionCard();
  const saved = splitServer(store.cfg.server);
  const server = h("input", { type: "text", placeholder: "play.example.com", value: saved.host, "aria-label": "Server address" });
  const port = h("input", { type: "text", inputmode: "numeric", placeholder: DEFAULT_PORT, value: saved.port, "aria-label": "Server port" });
  const room = h("input", { type: "text", placeholder: "any name", value: store.cfg.room || "", "aria-label": "Room" });
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
  async function refresh() {
    const s = store.relay;
    const connected = s.startsWith("Connected as");
    const on = s === "Connecting..." || connected;
    btn.textContent = on ? "Disconnect" : "Connect";
    btn.dataset.mode = on ? "disconnect" : "connect";
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
      problem,
      h("details", { class: "help-inline" }, h("summary", {}, "How rooms work"),
        h("p", { class: "muted small" }, "Join a room on a Rallypoint server, then use the game's LAN lobby: everyone in the same room with the same password ends up in the same game. A room that doesn't exist yet is created with your password and disappears when everyone has left. A saved password stays in Rallypoint's settings file on this computer, readable only by you."))),
    h("div", { class: "card" }, h("h2", {}, "Your name"), setting("Name in the lobby", nameNote, nameBox)),
    conn.el,
    saved$);
  return { el, dispose: () => { refreshers.delete(refresh); conn.dispose(); } };
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

/** Settings card for the graphics fix: how far the camera zooms out, and the high-quality options. */
function graphicsCard() {
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
  return h("div", { class: "card" }, h("h2", {}, "Graphics"),
    setting("Camera distance", "How far out you can zoom. The original game stops at 300.", camera),
    setting("High-quality settings", "Shadows, soft water edges, trees, heat effects, extra animations and 5000 particles. Volumes and controls are not changed.", apply),
    fb.el);
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

function settingsPage() {
  const files = gameFilesPanel(() => updateSidebar());
  const el = h("div", { class: "page" },
    h("div", { class: "page-head" }, h("h1", {}, "Settings")),
    h("h3", { class: "section-title" }, "Game"),
    h("div", { class: "card" }, h("h2", {}, "Game files"), files.el, forgetSteamRow()),
    graphicsCard(),
    h("h3", { class: "section-title" }, "Multiplayer"),
    syncCard(),
    h("h3", { class: "section-title" }, "Rallypoint"),
    updatesCard(),
    h("div", { class: "card" }, h("h2", {}, "Troubleshooting"), diagnosePanel(),
      setting("First-run setup", "Walk through the setup again.", h("button", { class: "btn", onclick: () => openWizard() }, "Run"))),
    h("p", { class: "muted small about" }, `Rallypoint ${store.cfg.version} on ${store.cfg.os}. Launcher and server are MIT licensed; the game engine is GPL-3.0 with EA's additional terms. Not affiliated with or endorsed by Electronic Arts.`));
  return { el, dispose: files.dispose };
}

// ---------------------------------------------------------------- shell
const PAGES = { play: ["Play", "play", playPage], mods: ["Mods", "mods", modsPage], maps: ["Maps", "maps", mapsPage], online: ["Online", "online", onlinePage], settings: ["Settings", "settings", settingsPage] };
let current = null, sideFiles, sideNet;

function go(name) {
  store.page = name;
  current && current.dispose && current.dispose();
  pageStatus = liveStatus();
  current = PAGES[name][2]();
  const head = current.el.querySelector(".page-head");
  if (head) head.after(h("div", { class: "page-status" }, pageStatus.el));
  document.getElementById("pagehost").replaceChildren(current.el);
  document.querySelectorAll(".nav").forEach((n) => (n.dataset.page === name ? n.setAttribute("aria-current", "page") : n.removeAttribute("aria-current")));
}

function updateSidebar() {
  if (!sideFiles) return;
  sideFiles.replaceChildren(status(store.cfg.game_dir ? "Game files ready" : "Game files needed", store.cfg.game_dir ? "ok" : "warn"));
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

function shell() {
  sideFiles = h("div"); sideNet = h("div");
  const nav = Object.entries(PAGES).map(([id, [label, ic]]) => h("button", { class: "nav", "data-page": id, onclick: () => go(id) }, icon(ic), h("span", {}, label)));
  document.getElementById("root").append(h("div", { class: "app" },
    h("aside", { class: "sidebar" },
      h("div", { class: "brand" }, h("img", { src: "logo.png", alt: "" }), h("b", {}, "Rallypoint")),
      nav, h("div", { class: "spacer" }), h("div", { class: "side-foot" }, sideFiles, sideNet, h("div", { class: "side-version", title: "The version of Rallypoint you are running" }, `Rallypoint ${store.cfg.version}`))),
    h("main", { id: "main" }, h("div", { id: "updatehost" }), h("div", { id: "pagehost" }))));
  updateSidebar();
}

// ---------------------------------------------------------------- first-run wizard
let wizardEl = null;
function closeWizard() { wizardEl && wizardEl.remove(); wizardEl = null; }

function openWizard() {
  closeWizard();
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
      return h("div", { class: "intro" }, h("img", { src: "logo.png", alt: "" }),
        h("div", { class: "stack" }, h("h1", {}, "Set up Rallypoint"),
          h("p", { class: "muted" }, "Rallypoint runs Command & Conquer: Generals – Zero Hour on your Mac or PC, at any screen size. It needs your own copy of the game. This takes about two minutes and everything can be changed later."),
          h("ol", { class: "checklist" }, h("li", {}, "Find or download your game files"), h("li", {}, "Choose a resolution"), h("li", {}, "Add mods and maps, if you want them"))));
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
      h("p", { class: "muted small" }, "To play with friends, open the Online tab and enter a server and room."),
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

// ---------------------------------------------------------------- start
async function start() {
  store.cfg = await invoke("get_config");
  store.games = await invoke("detect_games").catch(() => []);
  shell();
  await loadMods();
  ensureIcons().catch((e) => { if (!wizardEl) toast(errText(e), "bad"); });
  await loadMaps();
  go("play");
  setInterval(pollNet, 2000);
  pollNet();
  setInterval(async () => {
    try { store.relay = await invoke("relay_status"); } catch { /* keep the last status */ }
    updateSidebar();
    notifyOnlineOnly();
  }, 1000);
  if (!store.cfg.setup_done) openWizard();
  if (store.cfg.check_updates !== false) checkUpdate();
  invoke("whats_new_take").then((w) => { if (w) showWhatsNew(w); }).catch(() => {});

  // Dropping a file or folder on the window installs it as a mod.
  try {
    tauri.webview.getCurrentWebview().onDragDropEvent((e) => {
      const z = document.getElementById("dropzone");
      if (e.payload.type === "over" && z) z.classList.add("over");
      else if (e.payload.type === "drop") {
        z && z.classList.remove("over");
        if (store.page !== "mods") go("mods");
        (e.payload.paths || []).forEach(installPath);
      } else z && z.classList.remove("over");
    });
  } catch { /* drag and drop is a convenience; the path box still works */ }
}
function notifyOnlineOnly() { if (store.page === "online") refreshers.forEach((f) => f.name === "refresh" && f()); }

start().catch((e) => document.getElementById("root").append(h("pre", { class: "log" }, "Couldn't start: " + errText(e))));
