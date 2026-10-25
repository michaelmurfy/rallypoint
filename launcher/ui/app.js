const { invoke } = window.__TAURI__.core;
const $ = (id) => document.getElementById(id);
const show = (id, on) => { $(id).hidden = !on; };

// Everything from the backend is put in the page as text or option values, never as markup.
function option(select, text, value) { select.add(new Option(text, value)); }

$("w").value = Math.round(screen.width * devicePixelRatio);
$("h").value = Math.round(screen.height * devicePixelRatio);

function loadGames() {
  invoke("detect_games").then((games) => {
    $("game").replaceChildren();
    games.forEach((g) => option($("game"), `${g.zero_hour ? "Zero Hour" : "Generals"} - ${g.dir}`, g.dir));
    if (!games.length) option($("game"), "No install found", "");
  });
}
loadGames();
invoke("installed_mods").then((mods) => mods.forEach((m) => option($("mod"), m.name, m.id)));
invoke("get_config").then((c) => {
  $("dir").value = c.game_dir; $("srv").value = c.server; $("room").value = c.room; $("suser").value = c.steam_user;
});

const saveConfig = () => invoke("save_config", { server: $("srv").value, room: $("room").value, steamUser: $("suser").value });

// ---- play
const play = (skirmish) => invoke("launch_game", {
  gameDir: $("dir").value.trim() || $("game").value, modId: $("mod").value || null,
  width: +$("w").value, height: +$("h").value, fullscreen: $("fs").checked, skirmish,
}).then(() => { $("msg").textContent = "Launched."; }, (e) => { $("msg").textContent = e; });
$("go").onclick = () => play(false);
$("sk").onclick = () => play(true);

// ---- online
$("net").onclick = async () => {
  try {
    if ($("net").textContent === "Connect") {
      await saveConfig();
      await invoke("relay_connect", { password: $("rpass").value });
    } else {
      await invoke("relay_disconnect");
    }
  } catch (e) { $("relay").textContent = e; }
};
setInterval(() => invoke("relay_status").then((s) => {
  $("relay").textContent = s;
  $("net").textContent = s === "Connecting..." || s.startsWith("Connected as") ? "Disconnect" : "Connect";
}), 1000);

// ---- game files from Steam
$("sget").onclick = () => {
  $("smsg").textContent = "";
  invoke("assets_start", { user: $("suser").value, password: $("spass").value })
    .then(() => { $("spass").value = ""; }, (e) => { $("smsg").textContent = e; });
};
$("scancel").onclick = () => invoke("assets_cancel");
let need = null;
$("ssubmit").onclick = () => { invoke("assets_submit", { text: need === "Rosetta" ? "install" : $("sinput").value }); $("sinput").value = ""; };
let wasDone = false;
setInterval(() => invoke("assets_status").then((s) => {
  const active = s.phase === "Preparing" || s.phase === "LoggingIn" || s.phase === "Downloading";
  show("sget", !active); show("scancel", active);
  show("sbar", s.phase === "Downloading"); $("sbar").value = s.percent;
  need = s.need;
  show("sneed", !!s.need);
  show("sfield", s.need === "Guard" || s.need === "Password"); // Rosetta only needs a button
  $("ssubmit").textContent = s.need === "Rosetta" ? "Install Rosetta" : "Submit";
  if (s.need) $("sneedlabel").textContent = s.need === "Guard" ? "Steam Guard code" : "Steam password";
  $("smsg").textContent = s.phase === "Idle" ? "" : `${s.message}${s.phase === "Downloading" ? ` ${s.percent.toFixed(0)}%` : ""}`;
  if (s.log) $("slog").textContent = s.log;
  if (s.phase === "Done" && !wasDone) {
    invoke("get_config").then((c) => { $("dir").value = c.game_dir; });
  }
  wasDone = s.phase === "Done";
}), 1000);
