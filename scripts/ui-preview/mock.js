// A fake Tauri backend so the launcher UI can be opened in a plain browser:
//   open scripts/ui-preview/index.html   (add #fresh for first run, #steam for a detected install,
//   #ra2 for Red Alert 2 mode (#ra2nofiles no game files, #ra2noengine, #ra2nomaps, #ra2slow loading maps, #ra2maperr unreadable map), #ra2fresh for its first run, #ra2online / #ra2guest for the lobby as host / guest,
//   #ra2guestchanged: the host changes the speed 2.5 s in, #ra2guestready: a guest who is ready)
//   Sending maps to the room: #share (connected, three other players), #sharepending (an offer waiting), #shareconflict (an offer with the
//   name of a map you have), #sharenosupport (an old server), #shareempty (nobody else), #shareoffline; add #ra2 for Red Alert 2 (#ra2share)
const hash = location.hash;
const fakeMaps = () => {
  const names = ["! 1v1 ! super no money map v2", "!!! no money !!! lots of fun3", "!2v2 cxn", "Tournament Desert", "Twilight Flame", "Defcon 6", "Mountain Pass", "Arctic Convoy", "Bear Town", "Fallen Empire", "Winter Wolves", "Dust Bowl 2v2", "Golden Gate", "Oasis 3v3"];
  const out = [];
  for (let i = 0; i < 120; i++) out.push({ name: `${names[i % names.length]}${i >= names.length ? " v" + (1 + Math.floor(i / names.length)) : ""}`, pack: i % 3 === 0 ? "Other maps" : "Epic Showdown 707 map pack", enabled: i % 3 === 0 || i % 17 === 0, size_kb: 40 + (i * 37) % 900 });
  return out;
};
const state = {
  orgMaps: fakeMaps(),
  cfg: { server: "", room: "", game_dir: hash.includes("fresh") ? "" : "/Users/me/Library/Application Support/GeneralsZH/game/ZeroHour", steam_user: "", setup_done: !hash.includes("fresh"), check_updates: true, beta_updates: false, version_is_beta: hash.includes("onbeta"), engine_arch: "x64", renderer: "enhanced", effects: [["PostProcessing","yes"],["Bloom","yes"],["AmbientOcclusion","yes"],["AmbientOcclusionQuality","half"],["AntiAliasingFXAA","yes"],["SoftShadows","yes"],["ShadowQuality","medium"],["DynamicLights","yes"],["PixelLighting","yes"],["AnisotropicFiltering","16"],["CloudShadows","yes"],["Clouds","no"],["UpdatedWater","yes"],["WaterReflections","yes"],["Shockwaves","yes"],["ScorchRelief","yes"],["BloomStrength","normal"],["ColorLook","standard"],["Sharpen","off"],["Vignette","no"],["SoftParticles","yes"],["SmokeShading","yes"]], effect_preset: "balanced", engines_bundled: hash.includes("win"), editor_available: hash.includes("win"), renderer_available: true, player_name: "", default_player_name: "Michaels-Mac", version: "0.1.0-alpha.3", os: "macos",
    game: hash.includes("ra2") ? "ra2" : "generals", ra2_engine: !hash.includes("noengine"),
    ra2: { game_dir: hash.includes("ra2fresh") || hash.includes("ra2nofiles") ? "" : "/Users/me/Library/Application Support/GeneralsZH/game/RA2", width: 0, height: 0, windowed: false, logical_height: 0, sound: true, side: -1, color: 2, setup_done: !hash.includes("ra2fresh") } },
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
const ra2Maps = [["Little Big Lake", 4, "yr"], ["Golden State Fwy", 6, "yr"], ["Heck Freezes Over", 8, "ra2"], ["Isle of War", 2, "yr"], ["Dune Patrol", 2, "ra2"], ["Tour of Egypt", 6, "ra2"], ["Kill Bill", 2, "yr"], ["Cold War", 4, "ra2"], ["Mission: Last Stand", 1, "yr"]]
  .map(([name, players, game], i) => ({ path: `/Users/me/Library/Application Support/GeneralsZH/RA2Maps/${name.replace(/[^A-Za-z ]/g, "")}.${game === "yr" ? "yrm" : "mpr"}`, name, players, game, size_kb: 60 + i * 23 }));
const lobby = { seat: { side: -1, color: -1, start: -1, team: -1, ready: false }, settings: null, changedAt: 0 };
const guestHost = hash.includes("ra2guest");
// what the guest sees of the host's room; any change clears the guest's ready, as in ra2lobby
const guestRoom = { map: "Little Big Lake.yrm", map_name: "Little Big Lake", starts: 4, credits: 10000, speed: 2, ai: [{ difficulty: "brutal", side: 7, color: 6, start: 3, team: 1 }] };
if (hash.includes("changed")) { setTimeout(() => { lobby.seat.ready = true; }, 1000); setTimeout(() => { guestRoom.speed = 0; lobby.seat.ready = false; }, 2500); }
const handlers = {
  set_game: ({ game }) => { state.cfg.game = game; },
  ra2_country_flags: () => [],
  ra2_detect: () => (hash.includes("steam") ? [{ dir: "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Command & Conquer Red Alert II" }] : []),
  ra2_check_dir: ({ path }) => (/RA2|Red Alert/i.test(path) ? (state.cfg.ra2.game_dir = path.trim(), { ok: true, message: "Yuri's Revenge found" }) : path.trim() ? { ok: false, message: "That folder has no Red Alert 2 and Yuri's Revenge (ra2md.mix)" } : { ok: false, message: "Choose the folder that contains ra2md.mix" }),
  ra2_set_settings: (a) => { Object.assign(state.cfg.ra2, a); },
  ra2_maps: async () => (await new Promise((r) => setTimeout(r, hash.includes("slow") ? 60000 : 200)), hash.includes("nomaps") ? [] : ra2Maps),
  ra2_map_view: ({ file }) => {
    if (hash.includes("maperr")) throw new Error("the map file is truncated");
    const m = ra2Maps.find((x) => x.path.endsWith("/" + file)) || ra2Maps[0];
    const v = handlers.map_view({ name: m.name });
    const pts = [[.2, .8], [.8, .2], [.8, .8], [.2, .2], [.5, .9], [.5, .1], [.1, .5], [.9, .5]];
    return { ...v, game: m.game, cells_w: 120, cells_h: 100, info: { width: 1200, height: 1000, starts: pts.slice(0, m.players).map(([x, y], i) => ({ number: i + 1, x, y })) } };
  },
  ra2_community_packs: async () => ({
    packs: [
      { id: "ra2-1", name: "RA2 map pack 1", file: "ra2mappack1.zip", game: "ra2", maps: 3, sha256: "" },
      { id: "yr-5", name: "Yuri's Revenge map pack 5", file: "yrmappack5.zip", game: "yr", maps: 13, sha256: "" },
    ],
    installed_files: hash.includes("nomaps") ? 0 : 6,
    available: true,
  }),
  ra2_install_community_packs: async () => (await new Promise((r) => setTimeout(r, 400)), [13, 46]),
  ra2_open_maps_folder: () => {},
  ra2_launch: ({ setup }) => { window.__lastRa2 = setup; if (!state.cfg.ra2_engine) throw new Error("The RA2 engine is not in this build yet"); },
  ra2_lobby_seat: ({ seat }) => {
    const changed = ["side", "color", "start", "team"].some((k) => seat[k] !== lobby.seat[k]);
    lobby.seat = { ...seat };
    if (changed && !guestHost && lobby.settings) lobby.changedAt = Date.now();
  },
  ra2_lobby_room: ({ map, credits, speed, ai }) => {
    const m = ra2Maps.find((x) => x.path.endsWith("/" + map)), next = { map, map_name: m.name, starts: m.players, credits, speed, ai };
    if (JSON.stringify(next) !== JSON.stringify(lobby.settings)) { lobby.settings = next; lobby.changedAt = Date.now(); }
  },
  ra2_lobby_start: () => { throw new Error("Waiting for Dana to be ready"); },
  ra2_lobby_poll: () => {
    if (!hash.includes("online") && !guestHost) return null;
    const me = { id: 2, name: "Michaels-Mac", host: !guestHost, me: true, ...lobby.seat, ...(hash.includes("guestready") ? { ready: true } : {}) };
    // Sam readies again 4 s after the host's last change; Dana never does
    const samReady = guestHost || Date.now() - lobby.changedAt > 4000;
    const others = [{ id: 1, name: "Sam", host: guestHost, me: false, side: 8, color: 1, start: 1, team: 0, ready: samReady }, { id: 3, name: "Dana", host: false, me: false, side: 9, color: 3, start: -1, team: -1, ready: false }];
    const settings = guestHost ? structuredClone(guestRoom) : lobby.settings;
    return { me: 2, host: guestHost ? 1 : 2, am_host: !guestHost, players: [others[0], me, others[1]], settings, blocked: guestHost ? "" : !settings ? "Choose a map first" : `Waiting for ${samReady ? "Dana" : "Sam"} to be ready`, start: null };
  },
  get_config: () => structuredClone(state.cfg),
  maps_info: () => ({ dir: "/Users/me/Documents/Command and Conquer Generals Zero Hour Data/Maps", count: state.mapCount || 0, packs: [
    { id: "gentool-all", name: "All ranked maps", description: "Every ranked 1v1, 2v2 and 3v3 map from GenTool's download page, in one pack.", size_mb: 56 },
    { id: "gentool-1v1", name: "1v1 maps", description: "The ranked one-on-one maps.", size_mb: 6 },
    { id: "gentool-2v2", name: "2v2 maps", description: "The ranked two-on-two maps.", size_mb: 4 },
    { id: "gentool-3v3", name: "3v3 maps", description: "The ranked three-on-three maps.", size_mb: 4 },
    { id: "epic-showdown-707", name: "Epic Showdown 707 map pack", description: "R1ckhy's collection of 780 community maps. Everything starts switched off: pick the maps you want in the organiser.", size_mb: 248, manual: true, start_disabled: true }] }),
  maps_organiser: () => state.orgMaps,
  maps_official: () => [["Alpine Assault", 2, 333], ["Armored Fury", 6, 237], ["Bear Town Beatdown", 4, 331], ["Death Valley", 8, 398], ["Defcon6", 6, 287], ["Golden Oasis", 4, 632], ["Tournament Desert", 2, 205], ["Tournament City", 6, 441], ["Twilight Flame", 8, 455], ["Winter Wolf", 2, 257]].map(([name, players, size_kb]) => ({ name, players, size_kb, games: ["zh"] }))
    .concat([["Shockwave Crossroads", 4], ["Frozen Pass", 2]].map(([name, players]) => ({ name, players, size_kb: 300, games: ["shockwave"] })), [{ name: "Chaos Arena", players: 6, size_kb: 410, games: ["shockwave-chaos"] }, { name: "Cold Front", players: 4, size_kb: 380, games: ["cold-war-crisis"] }]),
  official_map_view: (a) => handlers.map_view(a),
  skirmish_ai_names: async ({ modFolders }) => ((modFolders || []).includes("ShockwaveChaos") ? ["Noob Army", "Normie", "Turbo Death Army"] : ["Easy Army", "Medium Army", "Hard Army"]),
  skirmish_generals: async ({ modFolders }) => {
    const base = [["FactionAmerica", "USA"], ["FactionChina", "China"], ["FactionGLA", "GLA"], ["FactionAmericaLaserGeneral", "USA Laser General"], ["FactionChinaTankGeneral", "China Tank General"], ["FactionGLAToxinGeneral", "GLA Toxin General"]];
    const shock = [["FactionAmerica", "USA"], ["FactionChina", "China"], ["FactionGLA", "GLA"], ["FactionShockHero", "Shockwave Hero"], ["FactionShockElite", "Shockwave Elite"]];
    return (modFolders || []).includes("Shockwave") ? shock.map(([id, name]) => ({ id, name })) : base.map(([id, name]) => ({ id, name }));
  },
  maps_players: async () => { await new Promise((r) => setTimeout(r, 500)); const o = {}; state.orgMaps.forEach((m, i) => { o[m.name] = /Mission/.test(m.name) ? 0 : [2, 2, 4, 3, 6, 8][i % 6]; }); return o; },
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
  update_check: async () => (await new Promise((r) => setTimeout(r, 300)), state.cfg.beta_updates ? { current: "0.6.1", latest: "0.7.0-beta.2", page: "https://github.com/michaelmurfy/rallypoint/releases/tag/v0.7.0-beta.2", notes: "## Rallypoint 0.7.0-beta.2\n\n**The game can look newer.**\n\n- **Better smoke.** Darker near the ground.\n- **Glow you can tune.**", size_mb: 212, installer: true, beta: true, skipped: !!state.skipped } : hash.includes("update") ? { current: "0.6.0", latest: "0.6.1", page: "https://github.com/michaelmurfy/rallypoint/releases/tag/v0.6.1", notes: "", size_mb: 9, installer: true, beta: false, skipped: !!state.skipped } : null),
  set_beta_updates: ({ on }) => { state.cfg.beta_updates = on; }, update_skip: () => { state.skipped = true; },
  update_install: async () => { window.__upd = Date.now(); await new Promise((r) => setTimeout(r, 7000)); window.__upd = 0; return "Restarting…"; },
  game_art: (a) => { const c = a && a.gameFolder ? ["#d98a2b", "#7a3b1c"] : ["#3b7be0", "#14305f"]; return "data:image/svg+xml;utf8," + encodeURIComponent(`<svg xmlns="http://www.w3.org/2000/svg" width="800" height="600"><defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="${c[0]}"/><stop offset=".6" stop-color="${c[1]}"/><stop offset="1" stop-color="#101010"/></linearGradient></defs><rect width="800" height="600" fill="url(#g)"/><circle cx="560" cy="250" r="140" fill="#fff" opacity=".35"/><path d="M0 460 L180 330 L330 430 L520 300 L800 470 V600 H0Z" fill="#0c0c14"/></svg>`); },
  update_progress: () => {
    const t = window.__upd ? (Date.now() - window.__upd) / 1000 : 0, total = 46 * 1048576;
    return t < 4.5 ? { phase: "Downloading", done: Math.min(total, (t / 4.5) * total), total } : t < 6.2 ? { phase: "Installing", done: total, total } : { phase: "Restarting", done: total, total };
  },
  whats_new_take: async () => hash.includes("whatsnew") ? { version: "0.3.0", notes: "## Highlights\n\nStill a **prerelease**.\n\n- **Seamless updates.** The app installs the new version and opens again by itself.\n- **Multiplayer fixes.** Sync logs now name the frame that diverged.\n- A `code` word stays monospaced.\n\n## Also in this build\n\n- Smoother pages.\n- No autocorrect in text fields.\n\n---\n\n## Earlier\n\n" + Array.from({ length: 12 }, (_, i) => `- Note ${i + 1} is long enough that the dialog has to scroll instead of clipping the last lines.`).join("\n") } : null,
  changelog: () => "## Rallypoint 0.5.6\n\nFixes black units on Mac.\n\n- **Team colours on Mac.** Units draw with the player colour.\n\n## Rallypoint 0.5.5\n\nMultiplayer opens the LAN lobby.",
  open_release_page: () => {},
  detect_games: () => (hash.includes("steam") ? [{ dir: "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Command & Conquer Generals - Zero Hour", zero_hour: true }] : []),
  check_game_dir: ({ path }) => (path.includes("ZeroHour") ? { ok: true, message: "Zero Hour found" } : path.trim() ? { ok: false, message: "No Zero Hour files in that folder (INIZH.big is missing)" } : { ok: false, message: "Choose the folder that contains INIZH.big" }),
  save_game_dir: ({ path }) => { state.cfg.game_dir = path.trim(); },
  save_config: ({ server, room, steamUser }) => { Object.assign(state.cfg, { server, room, steam_user: steamUser }); },
  set_setup_done: ({ done }) => { state.cfg.setup_done = done; },
  set_ra2_setup_done: ({ done }) => { state.cfg.ra2.setup_done = done; },
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
  set_sync_logs: () => {}, steam_forget_login: async () => 1, make_debug_bundle: async () => { await new Promise((r) => setTimeout(r, 600)); return "/Users/me/Downloads/rallypoint-debug-2026-10-26-100000.zip"; }, open_sync_logs_folder: () => {}, sync_compare: async () => { await new Promise((r) => setTimeout(r, 700)); return { kind: "Differs", frames_before: 99, at: { frame: 100, line: 37, mine: "100:00037 obj 12 pos=(412.5,230.1)", theirs: "100:00037 obj 12 pos=(412.5,230.100006)" } }; },
  set_camera_height: () => {}, set_control_bar: async ({ choice }) => { await new Promise((r) => setTimeout(r, 400)); state.other = state.other.filter((o) => o.folder !== "Control Bar Pro"); if (choice === "pro") state.other.push({ folder: "Control Bar Pro", bigs: 5, mb: 17 }); }, set_engine_arch: () => {}, set_renderer: () => {}, set_effect: () => "custom", set_effect_preset: ({ name }) => state.cfg.effects.map(([k, v]) => [k, name === "cinematic" && k === "ColorLook" ? "cinematic" : name === "cinematic" && k === "Vignette" ? "yes" : v]), graphics_apply_high: async () => { await new Promise((r) => setTimeout(r, 600)); },
  open_mods_folder: () => {}, open_download_page: () => {}, open_homepage: () => {},
  relay_set_ai: ({ level }) => { state.roomAi = level; },
  relay_diagnostics: () => (hash.includes("online") || hash.includes("ra2guest") ? { rtt_ms: 42 + Math.round(18 * Math.sin(Date.now() / 7000) + 10 * Math.random()), avg_ms: 45, jitter_ms: 6, loss_pct: Math.random() < 0.12 ? 2 : 0, samples: 20, up_kbps: 38 + Math.round(20 * Math.random()), down_kbps: 61 + Math.round(40 * Math.random()), server_identity: "3F2A-91C0-77AB-0E15", identity_saved: true, room_master: !hash.includes("guest"), ai_level: state.roomAi || 0, desync: hash.includes("desync") ? { frame: 100, certain: false, players: [{ vip: "10.77.0.1", crc: "12C26F6A", odd: false }, { vip: "10.77.0.2", crc: "E67396AE", odd: false }] } : null, server: { version: "0.1.0-beta.4", uptime_secs: 273600, rooms: 4, players: 11, max_rooms: 256, max_players: 2048, room_players: 3, load_pct: 2 + Math.round(10 * Math.random()), pps: 420, kbps: 310 } } : null),
  room_password_save: ({ server, room, password }) => { state.pw = state.pw || {}; const k = (server + "|" + room).toLowerCase(); if (password) state.pw[k] = true; else delete state.pw[k]; },
  room_passwords_saved: ({ pairs }) => pairs.map(([s, r]) => !!(state.pw || {})[(s + "|" + r).toLowerCase()]),
  relay_connection: () => (hash.includes("noanswer") ? { address: "203.0.113.5:7777", stage: "contacting the server", waited_secs: 14, replies: 0, reconnects: 0, address_changed: false, hint: "Nothing has answered from 203.0.113.5:7777 in 14 seconds. Check the server address and port, that the server is running, and that no firewall or router is blocking UDP to that port." } : { address: "203.0.113.5:7777", stage: "connected", waited_secs: 0, replies: 40, reconnects: 0, address_changed: false, hint: "" }),
  relay_status: () => (hash.includes("ra2online") || hash.includes("ra2guest") ? "Connected as 10.77.0.2" : hash.includes("noanswer") ? "Connecting..." : hash.includes("wrongpw") ? "Wrong room password" : hash.includes("online") ? "Connected as 10.77.0.2" : "Offline"), relay_connect: () => {}, relay_disconnect: () => {},
  icons_install: () => { const row = { folder: "Control Bar HD", bigs: 2, mb: 7 }; if (!state.other.some((o) => o.folder === row.folder)) state.other.push(row); return row; },
  launch_game: (a) => { window.__lastLaunch = a; },
  launch_editor: (a) => { window.__lastEditor = a; },
};

// ---- sending maps to the room (mapshare.js). One fake room; offers advance with the clock, whatever the page does.
const shareOn = hash.includes("share") && !hash.includes("shareoffline");
const shareRa2 = hash.includes("ra2");
const sh = { auto: false, id: 1, incoming: [], outgoing: [], peers: hash.includes("shareempty") ? [] : [
  { vip: "10.77.0.1", name: "Sam", game: shareRa2 ? "ra2" : "generals", master: true },
  { vip: "10.77.0.3", name: "Dana", game: shareRa2 ? "ra2" : "generals", master: false },
  { vip: "10.77.0.4", name: "Kim", game: shareRa2 ? "generals" : "ra2", master: false }] };
const shGame = shareRa2 ? "ra2" : "generals";
if (hash.includes("sharepending") || hash.includes("shareconflict")) sh.incoming.push({ id: 41, from: "10.77.0.1", from_name: "Sam", name: shareRa2 ? "Dune Patrol.map" : "Tournament Desert", game: shGame, size: 2200000, players: 4, master: true, conflict: hash.includes("shareconflict"), state: "prompt", percent: 0, note: "", installed: null, seconds_left: 100, t: 0 });
const shTick = () => {
  const now = Date.now();
  for (const o of sh.outgoing) {
    if (o.state === "waiting" && now - o.t > 2500) { if (o.to === "10.77.0.3") { o.state = "declined"; o.note = "Declined"; } else { o.state = "sending"; o.t = now; } }
    else if (o.state === "sending") { o.percent = Math.min(100, Math.round((now - o.t) / 60)); if (o.percent >= 100) { o.state = "done"; o.note = "Delivered"; } }
  }
  for (const i of sh.incoming) {
    if (i.state === "receiving") { i.percent = Math.min(100, Math.round((now - i.t) / 50)); if (i.percent >= 100) { i.state = "done"; i.installed = i.installed || i.name; i.note = `Saved as ${i.installed}` + (i.how === "keep" ? ", because you have a different map with that name" : i.how === "replace" ? ", in place of your map of that name (the old one is kept in the backup folder)" : ""); } }
  }
};
Object.assign(handlers, {
  map_share_view: () => {
    shTick();
    if (!shareOn) return { supported: null, connected: false, game: shGame, auto_accept: false, peers: [], incoming: [], outgoing: [] };
    return { supported: hash.includes("sharenosupport") ? false : true, connected: true, game: shGame, auto_accept: sh.auto, peers: sh.peers.map((p) => ({ ...p, can_receive: p.game === shGame })), incoming: sh.incoming.map(({ t, how, ...i }) => i), outgoing: sh.outgoing.map(({ t, ...o }) => o) };
  },
  map_share_send: async ({ name, to }) => {
    if (hash.includes("sharenosupport")) throw new Error("This server does not support sending maps yet. Its owner needs to update it.");
    for (const v of to) { const p = sh.peers.find((x) => x.vip === v); sh.outgoing.push({ id: sh.id++, to: v, to_name: p.name, name, game: shGame, state: "waiting", percent: 0, note: "", t: Date.now() }); }
    return to.length;
  },
  map_share_reply: ({ from, id, reply }) => {
    const i = sh.incoming.find((x) => x.from === from && x.id === id); if (!i) return;
    if (reply === "decline" || reply === "block") { i.state = "failed"; i.note = reply === "block" ? "Declined, and you blocked this player's maps" : i.state === "receiving" ? "Cancelled" : "Declined"; return; }
    i.state = "receiving"; i.t = Date.now(); i.how = i.conflict ? (reply === "replace" ? "replace" : "keep") : ""; i.installed = i.conflict && reply !== "replace" ? i.name + " (2)" : i.name;
  },
  map_share_cancel: ({ to, id }) => { const o = sh.outgoing.find((x) => x.to === to && x.id === id); if (o) { o.state = "cancelled"; o.note = "Cancelled"; } },
  map_share_dismiss: ({ incoming, who, id }) => { const l = incoming ? sh.incoming : sh.outgoing; const k = l.findIndex((x) => (incoming ? x.from : x.to) === who && x.id === id); if (k >= 0) l.splice(k, 1); },
  map_share_auto_accept: ({ on }) => { sh.auto = on; },
});
if (shareOn) { const base = handlers.relay_status; handlers.relay_status = () => "Connected as 10.77.0.2"; void base; const bd = handlers.relay_diagnostics; handlers.relay_diagnostics = () => ({ ...(hash.includes("online") ? bd() : { rtt_ms: 40, avg_ms: 45, jitter_ms: 5, loss_pct: 0, samples: 20, up_kbps: 30, down_kbps: 50, server_identity: "3F2A-91C0-77AB-0E15", identity_saved: true, ai_level: 0, desync: null, server: null }), room_master: false }); }
window.__TAURI__ = {
  core: { invoke: async (cmd, args) => { if (!(cmd in handlers)) throw new Error("mock: no handler for " + cmd); return handlers[cmd](args || {}); } },
  webview: { getCurrentWebview: () => ({ onDragDropEvent: () => {} }) },
  dialog: { open: async () => "/Users/me/Downloads/ShockWave_1.201.zip" },
};
// #...@online opens the Online page; #ra2online...hostchange changes the credits 6 s in (Sam's ready clears for 4 s)
if (hash.includes("@online")) { const t = setInterval(() => { if (store.page === "play") { clearInterval(t); go("online"); } }, 100); }
if (hash.includes("hostchange")) setTimeout(() => { if (lobby.settings) { lobby.settings = { ...lobby.settings, credits: 20000 }; lobby.changedAt = Date.now(); } }, 6000);
// #page-maps, #page-settings ... open that page after start
{
  const pg = hash.match(/page-(\w+)/);
  const click = (sel) => setTimeout(() => { const el = document.querySelector(sel); el && el.click(); }, 900);
  if (pg) click(`.nav[data-page="${pg[1]}"]`);
  // #switchdemo: presses the other game 1.2 s in, to watch the switch animation
  if (hash.includes("switchdemo")) setTimeout(() => { const b = document.querySelector('.gseg[aria-pressed="false"]'); b && b.click(); }, 1200);
}
