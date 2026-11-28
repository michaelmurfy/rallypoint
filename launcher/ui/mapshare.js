"use strict";
// Sending maps to the other players in a relay room (see docs/send-map-threat-model.md). Loaded before app.js: it only
// defines things; app.js calls `mountShareDock()` once the shell exists and puts `shareButton()` on its map rows.
//
// Three pieces, all fed by the backend's `map_share_view` (polled while it matters):
//  - the dock: a small region at the bottom right with offers waiting for an answer, transfers in progress and results.
//    It never takes focus and never blocks; a new offer is announced to screen readers, and Alt+M jumps to it.
//  - the Send dialog: pick who gets the map.
//  - the conflict dialog: a map arrives with the name of one you have, so you decide: keep both, or replace yours.
// Everything another player wrote (their name, the map's name) goes in as text, never as markup.

const share = { view: null, sig: "", buttons: new Set(), listeners: new Set(), seen: new Set(), asked: new Set(), hidden: new Set(), dock: null, live: null, conflict: false, busy: false, outOpen: false, shown: new Set(), finishedAt: new Map(), clearTimer: 0 };
const GAME_LABEL = { generals: "Zero Hour", ra2: "Red Alert 2" };
function isTerminal(i) { return i.state !== "prompt" && i.state !== "receiving"; }
const gameLabel = (g) => GAME_LABEL[g] || "another game";
const shareKB = (bytes) => sizeText(Math.max(1, Math.ceil(bytes / 1024)));

/** Why the player cannot send right now, or "" when they can. */
function shareWhy() {
  const v = share.view;
  if (!v || !v.connected) return "Join a room on the Online tab to send maps";
  if (v.supported === false) return "This server does not support sending maps yet. Its owner needs to update it";
  if (v.supported == null) return "Checking whether this server can send maps";
  if (!v.peers.length) return "Nobody else is in the room yet";
  if (!v.peers.some((p) => p.can_receive)) return `Nobody else in the room has ${gameLabel(v.game)} selected`;
  return "";
}

/** A "Send" button for one map. `name` is the map's folder (Generals) or file (Red Alert 2), or a function that says it. */
function shareButton(name, label) {
  const b = h("button", { class: "btn small sharebtn", onclick: (e) => { e.preventDefault(); e.stopPropagation(); openSendDialog(typeof name === "function" ? name() : name, e.currentTarget); } }, label || "Send");
  b.dataset.map = typeof name === "string" ? name : "";
  share.buttons.add(b);
  paintShareButton(b);
  return b;
}
function paintShareButton(b) {
  const why = shareWhy();
  b.disabled = !!why;
  b.title = why || "Send this map to other players in your room";
  const n = typeof b.dataset.map === "string" ? b.dataset.map : "";
  b.setAttribute("aria-label", why ? `Send map (unavailable: ${why})` : n ? `Send ${n} to the room` : "Send this map to the room");
}
function paintShareButtons() {
  for (const b of share.buttons) { if (!b.isConnected) share.buttons.delete(b); else paintShareButton(b); }
}

async function pollShare() {
  if (share.busy) return;
  share.busy = true;
  try {
    // nothing to ask while offline, unless something is still on the screen
    const online = store.relay && store.relay.startsWith("Connected as");
    if (!online && !share.view) return;
    const v = await invoke("map_share_view");
    const next = v.connected ? v : null;
    const sig = JSON.stringify(next, (k, x) => (k === "seconds_left" ? undefined : x));
    if (sig === share.sig) return;
    share.sig = sig;
    share.view = next;
    afterShare();
  } catch { /* the backend is busy: try again next time */ }
  finally { share.busy = false; }
}

function afterShare() {
  const v = share.view;
  paintShareButtons();
  renderDock();
  share.listeners.forEach((f) => f());
  if (!v) return;
  // a map arrived: the lists of maps are out of date; say what is new once per refresh, not once per map
  const fresh = [], saved = [];
  for (const i of v.incoming) {
    const key = `${i.from}/${i.id}`;
    if (i.state === "done" && !share.seen.has("done" + key)) { share.seen.add("done" + key); saved.push(i); }
    if (i.state === "prompt" && !share.seen.has(key)) { share.seen.add(key); fresh.push(i); if (i.conflict && !share.asked.has(key)) { share.asked.add(key); openConflictDialog(i); } }
  }
  if (saved.length) { notifyMaps(); window.dispatchEvent(new Event("maps-changed")); }
  if (share.live && (fresh.length || saved.length)) {
    share.live.textContent = fresh.length
      ? (fresh.length === 1 ? `${fresh[0].from_name} wants to send you the map ${fresh[0].name}.` : `${plural(fresh.length, "map", "maps")} offered by other players.`) + " Press Alt and M to answer."
      : `${plural(saved.length, "map", "maps")} saved.`;
  }
}

const shareCall = async (cmd, args) => { try { await invoke(cmd, args); } catch (e) { say(errText(e), "bad"); } await pollShare(); };

function mountShareDock() {
  share.live = h("div", { class: "sr", role: "status", "aria-live": "polite" });
  share.dock = h("section", { id: "sharedock", class: "sharedock", "aria-label": "Maps from other players", tabindex: "-1", hidden: true });
  document.getElementById("root").append(share.dock, share.live);
  // Alt+M: go to the dock (offers do not take focus by themselves)
  document.addEventListener("keydown", (e) => {
    if (e.altKey && e.code === "KeyM" && !share.dock.hidden) { e.preventDefault(); const b = share.dock.querySelector("button"); (b || share.dock).focus(); }
  });
}

// ---- the dock: one panel, at most four slim parts (an offer, what is arriving, what arrived, what you are sending).
// Many maps never mean many cards: offers queue behind the oldest, transfers and results are summed up, and finished
// things go away by themselves a few seconds after the player last looked.
const DONE_SHOW_MS = 12000;

const plural = (n, one, many) => `${n} ${n === 1 ? one : many}`;
const k = (key, el) => { el.dataset.k = key; return el; };
const shareErr = (e) => say(errText(e), "bad");

/** Answer several offers or cancel several transfers with one refresh at the end. */
async function replyAll(items, reply) {
  for (const i of items) { try { await invoke("map_share_reply", { from: i.from, id: i.id, reply }); } catch (e) { shareErr(e); break; } }
  await pollShare();
}
async function cancelOut(items) {
  for (const o of items) { try { await invoke("map_share_cancel", { to: o.to, id: o.id }); } catch (e) { shareErr(e); break; } }
  await pollShare();
}
async function dismissMany(incoming, items) {
  for (const x of items) share.hidden.add(`${incoming ? "i" : "o"}${incoming ? x.from : x.to}/${x.id}`);
  renderDock();
  for (const x of items) { try { await invoke("map_share_dismiss", { incoming, who: incoming ? x.from : x.to, id: x.id }); } catch { /* it is hidden anyway */ } }
  pollShare();
}
const meanPercent = (items) => Math.round(items.reduce((s, x) => s + (x.state === "done" ? 100 : x.percent || 0), 0) / Math.max(1, items.length));

function offerPart(prompts) {
  const i = prompts[0], who = i.from_name, more = prompts.length - 1;
  const acceptable = prompts.filter((p) => !p.conflict);
  const meta = [shareKB(i.size), i.players >= 2 ? `${i.players} players` : null].filter(Boolean).join(" · ");
  return h("div", { class: "sharepart offer", role: "group", "aria-label": `Map offer from ${who}: ${i.name}` },
    h("div", { class: "sharehead" }, h("span", {}, h("strong", {}, who), i.master ? h("span", { class: "muted" }, " (room master)") : null, " wants to send you a map")),
    h("div", { class: "sharemap" }, h("span", { class: "mapname" }, i.name), h("span", { class: "sharemeta" }, h("span", { class: "gametag" }, gameLabel(i.game)), h("span", { class: "muted small" }, meta))),
    i.conflict ? h("p", { class: "small sharewarn" }, "You already have a different map with this name.") : null,
    h("div", { class: "row sharebtns" },
      i.conflict
        ? k("accept", h("button", { class: "btn primary small", onclick: () => openConflictDialog(i) }, "Choose what to do"))
        : k("accept", h("button", { class: "btn primary small", onclick: () => replyAll([i], "accept") }, "Accept")),
      k("decline", h("button", { class: "btn small", onclick: () => replyAll([i], "decline") }, "Decline")),
      k("block", h("button", { class: "btn small ghost", title: `Decline, and ignore every map ${who} sends until you leave this room`, onclick: () => replyAll([i], "block") }, `Block ${who}`))),
    more ? h("div", { class: "sharequeue" },
      h("span", { class: "muted small" }, `+${more} waiting`),
      h("span", { class: "row sharebtns" },
        acceptable.length > 1 ? k("accept-all", h("button", { class: "btn small ghost", onclick: () => replyAll(acceptable, "accept") }, "Accept all")) : null,
        k("decline-all", h("button", { class: "btn small ghost", onclick: () => replyAll(prompts, "decline") }, "Decline all")))) : null);
}

function progressPart(cls, text, pct, label, buttons) {
  return h("div", { class: "sharepart " + cls },
    h("div", { class: "sharetext" }, text),
    h("div", { class: "shareline" }, h("span", { class: "shareprog" }, h("progress", { max: 100, value: pct, "aria-label": label }), h("span", { class: "pct small" }, `${pct}%`)), buttons));
}

function receivingPart(items) {
  const pct = meanPercent(items);
  const from = new Set(items.map((i) => i.from_name));
  const text = items.length === 1 ? ["Receiving ", h("strong", {}, items[0].name)] : [`Receiving ${plural(items.length, "map", "maps")} from ${plural(from.size, "player", "players")}`];
  return progressPart("receiving", text, pct, "Receiving maps", k("cancel-in", h("button", { class: "btn small", "aria-label": "Cancel receiving", onclick: () => replyAll(items, "decline") }, "Cancel")));
}

function finishedPart(items) {
  const c = { done: 0, have: 0, failed: 0 };
  for (const i of items) c[i.state] = (c[i.state] || 0) + 1;
  const summary = [c.done && `${c.done} saved`, c.have && `${c.have} already had`, c.failed && `${c.failed} not saved`].filter(Boolean).join(", ");
  const names = items.slice(0, 2).map((i) => i.name).join(", ") + (items.length > 2 ? ` +${items.length - 2} more` : "");
  const bad = items.find((i) => i.state === "failed");
  return h("div", { class: "sharepart finished", role: "group", "aria-label": items.length === 1 ? `Finished: ${items[0].name}` : `${items.length} finished map transfers` },
    h("div", { class: "sharetext" }, h("strong", { class: c.failed ? "" : "sharedone" }, summary), h("span", { class: "muted small sharenames" }, names)),
    bad ? h("p", { class: "small sharewarn" }, `${bad.name}: ${bad.note || "not saved"}`) : null,
    h("div", { class: "row sharebtns" },
      c.done ? k("show", h("button", { class: "btn small", onclick: async () => { await dismissMany(true, items); go("maps"); } }, "Show in Maps")) : null,
      k("dismiss", h("button", { class: "btn small ghost", onclick: () => dismissMany(true, items) }, "Dismiss"))));
}

const OUT_PILL = { waiting: ["", "Waiting for an answer"], done: ["ok", "Delivered"], have: ["", "Already has it"], declined: ["warn", "Declined"], cancelled: ["", "Cancelled"], failed: ["bad", "Failed"] };
function outgoingRow(o) {
  const live = o.state === "waiting" || o.state === "sending";
  const status = o.state === "sending"
    ? h("span", { class: "shareprog" }, h("progress", { max: 100, value: o.percent, "aria-label": `Sending ${o.name} to ${o.to_name}` }), h("span", { class: "pct small" }, `${o.percent}%`))
    : h("span", { class: "pill " + OUT_PILL[o.state][0], title: o.note || undefined }, o.state === "failed" || o.state === "declined" || o.state === "have" ? (o.note || OUT_PILL[o.state][1]) : OUT_PILL[o.state][1]);
  const btn = live
    ? h("button", { class: "btn small", "aria-label": `Cancel sending ${o.name} to ${o.to_name}`, onclick: () => cancelOut([o]) }, "Cancel")
    : h("button", { class: "btn small ghost shareclose", "aria-label": `Dismiss ${o.name} to ${o.to_name}`, title: "Dismiss", onclick: () => dismissMany(false, [o]) }, "×");
  return h("div", { class: "shareout", role: "group", "aria-label": `${o.name} to ${o.to_name}` }, h("span", { class: "shareto" }, h("span", { class: "mapname" }, o.name), h("span", { class: "muted small" }, ` to ${o.to_name}`)), status, btn);
}

function sendingPart(out) {
  const live = out.filter((o) => o.state === "waiting" || o.state === "sending");
  const maps = new Set(out.map((o) => o.name)).size, players = new Set(out.map((o) => o.to)).size;
  const counted = out.filter((o) => o.state === "waiting" || o.state === "sending" || o.state === "done");
  const pct = meanPercent(counted);
  const what = `${plural(maps, "map", "maps")} to ${plural(players, "player", "players")}`;
  let head;
  if (live.length) head = h("div", { class: "sharetext" }, h("strong", {}, "Sending "), `${what}${live.every((o) => o.state === "waiting") ? ", waiting for answers" : ""}`);
  else {
    const c = {};
    for (const o of out) c[o.state] = (c[o.state] || 0) + 1;
    const parts = [c.done && `${c.done} delivered`, c.have && `${c.have} already had it`, c.declined && `${c.declined} declined`, c.failed && `${c.failed} failed`, c.cancelled && `${c.cancelled} cancelled`].filter(Boolean);
    head = h("div", { class: "sharetext" }, h("strong", { class: c.done && parts.length === 1 ? "sharedone" : "" }, `${what}: `), parts.join(", "));
  }
  const open = share.outOpen;
  const toggle = k("details", h("button", { class: "btn small ghost", "aria-expanded": String(open), "aria-controls": "sharedetails", onclick: () => { share.outOpen = !share.outOpen; renderDock(); } }, open ? "Hide details" : "Details"));
  const act = live.length
    ? k("cancel-out", h("button", { class: "btn small", onclick: () => cancelOut(live) }, `Cancel${live.length > 1 ? " all" : ""}`))
    : k("dismiss-out", h("button", { class: "btn small ghost", onclick: () => dismissMany(false, out) }, "Dismiss"));
  return h("div", { class: "sharepart sending", role: "group", "aria-label": "Maps you are sending" },
    head,
    h("div", { class: "shareline" }, live.length ? h("span", { class: "shareprog" }, h("progress", { max: 100, value: pct, "aria-label": "Sending maps" }), h("span", { class: "pct small" }, `${pct}%`)) : null, toggle, act),
    open ? h("div", { id: "sharedetails", class: "sharedetails" }, out.map(outgoingRow)) : null);
}

/** Leave finished things on screen for a while after the player last looked, then clear them. */
function scheduleClear(finishedKeys) {
  const now = Date.now();
  for (const key of finishedKeys) if (!share.finishedAt.has(key)) share.finishedAt.set(key, now);
  for (const key of [...share.finishedAt.keys()]) if (!finishedKeys.includes(key)) share.finishedAt.delete(key);
  clearTimeout(share.clearTimer);
  if (!share.finishedAt.size) return;
  const wait = Math.max(500, Math.min(...share.finishedAt.values()) + DONE_SHOW_MS - now);
  share.clearTimer = setTimeout(() => {
    if (share.dock.matches(":hover, :focus-within")) { for (const key of share.finishedAt.keys()) share.finishedAt.set(key, Date.now() - DONE_SHOW_MS + 4000); return scheduleClear([...share.finishedAt.keys()]); }
    const v = share.view; if (!v) return;
    const old = (key) => share.finishedAt.has(key) && Date.now() - share.finishedAt.get(key) >= DONE_SHOW_MS - 50;
    const inc = v.incoming.filter((i) => isTerminal(i) && old(`i${i.from}/${i.id}`));
    const out = v.outgoing.filter((o) => o.state !== "waiting" && o.state !== "sending" && old(`o${o.to}/${o.id}`));
    // an outgoing group goes only as a whole, so a result is never torn away from the rows beside it
    if (inc.length) dismissMany(true, v.incoming.filter(isTerminal));
    if (out.length && !v.outgoing.some((o) => o.state === "waiting" || o.state === "sending")) dismissMany(false, v.outgoing);
    scheduleClear([...share.finishedAt.keys()]);
  }, wait);
}

function renderDock() {
  if (!share.dock) return;
  const v = share.view;
  const inc = v ? v.incoming.filter((i) => !share.hidden.has(`i${i.from}/${i.id}`)) : [];
  const out = v ? v.outgoing.filter((o) => !share.hidden.has(`o${o.to}/${o.id}`)) : [];
  const prompts = inc.filter((i) => i.state === "prompt"), receiving = inc.filter((i) => i.state === "receiving"), done = inc.filter(isTerminal);
  const parts = [];
  if (prompts.length) parts.push([`offer:${prompts[0].from}/${prompts[0].id}`, offerPart(prompts)]);
  if (receiving.length) parts.push(["receiving", receivingPart(receiving)]);
  if (done.length) parts.push(["finished", finishedPart(done)]);
  if (out.length) parts.push(["sending", sendingPart(out)]);
  if (!out.length) share.outOpen = false;
  share.dock.hidden = !parts.length;
  const focus = document.activeElement && share.dock.contains(document.activeElement) ? document.activeElement.dataset.k || null : null;
  const wasInside = focus !== null || share.dock === document.activeElement;
  // a part that was not there before slides in; one that is only updating does not replay it
  const keys = new Set(parts.map((p) => p[0]));
  for (const [key, el] of parts) if (!share.shown.has(key)) el.classList.add("enter");
  share.shown = keys;
  fill(share.dock, ...parts.map((p) => p[1]));
  if (wasInside) { const b = focus && share.dock.querySelector(`[data-k="${focus}"]`); (b || share.dock).focus(); }
  scheduleClear([...done.map((i) => `i${i.from}/${i.id}`), ...(out.length && !out.some((o) => o.state === "waiting" || o.state === "sending") ? out.map((o) => `o${o.to}/${o.id}`) : [])]);
}


// ---------------------------------------------------------------- modal plumbing

/** A modal: Escape and the Cancel button close it, Tab stays inside it, and focus returns to what opened it. */
function shareModal(label, body, foot, from, { alert = false } = {}) {
  const overlay = h("div", { class: "overlay", role: alert ? "alertdialog" : "dialog", "aria-modal": "true", "aria-label": label },
    h("div", { class: "wizard sharedialog" }, h("div", { class: "wiz-head" }, h("h2", {}, label)), h("div", { class: "wiz-body" }, body), h("div", { class: "wiz-foot" }, foot)));
  // Capture phase, so a page or dialog underneath (the map viewer closes on Escape and moves on arrow keys) does not also react
  const onKey = (e) => {
    if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); close(); return; }
    if (overlay.contains(e.target)) e.stopPropagation();
    if (e.key !== "Tab") return;
    const f = [...overlay.querySelectorAll("button:not(:disabled), input:not(:disabled)")];
    if (!f.length) return;
    const [first, last] = [f[0], f[f.length - 1]];
    if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); }
    else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
  };
  let closed = false, onClose = null;
  function close() {
    if (closed) return;
    closed = true;
    document.removeEventListener("keydown", onKey, true);
    overlay.remove();
    if (onClose) onClose();
    if (from && from.isConnected) from.focus();
  }
  document.addEventListener("keydown", onKey, true);
  document.getElementById("root").append(overlay);
  if (!alert) backdropCloses(overlay, close); // a question that needs an answer stays until it gets one
  return { overlay, close, onClose: (f) => { onClose = f; } };
}

// ---------------------------------------------------------------- Send to room

function openSendDialog(name, from) {
  const fb = liveStatus();
  const picked = new Set();
  let touched = false;
  const list = h("div", { class: "peerlist", role: "group", "aria-label": "Players in the room" });
  const intro = h("p", { class: "muted small" });
  const send = h("button", { class: "btn primary", onclick: async () => {
    send.disabled = true; fb.busy("Sending the offer…");
    try {
      const n = await invoke("map_share_send", { name, to: [...picked] });
      modal.close();
      toast(`Offer sent to ${n} player${n === 1 ? "" : "s"}. They choose whether to take it.`, "ok");
      pollShare();
    } catch (e) { fb.bad(errText(e)); send.disabled = false; }
  } }, "Send");
  const cancel = h("button", { class: "btn", onclick: () => modal.close() }, "Cancel");
  const modal = shareModal(`Send “${name}” to the room`, [intro, list, h("p", { class: "muted small" }, "Nothing is sent until a player says yes. They can also say no, or block you."), fb.el], [cancel, send], from);
  function draw() {
    const v = share.view, why = shareWhy();
    const eligible = v ? v.peers.filter((p) => p.can_receive) : [];
    if (!touched) eligible.forEach((p) => picked.add(p.vip));
    for (const vip of [...picked]) if (!eligible.some((p) => p.vip === vip)) picked.delete(vip);
    intro.textContent = v && v.connected ? `${gameLabel(v.game)} map. Choose who should get it.` : "";
    if (why && !(v && v.peers.length)) {
      fill(list, h("div", { class: "callout", role: "status" }, h("span", {}, why + "."), !v || !v.connected ? h("button", { class: "btn small", onclick: () => { modal.close(); go("online"); } }, "Open Online") : null));
    } else {
      const all = h("input", { type: "checkbox", "aria-label": "Everyone who can take it", checked: eligible.length > 0 && eligible.every((p) => picked.has(p.vip)), disabled: !eligible.length, onchange: () => { touched = true; if (all.checked) eligible.forEach((p) => picked.add(p.vip)); else picked.clear(); draw(); } });
      fill(list, h("label", { class: "peerrow all" }, all, h("span", { class: "peername" }, "Everyone")),
        ...v.peers.map((p) => {
          const box = h("input", { type: "checkbox", checked: picked.has(p.vip), disabled: !p.can_receive, onchange: () => { touched = true; if (box.checked) picked.add(p.vip); else picked.delete(p.vip); draw(); } });
          return h("label", { class: "peerrow" + (p.can_receive ? "" : " off") }, box, h("span", { class: "peername" }, p.name), p.master ? h("span", { class: "pill" }, "Room master") : null, h("span", { class: "gametag" }, gameLabel(p.game)), p.can_receive ? null : h("span", { class: "muted small" }, `Has ${gameLabel(p.game)} selected`));
        }));
    }
    send.disabled = !picked.size;
    send.textContent = picked.size > 1 ? `Send to ${picked.size} players` : picked.size === 1 ? "Send to 1 player" : "Send";
  }
  share.listeners.add(draw);
  modal.onClose(() => share.listeners.delete(draw));
  draw();
  pollShare();
  (list.querySelector("input:not(:disabled)") || cancel).focus();
}

// ---------------------------------------------------------------- a map with the name of one you have

function openConflictDialog(i) {
  if (share.conflict) return;
  share.conflict = true;
  const reply = (r) => { modal.close(); shareCall("map_share_reply", { from: i.from, id: i.id, reply: r }); };
  const keep = h("button", { class: "btn primary", onclick: () => reply("accept") }, "Keep both");
  const modal = shareModal(`You already have “${i.name}”`, [
    h("p", {}, `${i.from_name} sent a different map with the same name. What should happen?`),
    h("ul", { class: "conflictlist" },
      h("li", {}, h("b", {}, "Keep both: "), `theirs is saved as “${i.name} (2)”, yours stays as it is.`),
      h("li", {}, h("b", {}, "Replace mine: "), "theirs takes its place. Your map is moved to a backup folder, not deleted.")),
    h("p", { class: "muted small" }, `${shareKB(i.size)}${i.players >= 2 ? `, ${i.players} players` : ""}, ${gameLabel(i.game)}`)],
    [h("button", { class: "btn ghost", onclick: () => modal.close() }, "Decide later"), h("button", { class: "btn", onclick: () => reply("decline") }, "Decline"), h("button", { class: "btn danger", onclick: () => reply("replace") }, "Replace mine"), keep], null, { alert: true });
  modal.onClose(() => { share.conflict = false; });
  keep.focus();
}

// ---------------------------------------------------------------- the Online page's room setting

/** A switch for "take maps from the room master without asking" (off until the player turns it on). */
function shareAutoAccept() {
  const box = h("input", { type: "checkbox", role: "switch", onchange: async () => {
    try { await invoke("map_share_auto_accept", { on: box.checked }); } catch (e) { say(errText(e), "bad"); box.checked = !box.checked; }
    pollShare();
  } });
  const row = h("label", { class: "switch" }, box, "Accept maps from the room master without asking");
  const sync = () => { box.checked = !!(share.view && share.view.auto_accept); };
  share.listeners.add(sync);
  sync();
  return { el: row, dispose: () => share.listeners.delete(sync) };
}
