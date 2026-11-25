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

const share = { view: null, sig: "", buttons: new Set(), listeners: new Set(), seen: new Set(), asked: new Set(), hidden: new Set(), dock: null, live: null, conflict: false, busy: false };
const GAME_LABEL = { generals: "Zero Hour", ra2: "Red Alert 2" };
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
  // a map arrived: the lists of maps are out of date
  for (const i of v.incoming) {
    const k = `${i.from}/${i.id}`;
    if (i.state === "done" && !share.seen.has("done" + k)) { share.seen.add("done" + k); notifyMaps(); window.dispatchEvent(new Event("maps-changed")); }
    if (i.state === "prompt" && !share.seen.has(k)) {
      share.seen.add(k);
      if (share.live) share.live.textContent = `${i.from_name} wants to send you the map ${i.name}. Press Alt and M to answer.`;
      if (i.conflict && !share.asked.has(k)) { share.asked.add(k); openConflictDialog(i); }
    }
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

function dismissButton(incoming, who, id) {
  return h("button", { class: "btn small ghost shareclose", "aria-label": "Dismiss", title: "Dismiss", onclick: () => { share.hidden.add(`${incoming ? "i" : "o"}${who}/${id}`); renderDock(); shareCall("map_share_dismiss", { incoming, who, id }); } }, "×");
}

function incomingCard(i) {
  const who = i.from_name;
  const key = `i${i.from}/${i.id}`;
  const head = h("div", { class: "sharehead" }, h("span", { class: "gametag" }, gameLabel(i.game)), h("span", {}, h("strong", {}, who), i.state === "prompt" || i.state === "receiving" ? " wants to send you a map" : " sent a map"), i.master && i.state === "prompt" ? h("span", { class: "pill" }, "Room master") : null);
  const map = h("div", { class: "sharemap" }, h("span", { class: "mapname" }, i.name), h("span", { class: "muted small" }, [shareKB(i.size), i.players >= 2 ? `${i.players} players` : null].filter(Boolean).join(" · ")));
  let body;
  if (i.state === "prompt") {
    body = [
      i.conflict ? h("p", { class: "small sharewarn" }, "You already have a different map with this name.") : null,
      h("div", { class: "row sharebtns" },
        i.conflict
          ? h("button", { class: "btn primary small", onclick: () => openConflictDialog(i) }, "Choose what to do")
          : h("button", { class: "btn primary small", onclick: () => shareCall("map_share_reply", { from: i.from, id: i.id, reply: "accept" }) }, "Accept"),
        h("button", { class: "btn small", onclick: () => shareCall("map_share_reply", { from: i.from, id: i.id, reply: "decline" }) }, "Decline"),
        h("button", { class: "btn small ghost", title: `Decline, and ignore every map ${who} sends until you leave this room`, onclick: () => shareCall("map_share_reply", { from: i.from, id: i.id, reply: "block" }) }, `Block ${who}'s maps`)),
    ];
  } else if (i.state === "receiving") {
    body = [h("div", { class: "shareprog" }, h("progress", { max: 100, value: i.percent, "aria-label": `Receiving ${i.name}` }), h("span", { class: "pct small" }, `${i.percent}%`), h("button", { class: "btn small", onclick: () => shareCall("map_share_reply", { from: i.from, id: i.id, reply: "decline" }) }, "Cancel"))];
  } else if (i.state === "done") {
    body = [h("p", { class: "small sharedone" }, i.note), h("div", { class: "row sharebtns" }, h("button", { class: "btn small", onclick: () => { share.hidden.add(key); go("maps"); renderDock(); shareCall("map_share_dismiss", { incoming: true, who: i.from, id: i.id }); } }, "Show in Maps"))];
  } else {
    body = [h("p", { class: "small " + (i.state === "have" ? "muted" : "sharewarn") }, i.note || (i.state === "have" ? "You already have this map" : "Not saved"))];
  }
  const card = h("div", { class: "sharecard " + i.state, role: "group", "aria-label": `Map from ${who}: ${i.name}` }, head, map, body);
  if (i.state !== "prompt" && i.state !== "receiving") card.append(dismissButton(true, i.from, i.id));
  return card;
}

const OUT_PILL = { waiting: ["", "Waiting for an answer"], done: ["ok", "Delivered"], have: ["", "Already has it"], declined: ["warn", "Declined"], cancelled: ["", "Cancelled"], failed: ["bad", "Failed"] };
function outgoingRow(o) {
  const live = o.state === "waiting" || o.state === "sending";
  const status = o.state === "sending"
    ? h("span", { class: "shareprog" }, h("progress", { max: 100, value: o.percent, "aria-label": `Sending ${o.name} to ${o.to_name}` }), h("span", { class: "pct small" }, `${o.percent}%`))
    : h("span", { class: "pill " + OUT_PILL[o.state][0], title: o.note || undefined }, o.state === "failed" || o.state === "declined" || o.state === "have" ? (o.note || OUT_PILL[o.state][1]) : OUT_PILL[o.state][1]);
  const btn = live
    ? h("button", { class: "btn small", "aria-label": `Cancel sending ${o.name} to ${o.to_name}`, onclick: () => shareCall("map_share_cancel", { to: o.to, id: o.id }) }, "Cancel")
    : dismissButton(false, o.to, o.id);
  return h("div", { class: "shareout", role: "group", "aria-label": `${o.name} to ${o.to_name}` }, h("span", { class: "shareto" }, h("span", { class: "mapname" }, o.name), h("span", { class: "muted small" }, ` to ${o.to_name}`)), status, btn);
}

function renderDock() {
  if (!share.dock) return;
  const v = share.view;
  const inc = v ? v.incoming.filter((i) => !share.hidden.has(`i${i.from}/${i.id}`)) : [];
  const out = v ? v.outgoing.filter((o) => !share.hidden.has(`o${o.to}/${o.id}`)) : [];
  share.dock.hidden = !inc.length && !out.length;
  // keep the focused button focused across a redraw, unless its card is gone
  const had = document.activeElement && share.dock.contains(document.activeElement) ? document.activeElement.textContent + "|" + (document.activeElement.getAttribute("aria-label") || "") : null;
  fill(share.dock, inc.slice(0, 5).map(incomingCard), out.length ? h("div", { class: "sharesent" }, h("h3", {}, "Maps you are sending"), out.slice(0, 8).map(outgoingRow)) : null);
  if (had) { const b = [...share.dock.querySelectorAll("button")].find((x) => x.textContent + "|" + (x.getAttribute("aria-label") || "") === had); if (b) b.focus(); }
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
