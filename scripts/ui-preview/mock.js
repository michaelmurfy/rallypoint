// A fake Tauri backend so the launcher UI can be opened in a plain browser:
//   open scripts/ui-preview/index.html   (add #fresh for first run, #steam for a detected install)
const hash = location.hash;
const state = {
  cfg: { server: "", room: "", game_dir: hash.includes("fresh") ? "" : "/Users/me/Library/Application Support/GeneralsZH/game/ZeroHour", steam_user: "", setup_done: !hash.includes("fresh"), version: "0.1.0", os: "macos" },
  mods: [{ id: "shockwave", name: "C&C: Shockwave", description: "A large fan-made overhaul of Zero Hour with new units, generals and balance (ModDB Editor's Choice 2008).", homepage: "https://www.moddb.com/mods/cc-shockwave", download_page: "https://www.moddb.com/mods/cc-shockwave/downloads", path: "Shockwave", detect: ["shockwave"], notes: "On the download page choose the full release. A .zip or \"no installer\" download installs automatically. If only the .exe installer is offered, see the help below.", args: [], installed: hash.includes("modinstalled") ? { folder: "Shockwave", bigs: 12, mb: 280 } : null }],
  other: hash.includes("modinstalled") ? [{ folder: "My Custom Mod", bigs: 2, mb: 14 }] : [],
  job: null, watch: false,
};
function advance() {
  const j = state.job; if (!j) return;
  j.tick++;
  if (j.tick === 3) { j.s = { phase: "LoggingIn", percent: 0, message: "Enter the Steam Guard code from your authenticator app", need: "Guard", log: "Connecting to Steam3... Done!\nSTEAM GUARD! Please enter your 2 factor auth code from your authenticator app:" }; }
  if (j.s.need) return;
  if (j.tick > 3 && j.tick < 30) { const p = (j.tick - 3) * 3.7; j.s = { phase: "Downloading", percent: p, message: "Downloading Zero Hour…", need: null, log: j.s.log }; }
  if (j.tick >= 30) { j.s = { phase: "Done", percent: 100, message: "Game files are ready", need: null, log: j.s.log }; state.cfg.game_dir = "/Users/me/Library/Application Support/GeneralsZH/game/ZeroHour"; }
}
setInterval(advance, 500);
const handlers = {
  get_config: () => structuredClone(state.cfg),
  detect_games: () => (hash.includes("steam") ? [{ dir: "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Command & Conquer Generals - Zero Hour", zero_hour: true }] : []),
  check_game_dir: ({ path }) => (path.includes("ZeroHour") ? { ok: true, message: "Zero Hour found" } : path.trim() ? { ok: false, message: "No Zero Hour files in that folder (INIZH.big is missing)" } : { ok: false, message: "Choose the folder that contains INIZH.big" }),
  save_game_dir: ({ path }) => { state.cfg.game_dir = path.trim(); },
  save_config: ({ server, room, steamUser }) => { Object.assign(state.cfg, { server, room, steam_user: steamUser }); },
  set_setup_done: ({ done }) => { state.cfg.setup_done = done; },
  assets_start: ({ user }) => { state.cfg.steam_user = user; state.job = { tick: 0, s: { phase: "LoggingIn", percent: 0, message: "Signing in to Steam…", need: null, log: "" } }; },
  assets_status: () => (state.job ? state.job.s : { phase: "Idle", percent: 0, message: "", need: null, log: "" }),
  assets_submit: () => { if (state.job) state.job.s = { ...state.job.s, need: null, message: "Signing in to Steam…" }; },
  assets_cancel: () => { state.job = null; },
  mods_list: () => ({ catalog: state.mods, other: state.other }),
  mods_get: () => { state.watch = true; setTimeout(() => { state.mods[0].installed = { folder: "Shockwave", bigs: 12, mb: 280 }; state.watch = false; }, 4000); },
  mods_poll: () => (state.watch ? { state: "waiting", id: "shockwave", message: "" } : { state: "installed", id: "shockwave", message: "C&C: Shockwave installed (280 MB)" }),
  mods_cancel_watch: () => { state.watch = false; },
  mods_install: ({ path }) => ({ folder: path.split("/").pop().replace(/\.\w+$/, ""), bigs: 3, mb: 20 }),
  mods_remove: ({ folder }) => { if (folder === "Shockwave") state.mods[0].installed = null; state.other = state.other.filter((o) => o.folder !== folder); },
  open_mods_folder: () => {}, open_homepage: () => {},
  relay_status: () => "Offline", relay_connect: () => {}, relay_disconnect: () => {},
  launch_game: () => {},
};
window.__TAURI__ = {
  core: { invoke: async (cmd, args) => { if (!(cmd in handlers)) throw new Error("mock: no handler for " + cmd); return handlers[cmd](args || {}); } },
  webview: { getCurrentWebview: () => ({ onDragDropEvent: () => {} }) },
};
