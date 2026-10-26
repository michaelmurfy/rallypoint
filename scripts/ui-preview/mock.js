// A fake Tauri backend so the launcher UI can be opened in a plain browser:
//   open scripts/ui-preview/index.html   (add #fresh for first run, #steam for a detected install)
const hash = location.hash;
const fakeMaps = () => {
  const names = ["! 1v1 ! super no money map v2", "!!! no money !!! lots of fun3", "!2v2 cxn", "Tournament Desert", "Twilight Flame", "Defcon 6", "Mountain Pass", "Arctic Convoy", "Bear Town", "Fallen Empire", "Winter Wolves", "Dust Bowl 2v2", "Golden Gate", "Oasis 3v3"];
  const out = [];
  for (let i = 0; i < 120; i++) out.push({ name: `${names[i % names.length]}${i >= names.length ? " v" + (1 + Math.floor(i / names.length)) : ""}`, pack: i % 3 === 0 ? "Other maps" : "Epic Showdown 707 map pack", enabled: i % 3 === 0 || i % 17 === 0, size_kb: 40 + (i * 37) % 900 });
  return out;
};
const state = {
  orgMaps: fakeMaps(),
  cfg: { server: "", room: "", game_dir: hash.includes("fresh") ? "" : "/Users/me/Library/Application Support/GeneralsZH/game/ZeroHour", steam_user: "", setup_done: !hash.includes("fresh"), check_updates: true, player_name: "", default_player_name: "Michaels-Mac", version: "0.1.0-alpha.3", os: "macos" },
  mods: [{ id: "shockwave", name: "C&C: Shockwave", description: "A large fan-made overhaul of Zero Hour with new units, generals and balance (ModDB Editor's Choice 2008).", homepage: "https://www.moddb.com/mods/cc-shockwave", download_page: "https://www.moddb.com/mods/cc-shockwave/downloads", path: "Shockwave", detect: ["shockwave"], pick: true, notes: "On the download page choose the full release. It arrives as a Windows installer, usually inside a .zip. Drop that file here. Rallypoint unpacks the game archives and does not start Shockwave's own launcher, so the same download works on macOS and Windows.", args: [], installed: hash.includes("modinstalled") ? { folder: "Shockwave", bigs: 12, mb: 280 } : null },
    { id: "shockwave-chaos", name: "C&C: Shockwave Chaos", description: "A deliberately over-the-top add-on for Shockwave with lots of new and reworked units, generals and abilities.", homepage: "https://www.moddb.com/mods/cc-shockwave-chaos", download_page: "https://www.moddb.com/mods/cc-shockwave-chaos/downloads", path: "ShockwaveChaos", detect: ["chaos"], addon_for: "shockwave", pick: true, notes: "An add-on: C&C: Shockwave must be installed first. Rallypoint combines it with Shockwave when you launch.", args: [], installed: null },
    { id: "cold-war-crisis", name: "Cold War Crisis", description: "A total conversion of Zero Hour (version 469), with its own movies, cursors and content.", homepage: "https://www.moddb.com/mods/cold-war-crisis", download_page: "https://www.moddb.com/mods/cold-war-crisis", path: "ColdWarCrisis", detect: ["cwc"], pick: true, notes: "Download the manual installation .zip from ModDB (about 550 MB) and choose it here; no installer is run. It replaces the game's content, so it can't be played together with Shockwave or Chaos.", args: [], conflicts: ["shockwave", "shockwave-chaos"], installed: hash.includes("modinstalled") ? { folder: "ColdWarCrisis", bigs: 1, mb: 833 } : null }],
  other: [{ folder: "Control Bar HD", bigs: 2, mb: 24 }, ...(hash.includes("modinstalled") ? [{ folder: "My Custom Mod", bigs: 2, mb: 14 }, { folder: "Better Sounds", bigs: 1, mb: 9 }] : [])],
  job: null, watch: false,
};
function advance() {
  const j = state.job; if (!j) return;
  j.tick++;
  if (j.tick < 8 && !j.codeMode) { j.s = { phase: "LoggingIn", percent: 0, message: "Approve this sign-in in the Steam Mobile app on your phone", need: null, phone: true, log: "STEAM GUARD! Use the Steam Mobile App to confirm your sign in..." }; return; }
  if (j.codeMode && !j.coded) { if (!j.s.need) j.s = { phase: "LoggingIn", percent: 0, message: "Enter the Steam Guard code from your authenticator app", need: "Guard", phone: false, log: j.s.log }; return; }
  const t = j.tick - 8;
  if (t < 30) { j.s = { phase: "Downloading", percent: t * 3.4, message: "Downloading Zero Hour…", need: null, phone: false, log: j.s.log }; return; }
  j.s = { phase: "Done", percent: 100, message: "Game files are ready", need: null, phone: false, log: j.s.log };
  state.cfg.game_dir = "/Users/me/Library/Application Support/GeneralsZH/game/ZeroHour";
}
setInterval(advance, 500);
const handlers = {
  get_config: () => structuredClone(state.cfg),
  maps_info: () => ({ dir: "/Users/me/Documents/Command and Conquer Generals Zero Hour Data/Maps", count: state.mapCount || 0, packs: [
    { id: "gentool-all", name: "All ranked maps", description: "Every ranked 1v1, 2v2 and 3v3 map from GenTool's download page, in one pack.", size_mb: 56 },
    { id: "gentool-1v1", name: "1v1 maps", description: "The ranked one-on-one maps.", size_mb: 6 },
    { id: "gentool-2v2", name: "2v2 maps", description: "The ranked two-on-two maps.", size_mb: 4 },
    { id: "gentool-3v3", name: "3v3 maps", description: "The ranked three-on-three maps.", size_mb: 4 },
    { id: "epic-showdown-707", name: "Epic Showdown 707 map pack", description: "R1ckhy's collection of 780 community maps. Everything starts switched off: pick the maps you want in the organiser.", size_mb: 248, manual: true, start_disabled: true }] }),
  maps_organiser: () => state.orgMaps,
  maps_set_enabled: ({ names, enabled }) => { let n = 0; state.orgMaps.forEach((m) => { if (names.includes(m.name)) { m.enabled = enabled; n++; } }); return n; },
  maps_delete: ({ names }) => { const before = state.orgMaps.length; state.orgMaps = state.orgMaps.filter((m) => !names.includes(m.name)); return before - state.orgMaps.length; },
  maps_open_pack_page: () => {},
  map_view: ({ name }) => {
    const w = 128, h = 128, px = new Uint8Array(w * h * 4);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const k = (y * w + x) * 4, n = Math.sin(x / 9) * Math.cos(y / 11) * 40;
      px[k] = 70 + n; px[k + 1] = 110 + n * 1.3; px[k + 2] = 60 + n * .5; px[k + 3] = 255;
    }
    let bin = ""; for (let k = 0; k < px.length; k++) bin += String.fromCharCode(px[k]);
    const solo = name.includes("Mission");
    return { name, image_w: w, image_h: h, rgba: btoa(bin), size_kb: 190, info: { width: 2800, height: 2400, starts: solo ? [] : [{ number: 1, x: .18, y: .42 }, { number: 2, x: .18, y: .2 }, { number: 3, x: .82, y: .8 }, { number: 4, x: .82, y: .58 }] } };
  },
  maps_install_pack: () => { state.mapJob = { tick: 0 }; },
  maps_status: () => { const j = state.mapJob; if (!j) return { phase: "Idle", percent: 0, message: "", installed: 0 }; j.tick++; if (j.tick < 10) return { phase: "Downloading", percent: j.tick * 8, message: `Downloading All ranked maps (${j.tick * 5} MB so far)`, installed: 0 }; state.mapCount = 96; return { phase: "Done", percent: 100, message: "96 maps installed", installed: 96 }; },
  maps_cancel: () => { state.mapJob = null; },
  maps_install_path: () => 3,
  set_player_name: ({ name }) => { state.cfg.player_name = name.trim(); return name.trim() || "Michaels-Mac"; },
  set_check_updates: ({ on }) => { state.cfg.check_updates = on; },
  update_check: async () => (await new Promise((r) => setTimeout(r, 1200)), hash.includes("update") ? { current: "0.1.0-alpha.3", latest: "0.1.0-alpha.4", page: "https://github.com/michaelmurfy/rallypoint/releases/tag/v0.1.0-alpha.4", notes: "", size_mb: 9, installer: true } : null),
  update_install: async () => { await new Promise((r) => setTimeout(r, 1500)); return "Restarting…"; },
  whats_new_take: async () => hash.includes("whatsnew") ? { version: "0.3.0", notes: "## Highlights\n\nStill a **prerelease**.\n\n- **Seamless updates.** The app installs the new version and opens again by itself.\n- **Multiplayer fixes.** Sync logs now name the frame that diverged.\n- A `code` word stays monospaced.\n\n## Also in this build\n\n- Smoother pages.\n- No autocorrect in text fields.\n\n---\n\n## Earlier\n\n" + Array.from({ length: 12 }, (_, i) => `- Note ${i + 1} is long enough that the dialog has to scroll instead of clipping the last lines.`).join("\n") } : null,
  open_release_page: () => {},
  detect_games: () => (hash.includes("steam") ? [{ dir: "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Command & Conquer Generals - Zero Hour", zero_hour: true }] : []),
  check_game_dir: ({ path }) => (path.includes("ZeroHour") ? { ok: true, message: "Zero Hour found" } : path.trim() ? { ok: false, message: "No Zero Hour files in that folder (INIZH.big is missing)" } : { ok: false, message: "Choose the folder that contains INIZH.big" }),
  save_game_dir: ({ path }) => { state.cfg.game_dir = path.trim(); },
  save_config: ({ server, room, steamUser }) => { Object.assign(state.cfg, { server, room, steam_user: steamUser }); },
  set_setup_done: ({ done }) => { state.cfg.setup_done = done; },
  assets_start: ({ user }) => { state.cfg.steam_user = user; state.job = { tick: 0, s: { phase: "LoggingIn", percent: 0, message: "Signing in to Steam…", need: null, phone: false, log: "" } }; },
  assets_status: () => (state.job ? state.job.s : { phase: "Idle", percent: 0, message: "", need: null, phone: false, log: "" }),
  assets_use_code: () => { if (state.job) state.job.codeMode = true; },
  assets_submit: () => { if (state.job) { state.job.coded = true; state.job.tick = 8; state.job.s = { ...state.job.s, need: null, message: "Signing in to Steam…" }; } },
  assets_cancel: () => { state.job = null; },
  mods_list: () => ({ catalog: state.mods, other: state.other }),
  mods_get: () => { state.watch = Date.now(); },
  mods_poll: () => (state.watch && Date.now() - state.watch > 5000 ? { state: "found", id: "shockwave", message: "ShockWave_1.201.zip", path: "/Users/me/Downloads/ShockWave_1.201.zip" } : { state: state.watch ? "waiting" : "idle", id: "shockwave", message: "", path: "" }),
  downloads_dir: () => "/Users/me/Downloads",
  mods_cancel_watch: () => { state.watch = false; },
  mods_install: async ({ path, id }) => { await new Promise((r) => setTimeout(r, 1500)); state.watch = false; if (id) { const m = state.mods.find((x) => x.id === id); m.installed = { folder: m.path, bigs: 12, mb: 280 }; return m.installed; } return { folder: path.split("/").pop().replace(/\.\w+$/, ""), bigs: 3, mb: 20 }; },
  mods_remove: ({ folder }) => { state.mods.forEach((x) => { if (x.path === folder) x.installed = null; }); state.other = state.other.filter((o) => o.folder !== folder); },
  diagnose: () => ({ checks: [{ name: 'Game engine', ok: true, detail: 'engine.exe' }, { name: 'Zero Hour files', ok: true, detail: 'C:/Games/ZH' }, { name: 'Generals (base game)', ok: false, detail: 'Not found. Zero Hour also needs the original Generals installed or downloaded from Settings.' }], crash: 'Release Crash at Mon Oct 25\nException code: C0000005', engine_log: '[launcher] 1920x1080 fullscreen=false relay=agent 127.0.0.1:5000, virtual IP 10.77.0.2\n[net] relay on: agent 7F000001:5000, virtual IP 0A4D0002, logical port 8086', data_dir: 'x' }), open_game_data_folder: () => {},
  set_sync_logs: () => {}, make_debug_bundle: async () => { await new Promise((r) => setTimeout(r, 600)); return "/Users/me/Downloads/rallypoint-debug-2026-10-26-100000.zip"; }, open_sync_logs_folder: () => {}, sync_compare: async () => { await new Promise((r) => setTimeout(r, 700)); return { kind: "Differs", frames_before: 99, at: { frame: 100, line: 37, mine: "100:00037 obj 12 pos=(412.5,230.1)", theirs: "100:00037 obj 12 pos=(412.5,230.100006)" } }; },
  set_camera_height: () => {}, graphics_apply_high: async () => { await new Promise((r) => setTimeout(r, 600)); },
  open_mods_folder: () => {}, open_download_page: () => {}, open_homepage: () => {},
  relay_diagnostics: () => (hash.includes("online") ? { rtt_ms: 42, avg_ms: 45, jitter_ms: 6, loss_pct: 0, samples: 20, up_kbps: 38, down_kbps: 61, server_identity: "3F2A-91C0-77AB-0E15", identity_saved: true, desync: hash.includes("desync") ? { frame: 100, certain: false, players: [{ vip: "10.77.0.1", crc: "12C26F6A", odd: false }, { vip: "10.77.0.2", crc: "E67396AE", odd: false }] } : null, server: { version: "0.1.0-beta.4", uptime_secs: 273600, rooms: 4, players: 11, max_rooms: 256, max_players: 2048, room_players: 3, load_pct: 2, pps: 420, kbps: 310 } } : null),
  room_password_save: ({ server, room, password }) => { state.pw = state.pw || {}; const k = (server + "|" + room).toLowerCase(); if (password) state.pw[k] = true; else delete state.pw[k]; },
  room_passwords_saved: ({ pairs }) => pairs.map(([s, r]) => !!(state.pw || {})[(s + "|" + r).toLowerCase()]),
  relay_connection: () => (hash.includes("noanswer") ? { address: "203.0.113.5:7777", stage: "contacting the server", waited_secs: 14, replies: 0, reconnects: 0, address_changed: false, hint: "Nothing has answered from 203.0.113.5:7777 in 14 seconds. Check the server address and port, that the server is running, and that no firewall or router is blocking UDP to that port." } : { address: "203.0.113.5:7777", stage: "connected", waited_secs: 0, replies: 40, reconnects: 0, address_changed: false, hint: "" }),
  relay_status: () => (hash.includes("noanswer") ? "Connecting..." : hash.includes("wrongpw") ? "Wrong room password" : hash.includes("online") ? "Connected as 10.77.0.2" : "Offline"), relay_connect: () => {}, relay_disconnect: () => {},
  icons_install: () => { const row = { folder: "Control Bar HD", bigs: 2, mb: 7 }; if (!state.other.some((o) => o.folder === row.folder)) state.other.push(row); return row; },
  launch_game: () => {},
};
window.__TAURI__ = {
  core: { invoke: async (cmd, args) => { if (!(cmd in handlers)) throw new Error("mock: no handler for " + cmd); return handlers[cmd](args || {}); } },
  webview: { getCurrentWebview: () => ({ onDragDropEvent: () => {} }) },
  dialog: { open: async () => "/Users/me/Downloads/ShockWave_1.201.zip" },
};
