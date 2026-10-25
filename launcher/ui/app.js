"use strict";
// Rallypoint launcher UI. Everything is built from DOM nodes (never markup strings), because folder names and
// mod names come from outside and the page has a strict content security policy.
const tauri = window.__TAURI__;
const invoke = (cmd, args) => tauri.core.invoke(cmd, args);

// ---------------------------------------------------------------- helpers
function h(tag, props, ...kids) {
  const el = document.createElement(tag);
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
  settings: "M19.400 13a7.500 7.500 0 0 0 0-2l2.100-1.600-2-3.400-2.500 1a7.600 7.600 0 0 0-1.700-1L15 3.300h-4L10.700 6a7.600 7.600 0 0 0-1.700 1l-2.500-1-2 3.400L6.600 11a7.500 7.500 0 0 0 0 2l-2.100 1.600 2 3.400 2.500-1c.5.400 1.100.7 1.700 1l.3 2.700h4l.3-2.700c.6-.3 1.200-.6 1.700-1l2.500 1 2-3.400L19.400 13zM13 15.500a3.500 3.500 0 1 1 0-7 3.500 3.500 0 0 1 0 7z",
  check: "M9 16.200 4.800 12l-1.400 1.400L9 19 21 7l-1.400-1.400z",
  plus: "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6z",
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
  setTimeout(() => t.remove(), kind === "bad" ? 9000 : 4500);
}

/** replaceChildren that skips null/false (replaceChildren would print "null"). */
function fill(el, ...kids) {
  el.replaceChildren(...kids.flat(Infinity).filter((k) => k != null && k !== false));
}

const errText = (e) => (typeof e === "string" ? e : (e && e.message) || String(e));

// ---------------------------------------------------------------- state
const store = { cfg: null, games: [], mods: { catalog: [], other: [] }, relay: "Offline", page: "play", watch: null };
const nativeRes = () => ({ w: Math.round(screen.width * devicePixelRatio), h: Math.round(screen.height * devicePixelRatio) });

const prefs = {
  get() {
    try { return { res: "native", fs: false, mod: "", ...JSON.parse(localStorage.getItem("rallypoint.prefs") || "{}") }; } catch { return { res: "native", fs: false, mod: "" }; }
  },
  set(patch) {
    try { localStorage.setItem("rallypoint.prefs", JSON.stringify({ ...prefs.get(), ...patch })); } catch { /* per-viewer convenience only */ }
  },
};

const refreshers = new Set(); // views that show mods re-render through these
const notifyMods = () => refreshers.forEach((f) => f());
async function loadMods() {
  try { store.mods = await invoke("mods_list"); } catch (e) { toast(errText(e), "bad"); }
  notifyMods();
}

const installedMods = () => [...store.mods.catalog.filter((m) => m.installed).map((m) => ({ folder: m.path, name: m.name })), ...store.mods.other.map((o) => ({ folder: o.folder, name: o.folder }))];

// ---------------------------------------------------------------- mod downloads
// "Get" opens the mod's official page. Nothing installs by itself: the user chooses the finished download
// (or confirms the file we spotted in their Downloads folder).
let watchTimer = null;
async function startWatch(m) {
  try {
    await invoke("mods_get", { id: m.id });
  } catch (e) { return toast(errText(e), "bad"); }
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
  toast("Installing…");
  try {
    const r = await invoke("mods_install", { path, id: id || null });
    toast(`${r.folder} installed (${r.bigs} game file${r.bigs === 1 ? "" : "s"})`, "ok");
    if (id) { clearInterval(watchTimer); store.watch = null; }
  } catch (e) { toast(errText(e), "bad"); }
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
  } catch (e) { return toast("Couldn't open the file picker: " + errText(e), "bad"); }
  if (picked) await installPath(typeof picked === "string" ? picked : picked.path, id);
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

function modCard(m, compact) {
  const waiting = store.watch && store.watch.id === m.id;
  const actions = h("div", { class: "row" });
  if (m.installed) {
    actions.append(pill("Installed", "ok"));
    if (!compact) {
      let armed = false;
      const rm = h("button", { class: "btn small danger", onclick: async () => {
        if (!armed) { armed = true; rm.textContent = "Click again to remove"; setTimeout(() => { armed = false; rm.textContent = "Remove"; }, 3000); return; }
        try { await invoke("mods_remove", { folder: m.path }); toast(m.name + " removed"); } catch (e) { toast(errText(e), "bad"); }
        loadMods();
      } }, "Remove");
      actions.append(rm);
    }
  } else if (waiting) {
    actions.append(pill("Waiting for download", "warn"));
  } else {
    const base = m.addon_for ? store.mods.catalog.find((b) => b.id === m.addon_for) : null;
    if (base && !base.installed) actions.append(pill(`Needs ${base.name}`, "warn"));
    else actions.append(h("button", { class: "btn primary", onclick: () => startWatch(m) }, "Get"));
  }
  return h("div", { class: "card" },
    h("div", { class: "mod" },
      h("div", {}, h("h3", {}, m.name), h("p", { class: "muted" }, m.description)),
      actions),
    !compact && m.notes ? h("p", { class: "muted small" }, m.notes) : null,
    h("div", { class: "row" },
      h("button", { class: "btn small ghost", onclick: () => invoke("open_homepage", { id: m.id }).catch((e) => toast(errText(e), "bad")) }, "Mod page")));
}

function watchBanner() {
  const w = store.watch;
  if (!w) return null;
  return h("div", { class: "banner stack" },
    h("div", {}, h("b", {}, `Download ${w.name} in your browser`), h("p", { class: "muted small" }, "When the download has finished, choose the file here. (If you picked an .exe installer, see the help below.)")),
    w.found ? h("div", { class: "row" }, h("span", { class: "small" }, "Found in Downloads: "), h("code", {}, w.found.name), h("button", { class: "btn small primary", onclick: () => installPath(w.found.path, w.id) }, "Install this file")) : null,
    h("div", { class: "row" }, h("button", { class: "btn primary", onclick: () => choose(w.id, false) }, "Choose downloaded file…"), h("button", { class: "btn", onclick: cancelWatch }, "Cancel")));
}

// ---------------------------------------------------------------- pages
function playPage() {
  const p = prefs.get();
  const nat = nativeRes();
  const sel = h("select", { "aria-label": "Mod" });
  const resSel = h("select", { "aria-label": "Resolution" });
  const cw = h("input", { type: "number", min: 640, value: nat.w, "aria-label": "Width" });
  const ch = h("input", { type: "number", min: 480, value: nat.h, "aria-label": "Height" });
  const custom = h("div", { class: "row", hidden: true }, cw, "×", ch);
  const fs = h("input", { type: "checkbox", checked: p.fs, onchange: () => prefs.set({ fs: fs.checked }) });
  const files = h("div", { class: "row between" });
  const play = h("button", { class: "btn primary big", onclick: () => launch(false) }, icon("play"), "Play");
  const sk = h("button", { class: "btn big", onclick: () => launch(true) }, "Quick Skirmish");

  const presets = [[3840, 2160], [2560, 1440], [1920, 1080], [1600, 900], [1280, 720]].filter(([w]) => w <= nat.w);
  resSel.append(new Option(`Native (${nat.w} × ${nat.h})`, "native"), ...presets.filter(([w]) => w !== nat.w).map(([w, hh]) => new Option(`${w} × ${hh}`, `${w}x${hh}`)), new Option("Custom…", "custom"));
  resSel.value = [...resSel.options].some((o) => o.value === p.res) ? p.res : "native";
  const syncRes = () => { custom.hidden = resSel.value !== "custom"; prefs.set({ res: resSel.value }); };
  resSel.addEventListener("change", syncRes); syncRes();
  const size = () => (resSel.value === "native" ? nat : resSel.value === "custom" ? { w: +cw.value, h: +ch.value } : (([w, hh]) => ({ w: +w, h: +hh }))(resSel.value.split("x")));

  function fillMods() {
    const keep = sel.value || p.mod;
    sel.replaceChildren(new Option("Vanilla (no mod)", ""), ...installedMods().map((m) => new Option(m.name, m.folder)));
    sel.value = [...sel.options].some((o) => o.value === keep) ? keep : "";
  }
  sel.addEventListener("change", () => prefs.set({ mod: sel.value }));
  refreshers.add(fillMods); fillMods();

  function fillFiles() {
    fill(files,
      store.cfg.game_dir ? h("p", { class: "muted small" }, "Game files: " + store.cfg.game_dir) : h("p", { class: "msg bad" }, "Rallypoint doesn't have your game files yet."),
      store.cfg.game_dir ? null : h("button", { class: "btn", onclick: () => openWizard() }, "Find or download game files"));
  }
  fillFiles();

  async function launch(skirmish) {
    if (!store.cfg.game_dir) { toast("Set up your game files first", "bad"); return openWizard(); }
    const { w, h: hh } = size();
    play.disabled = sk.disabled = true;
    try {
      await invoke("launch_game", { gameDir: store.cfg.game_dir, modFolder: sel.value || null, width: w, height: hh, fullscreen: fs.checked, skirmish });
      toast(skirmish ? "Starting a skirmish…" : "Starting the game…", "ok");
    } catch (e) { toast(errText(e), "bad"); }
    setTimeout(() => { play.disabled = sk.disabled = false; }, 2500);
  }

  const el = h("div", { class: "page" },
    h("div", { class: "page-head" }, h("h1", {}, "Play")),
    h("div", { class: "card hero" },
      h("div", {}, h("h2", {}, "Command & Conquer: Generals – Zero Hour"), h("p", { class: "muted" }, "Play opens the full game; Skirmish is under Solo Play. Quick Skirmish starts a 1v1 against the AI straight away.")),
      files,
      h("div", { class: "row" }, play, sk)),
    h("div", { class: "card" },
      h("h3", {}, "Options"),
      h("div", { class: "grid2" },
        h("label", { class: "field" }, h("span", {}, "Mod"), sel),
        h("label", { class: "field" }, h("span", {}, "Resolution"), resSel)),
      custom,
      h("label", { class: "switch" }, fs, "Fullscreen"),
      h("p", { class: "muted small" }, "Any resolution works, including 4K and ultrawide: the camera and menus adapt instead of zooming in or stretching.")));
  return { el, dispose: () => refreshers.delete(fillMods) };
}

function modsPage() {
  const body = h("div", { class: "stack" });
  const path = h("input", { type: "text", placeholder: "…or paste a path", "aria-label": "Path to mod" });
  const drop = h("div", { class: "drop", id: "dropzone" },
    h("b", {}, "Add a mod from a file"),
    h("span", { class: "small" }, "Drop it here, or choose it: .zip, .7z, .rar, a folder, or an installer .exe"),
    h("div", { class: "row" }, h("button", { class: "btn", onclick: () => choose(null, false) }, "Choose file…"), h("button", { class: "btn", onclick: () => choose(null, true) }, "Choose folder…")),
    h("div", { class: "row" }, path, h("button", { class: "btn small", onclick: () => { if (path.value.trim()) { installPath(path.value.trim()); path.value = ""; } } }, "Install path")));
  const help = h("details", { class: "card help" },
    h("summary", {}, "A mod came as an .exe installer?"),
    h("p", { class: "muted" }, "Rallypoint can unpack many installers without running them, if one of these free tools is installed (it tells you which if one is missing):"),
    h("ol", {},
      h("li", {}, "Install a tool: ", h("code", {}, "brew install innoextract sevenzip"), " on macOS, ", h("code", {}, "sudo apt install innoextract p7zip-full"), " on Linux, or 7-Zip on Windows."),
      h("li", {}, "Drop the .exe above. Rallypoint unpacks it and keeps the game files (.big)."),
      h("li", {}, "If the mod also needs its own program (.exe) to run, that part can't work with this game engine; only the mod's game files are used."),
      h("li", {}, "Many mods also offer a plain .zip or \"no installer\" download. Choose that when you can.")));

  function render() {
    fill(body,
      watchBanner(),
      h("h2", {}, "Featured"),
      store.mods.catalog.map((m) => modCard(m, false)),
      store.mods.other.length ? h("h2", {}, "Your other mods") : null,
      store.mods.other.map((o) => {
        let armed = false;
        const rm = h("button", { class: "btn small danger", onclick: async () => {
          if (!armed) { armed = true; rm.textContent = "Click again to remove"; setTimeout(() => { armed = false; rm.textContent = "Remove"; }, 3000); return; }
          try { await invoke("mods_remove", { folder: o.folder }); toast(o.folder + " removed"); } catch (e) { toast(errText(e), "bad"); }
          loadMods();
        } }, "Remove");
        return h("div", { class: "card" }, h("div", { class: "mod" }, h("div", {}, h("h3", {}, o.folder), h("p", { class: "muted small" }, `${o.bigs} game file${o.bigs === 1 ? "" : "s"}, ${o.mb} MB`)), rm));
      }));
  }
  refreshers.add(render); render();
  const el = h("div", { class: "page" },
    h("div", { class: "page-head" }, h("h1", {}, "Mods"), h("button", { class: "btn small", onclick: () => invoke("open_mods_folder").catch((e) => toast(errText(e), "bad")) }, "Open mods folder")),
    body, drop, help);
  return { el, dispose: () => refreshers.delete(render) };
}

function onlinePage() {
  const server = h("input", { type: "text", placeholder: "host:7777", value: store.cfg.server || "", "aria-label": "Server" });
  const room = h("input", { type: "text", placeholder: "any name", value: store.cfg.room || "", "aria-label": "Room" });
  const pw = h("input", { type: "password", autocomplete: "off", "aria-label": "Room password" });
  const status = h("div", { class: "row" });
  const btn = h("button", { class: "btn primary", onclick: async () => {
    try {
      if (btn.dataset.mode === "disconnect") await invoke("relay_disconnect");
      else {
        await invoke("save_config", { server: server.value, room: room.value, steamUser: store.cfg.steam_user || "" });
        store.cfg.server = server.value.trim(); store.cfg.room = room.value.trim();
        await invoke("relay_connect", { password: pw.value });
      }
    } catch (e) { toast(errText(e), "bad"); }
    refresh();
  } }, "Connect");
  function refresh() {
    const s = store.relay;
    const on = s === "Connecting..." || s.startsWith("Connected as");
    btn.textContent = on ? "Disconnect" : "Connect";
    btn.dataset.mode = on ? "disconnect" : "connect";
    status.replaceChildren(pill(s, s.startsWith("Connected as") ? "ok" : on ? "warn" : s === "Offline" ? "" : "bad"));
  }
  refreshers.add(refresh); refresh();
  const el = h("div", { class: "page" },
    h("div", { class: "page-head" }, h("h1", {}, "Online")),
    h("div", { class: "card" },
      h("p", { class: "muted" }, "Join a room on a Rallypoint server, then use the game's LAN lobby. Everyone in the same room with the same password ends up in the same game."),
      h("div", { class: "grid2" }, h("label", { class: "field" }, h("span", {}, "Server"), server), h("label", { class: "field" }, h("span", {}, "Room"), room)),
      h("label", { class: "field" }, h("span", {}, "Room password"), pw),
      h("p", { class: "muted small" }, "A room that doesn't exist yet is created with this password, and disappears when everyone has left."),
      h("div", { class: "row" }, btn, status)));
  return { el, dispose: () => refreshers.delete(refresh) };
}

function settingsPage() {
  const files = gameFilesPanel(() => updateSidebar());
  const el = h("div", { class: "page" },
    h("div", { class: "page-head" }, h("h1", {}, "Settings")),
    h("div", { class: "card" }, h("h2", {}, "Game files"), files.el),
    h("div", { class: "card" }, h("h2", {}, "Setup"), h("p", { class: "muted" }, "Walk through the first-run setup again."), h("div", { class: "row" }, h("button", { class: "btn", onclick: () => openWizard() }, "Run setup again"))),
    h("div", { class: "card" }, h("h2", {}, "About"),
      h("p", { class: "muted" }, `Rallypoint ${store.cfg.version} on ${store.cfg.os}. Launcher and server are MIT licensed; the game engine is GPL-3.0 with EA's additional terms. Not affiliated with or endorsed by Electronic Arts.`)));
  return { el, dispose: files.dispose };
}

// ---------------------------------------------------------------- shell
const PAGES = { play: ["Play", "play", playPage], mods: ["Mods", "mods", modsPage], online: ["Online", "online", onlinePage], settings: ["Settings", "settings", settingsPage] };
let current = null, sideFiles, sideNet;

function go(name) {
  store.page = name;
  current && current.dispose && current.dispose();
  current = PAGES[name][2]();
  document.getElementById("main").replaceChildren(current.el);
  document.querySelectorAll(".nav").forEach((n) => (n.dataset.page === name ? n.setAttribute("aria-current", "page") : n.removeAttribute("aria-current")));
}

function updateSidebar() {
  if (!sideFiles) return;
  sideFiles.replaceChildren(status(store.cfg.game_dir ? "Game files ready" : "Game files needed", store.cfg.game_dir ? "ok" : "warn"));
  const s = store.relay;
  sideNet.replaceChildren(status(s.startsWith("Connected as") ? "Online" : s === "Offline" ? "Offline" : s === "Connecting..." ? "Connecting…" : "Not connected", s.startsWith("Connected as") ? "ok" : s === "Offline" || s === "Connecting..." ? "" : "bad"));
}

function shell() {
  sideFiles = h("div"); sideNet = h("div");
  const nav = Object.entries(PAGES).map(([id, [label, ic]]) => h("button", { class: "nav", "data-page": id, onclick: () => go(id) }, icon(ic), h("span", {}, label)));
  document.getElementById("root").append(h("div", { class: "app" },
    h("aside", { class: "sidebar" },
      h("div", { class: "brand" }, h("img", { src: "logo.png", alt: "" }), h("b", {}, "Rallypoint")),
      nav, h("div", { class: "spacer" }), h("div", { class: "side-foot" }, sideFiles, sideNet)),
    h("main", { id: "main" })));
  updateSidebar();
}

// ---------------------------------------------------------------- first-run wizard
let wizardEl = null;
function closeWizard() { wizardEl && wizardEl.remove(); wizardEl = null; }

function openWizard() {
  closeWizard();
  const steps = ["Welcome", "Game files", "Display", "Mods", "Online", "Ready"];
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
          h("ol", { class: "checklist" }, h("li", {}, "Find or download your game files"), h("li", {}, "Choose a resolution"), h("li", {}, "Add mods and an online server, if you want them"))));
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
      return h("div", { class: "stack" }, h("h2", {}, "Display"),
        h("p", { class: "muted" }, "Rallypoint works at any resolution, including 4K and ultrawide. The camera keeps a normal view distance and the menus are scaled, not stretched."),
        h("label", { class: "field" }, h("span", {}, "Resolution"), resSel), h("label", { class: "switch" }, fs, "Fullscreen"));
    }
    if (step === 3) {
      const box = h("div", { class: "stack" });
      const render = () => fill(box, watchBanner(), store.mods.catalog.map((m) => modCard(m, true)));
      refreshers.add(render); render();
      w.dispose = () => refreshers.delete(render);
      return h("div", { class: "stack" }, h("h2", {}, "Mods (optional)"), h("p", { class: "muted" }, "Mods change the game. Get one now, or add any later from the Mods tab, where you can also drop in your own."), box);
    }
    if (step === 4) {
      const server = h("input", { type: "text", placeholder: "host:7777", value: store.cfg.server || "", "aria-label": "Server", onchange: save });
      const room = h("input", { type: "text", placeholder: "any name", value: store.cfg.room || "", "aria-label": "Room", onchange: save });
      function save() { store.cfg.server = server.value.trim(); store.cfg.room = room.value.trim(); invoke("save_config", { server: server.value, room: room.value, steamUser: store.cfg.steam_user || "" }); }
      return h("div", { class: "stack" }, h("h2", {}, "Online play (optional)"),
        h("p", { class: "muted" }, "To play with friends, enter a Rallypoint server and a room name. You pick the room password when you connect. Leave this blank to skip."),
        h("div", { class: "grid2" }, h("label", { class: "field" }, h("span", {}, "Server"), server), h("label", { class: "field" }, h("span", {}, "Room"), room)));
    }
    const pr = prefs.get(), nat = nativeRes();
    return h("div", { class: "stack" }, h("h1", {}, "Ready to play"),
      h("dl", { class: "summary" },
        h("dt", {}, "Game files"), h("dd", {}, store.cfg.game_dir || "not set yet"),
        h("dt", {}, "Display"), h("dd", {}, (pr.res === "native" ? `Native ${nat.w} × ${nat.h}` : pr.res.replace("x", " × ")) + (pr.fs ? ", fullscreen" : ", windowed")),
        h("dt", {}, "Mods"), h("dd", {}, installedMods().map((m) => m.name).join(", ") || "none"),
        h("dt", {}, "Server"), h("dd", {}, store.cfg.server ? `${store.cfg.server}${store.cfg.room ? ", room " + store.cfg.room : ""}` : "none")),
      h("div", { class: "row" }, h("button", { class: "btn primary big", onclick: () => finish() }, icon("play"), "Open Rallypoint"), h("button", { class: "btn big", onclick: () => finish(() => go("mods")) }, "Browse mods")));
  }

  let nextBtn, backBtn;
  function nav() {
    if (!nextBtn) return;
    nextBtn.disabled = w.i === 1 && !w.filesOk;
    nextBtn.textContent = ["Start setup", "Use these game files", "Continue to mods", "Continue to online play", "Review"][w.i] || "Continue";
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

// ---------------------------------------------------------------- start
async function start() {
  store.cfg = await invoke("get_config");
  store.games = await invoke("detect_games").catch(() => []);
  shell();
  await loadMods();
  go("play");
  setInterval(async () => {
    try { store.relay = await invoke("relay_status"); } catch { /* keep the last status */ }
    updateSidebar();
    notifyOnlineOnly();
  }, 1000);
  if (!store.cfg.setup_done) openWizard();

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
