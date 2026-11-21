// engine/mp/mp_lobby.cpp -- see mp_lobby.hpp.
#include "mp/mp_lobby.hpp"

#include <algorithm>

namespace ra2mp {

namespace {
constexpr std::uint64_t kPingMs = 2000;
constexpr std::uint64_t kSilenceMs = 15000;
constexpr std::uint64_t kQueryMs = 1000;
constexpr int kMaxQueries = 15;
constexpr std::uint64_t kGoWaitMs = 3000;
constexpr std::size_t kChatLines = 50;
// W9-04-07: the longest AWAY a peer may announce (the lobby uses 90 s, the
// match's 05 §7.2 value; the match caps its own at 300 s too).
constexpr std::uint16_t kMaxAwaySeconds = 300;

std::uint32_t body_cmd(const std::vector<std::uint8_t>& b) {
    return b.size() >= 4 ? (std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) |
                            (std::uint32_t(b[3]) << 24))
                         : 0u;
}

std::vector<std::uint8_t> simple(std::uint32_t cmd) {
    BodyWriter w;
    w.u32(cmd);
    return w.b;
}
}  // namespace

// ------------------------------------------------------------------ shared --

const char* reject_text(RejectReason r) {
    switch (r) {
        case RejectReason::Version:
            return "Different game version: you and the host run different builds of the game. "
                   "Both players need the same version.";
        case RejectReason::Rules:
            return "Different game data: your rulesmd.ini / artmd.ini / aimd.ini differ from the host's "
                   "(a mod or other game files). Both players need the same game data.";
        case RejectReason::Full: return "The game is full.";
        case RejectReason::Started: return "The game has already started.";
        case RejectReason::Kicked: return "You were removed from the game by the host.";
        case RejectReason::HostLeft: return "The host closed the game.";
        case RejectReason::Lost: return "Connection to the host was lost.";
        case RejectReason::Platform:
            return "The host plays on another platform (Windows / Android). Mixed-platform games need the "
                   "host's \"Cross-platform (experimental)\" option.";
        default: return "";
    }
}

std::string reject_message(RejectReason r, const std::string& mine, const std::string& host) {
    // W9-05-02: name both builds when the host sent its version string.
    if (r == RejectReason::Version && !host.empty())
        return "Different game version: yours is " + (mine.empty() ? std::string("?") : mine) + ", the host's is " +
               host + ". Both players need the same version.";
    return reject_text(r);
}

std::vector<std::vector<std::uint8_t>> options_bodies(std::uint32_t rev, const std::string& opts) {
    std::vector<std::vector<std::uint8_t>> out;
    const std::size_t parts = std::max<std::size_t>(1, (opts.size() + kOptionsChunk - 1) / kOptionsChunk);
    for (std::size_t i = 0; i < parts; ++i) {
        const std::string chunk = opts.substr(i * kOptionsChunk, kOptionsChunk);
        BodyWriter w;
        w.u32(kCmdGameOptions);
        w.u32(rev);
        w.u8(static_cast<std::uint8_t>(i));
        w.u8(static_cast<std::uint8_t>(parts));
        w.u16(static_cast<std::uint16_t>(chunk.size()));
        for (char c : chunk) w.u8(static_cast<std::uint8_t>(c));
        out.push_back(w.b);
    }
    return out;
}

void LobbyBase::add_chat(const ChatLine& l) {
    chat_.push_back(l);
    while (chat_.size() > kChatLines) chat_.pop_front();
}

void LobbyBase::system_line(const std::string& text) { add_chat(ChatLine{"", text, -1}); }

void LobbyBase::build_session(bool is_host, const std::vector<std::pair<int, PeerId>>& slot_peer) {
    session_ = MpSessionConfig{};
    session_.lobby = model_;
    session_.options = encode_options(model_);
    session_.seed = model_.seed;
    session_.is_host = is_host;
    session_.local_slot = local_slot_;
    session_.slot_peers = slot_peer;
    resolve_session(session_, 9);   // the UI re-resolves with the real country list
    started_ = true;
}

// -------------------------------------------------------------------- host --

LobbyHost::LobbyHost(MpNet& t, const MpIdentity& me, const MpLobbyModel& initial)
    : ch_(t), me_(me) {
    model_ = initial;
    MpSlot& h = model_.slots[0];
    h.kind = SlotKind::Human;
    h.name = sanitize_name(me.nick);
    h.host = true;
    h.has_map = true;
    h.ready = true;
    h.pid_lo = static_cast<std::uint32_t>(me.player_id);
    local_slot_ = 0;
    rev_ = 1;
}

PeerId LobbyHost::peer_of_slot(int slot) const {
    for (const auto& kv : peers_)
        if (kv.second.slot == slot) return kv.first;
    return 0;
}

void LobbyHost::changed(bool clear_ready) {
    if (clear_ready)
        for (MpSlot& s : model_.slots)
            if (s.kind == SlotKind::Human && !s.host) s.ready = false;
    ++rev_;
    dirty_ = true;
}

void LobbyHost::set_slot_kind(int slot, SlotKind kind, int ai) {
    if (slot <= 0 || slot >= kSlots || going_) return;
    MpSlot& s = model_.slots[slot];
    if (s.kind == SlotKind::Human) {
        if (kind == SlotKind::Human) return;
        kick(slot);   // replacing a human = removing them
    }
    if (kind == SlotKind::Human) return;   // humans only arrive by joining
    const SlotKind was = s.kind;
    s = MpSlot{};
    s.kind = kind;
    s.ai = std::max(0, std::min(2, ai));
    if (kind == SlotKind::AI && was == SlotKind::AI) {
        // keep nothing else: an AI level change resets the row like the skirmish page
    }
    changed(true);
}

void LobbyHost::set_slot(int slot, int country, int color, int team, int start) {
    if (slot < 0 || slot >= kSlots || going_) return;
    MpSlot& s = model_.slots[slot];
    if (!s.occupied()) return;
    if (color >= 0 && color_owner(model_, color, slot) >= 0) color = s.color;
    if (start >= 0 && start_owner(model_, start, slot) >= 0) start = s.start;
    s.country = country < 0 ? kRandom : country;
    s.color = color < 0 ? kRandom : std::min(color, 7);
    s.team = team < 0 ? kNoTeam : std::min(team, 3);
    s.start = start < 0 ? kRandomStart : std::min(start, std::max(0, model_.map.max_players - 1));
    changed(slot != 0);
}

void LobbyHost::set_options(const MpOptions& o) {
    if (going_) return;
    model_.opt = o;
    changed(true);
}

bool LobbyHost::set_map(const MpMap& m) {
    if (going_) return false;
    const int maxp = std::max(1, std::min(kSlots, m.max_players));
    // W9-03-13: the original refuses a map smaller than the players already
    // seated (TXT_SCENARIO_TOO_SMALL "This map has a %d player max. The max
    // includes human and computer players.") -- nobody is ever removed.
    if (player_count(model_) > maxp) return false;
    const int old_max = std::max(1, std::min(kSlots, model_.map.max_players));
    model_.map = m;
    // Seated players past the new max move down into a free row (Open first,
    // then a host-Closed one); a moved human finds its row again by its
    // player id (LobbyClient re-binds local_slot from pid_lo).
    for (int i = maxp; i < kSlots; ++i) {
        if (!model_.slots[i].occupied()) continue;
        int to = -1;
        for (int j = 1; j < maxp && to < 0; ++j)
            if (model_.slots[j].kind == SlotKind::Open) to = j;
        for (int j = 1; j < maxp && to < 0; ++j)
            if (model_.slots[j].kind == SlotKind::Closed) to = j;
        if (to < 0) break;   // unreachable: player_count <= maxp
        model_.slots[to] = model_.slots[i];
        for (auto& kv : peers_)
            if (kv.second.slot == i) kv.second.slot = to;
        model_.slots[i] = MpSlot{};
    }
    for (int i = maxp; i < kSlots; ++i) {
        model_.slots[i] = MpSlot{};
        model_.slots[i].kind = SlotKind::Closed;
    }
    // Rows the previous (smaller) map had closed open up; rows the host closed
    // himself stay closed.
    for (int i = std::max(1, old_max); i < maxp; ++i)
        if (model_.slots[i].kind == SlotKind::Closed) model_.slots[i].kind = SlotKind::Open;
    for (MpSlot& s : model_.slots) {
        if (s.start >= maxp) s.start = kRandomStart;
        if (s.kind == SlotKind::Human && !s.host) s.has_map = false;   // until the client re-checks
    }
    changed(true);
    return true;
}

void LobbyHost::set_mode(int mode, bool ai_allowed, bool teams_allowed) {
    if (going_) return;
    model_.mode = mode;
    for (int i = 1; i < kSlots; ++i) {
        MpSlot& s = model_.slots[i];
        if (!ai_allowed && s.kind == SlotKind::AI) {   // ManBattle / Unholy / Cooperative: humans only
            s = MpSlot{};
            s.kind = SlotKind::Open;
        }
    }
    if (!teams_allowed)   // FreeForAll: AlliesAllowed=no
        for (MpSlot& s : model_.slots) s.team = kNoTeam;
    changed(true);
}

void LobbyHost::kick(int slot) {
    if (slot <= 0 || slot >= kSlots) return;
    const PeerId p = peer_of_slot(slot);
    if (p) {
        BodyWriter w;
        w.u32(kCmdRejectJoin);
        w.u8(static_cast<std::uint8_t>(RejectReason::Kicked));
        ch_.send(p, w.b, true, now_);
        system_line(model_.slots[slot].name + " was removed from the game.");
        peers_.erase(p);   // the REJECT keeps resending until ACKed
    }
    model_.slots[slot] = MpSlot{};
    changed(false);
}

void LobbyHost::chat(const std::string& text) {
    if (text.empty()) return;
    ChatPacket c;
    c.sender = model_.slots[0].name;
    c.text = text;
    c.color = model_.slots[0].color;
    c.game_hash = game_name_hash(room_);
    add_chat(ChatLine{c.sender, text, c.color});
    const std::vector<std::uint8_t> body = encode_chat(c);
    for (const auto& kv : peers_) ch_.send(kv.first, body, true, now_);
}

StartCheck LobbyHost::start(std::uint32_t seed) {
    const StartCheck c = can_start(model_);
    if (c != StartCheck::Ok || going_) return c;
    model_.seed = seed;
    ++rev_;
    broadcast_options(now_);
    dirty_ = false;
    // NET-PLAY: GO carries the peer table (slot -> the peer as the host sees
    // it), so every client can open a private channel to every other human
    // (the match is a full mesh like the original's; the lobby is a star).
    BodyWriter w;
    w.u32(kCmdGo);
    w.u32(rev_);
    w.u32(seed);
    w.u8(static_cast<std::uint8_t>(peers_.size()));
    for (const auto& kv : peers_) {
        w.u8(static_cast<std::uint8_t>(kv.second.slot));
        w.u64(kv.first);
    }
    for (const auto& kv : peers_) ch_.send(kv.first, w.b, true, now_);
    going_ = true;
    go_at_ = now_;
    return c;
}

void LobbyHost::close() {
    for (const auto& kv : peers_) {
        ch_.send(kv.first, simple(kCmdSignOff), false, now_);
        ch_.send(kv.first, simple(kCmdSignOff), false, now_);
    }
    peers_.clear();
}

void LobbyHost::send_options(PeerId to, std::uint64_t now) {
    for (const auto& b : options_bodies(rev_, encode_options(model_))) ch_.send(to, b, true, now);
}

void LobbyHost::broadcast_options(std::uint64_t now) {
    const std::string opts = encode_options(model_);
    const auto bodies = options_bodies(rev_, opts);
    for (const auto& kv : peers_)
        for (const auto& b : bodies) ch_.send(kv.first, b, true, now);
}

void LobbyHost::drop_peer(PeerId p, const char* why) {
    const auto it = peers_.find(p);
    if (it == peers_.end()) return;
    const int slot = it->second.slot;
    peers_.erase(it);
    ch_.forget(p);
    if (slot > 0 && slot < kSlots && model_.slots[slot].kind == SlotKind::Human) {
        system_line(model_.slots[slot].name + why);
        if (on_event) on_event(model_.slots[slot].name + why);
        model_.slots[slot] = MpSlot{};
        changed(false);
    }
}

void LobbyHost::handle(PeerId from, const std::vector<std::uint8_t>& body, std::uint64_t now) {
    const std::uint32_t cmd = body_cmd(body);
    auto pit = peers_.find(from);
    if (pit != peers_.end()) pit->second.heard = now;
    BodyReader r(body);
    r.u32();
    switch (cmd) {
        case kCmdQueryJoin: {
            const std::uint16_t proto = r.u16();
            const std::uint32_t sim = r.u32();
            const std::uint32_t rules = r.u32();
            const std::uint64_t pid = r.u64();
            const std::string nick = sanitize_name(r.fixed(20));
            const std::string their_version = sanitize_name(r.fixed(24), 24);   // engine version (shown on a mismatch)
            const std::uint8_t platform = r.u8();
            if (pit != peers_.end()) {   // a repeat (our CONFIRM was lost / slow)
                BodyWriter w;
                w.u32(kCmdConfirmJoin);
                w.u8(static_cast<std::uint8_t>(pit->second.slot));
                ch_.send(from, w.b, true, now);
                send_options(from, now);
                return;
            }
            RejectReason why = RejectReason::None;
            int slot = -1;
            if (proto != me_.net_proto || sim != me_.sim_ver) why = RejectReason::Version;
            else if (rules != me_.rules_crc) why = RejectReason::Rules;
            else if (platform != me_.platform && !model_.opt.cross_platform) why = RejectReason::Platform;
            else if (going_ || started_) why = RejectReason::Started;
            else if ((slot = first_open_slot(model_)) < 0) why = RejectReason::Full;
            if (why == RejectReason::Platform &&
                std::find(told_platform_.begin(), told_platform_.end(), pid) == told_platform_.end()) {
                // W9-04-13: the host sees the refusal too (the joiner gets the
                // RejectJoin dialog); once per player.
                told_platform_.push_back(pid);
                const char* pn = platform_name(platform);
                const std::string who = (nick.empty() ? std::string("Player") : nick) +
                                        (*pn ? " (" + std::string(pn) + ")" : std::string());
                system_line(who + " tried to join: turn on \"Cross-platform (exp.)\" to admit other platforms.");
                if (on_event) on_event(who + " refused: other platform");
            }
            if (why != RejectReason::None) {
                BodyWriter w;
                w.u32(kCmdRejectJoin);
                w.u8(static_cast<std::uint8_t>(why));
                w.fixed(me_.version, 24);   // W9-05-02: the joiner names both builds (older clients ignore it)
                ch_.send(from, w.b, true, now);
                // W9-05-02: the host sees it too (once per joiner and reason;
                // the joiner repeats QUERY_JOIN until the REJECT arrives).
                if ((why == RejectReason::Version || why == RejectReason::Rules) &&
                    told_rejects_.insert(std::make_pair(pid, static_cast<int>(why))).second) {
                    const std::string line =
                        (nick.empty() ? std::string("A player") : nick) + " could not join: " +
                        (why == RejectReason::Version
                             ? "different game version (theirs: " +
                                   (their_version.empty() ? std::string("?") : their_version) + ")."
                             : std::string("different game data (rulesmd / artmd / aimd.ini)."));
                    system_line(line);
                    if (on_event) on_event(line);
                }
                return;
            }
            MpSlot& s = model_.slots[slot];
            s = MpSlot{};
            s.kind = SlotKind::Human;
            s.name = nick.empty() ? "Player" : nick;
            s.pid_lo = static_cast<std::uint32_t>(pid);
            s.has_map = false;
            peers_[from] = Peer{slot, now, now};
            BodyWriter w;
            w.u32(kCmdConfirmJoin);
            w.u8(static_cast<std::uint8_t>(slot));
            ch_.send(from, w.b, true, now);
            system_line(s.name + " joined the game.");
            if (on_event) on_event(s.name + " joined from " + ch_.net().describe(from));
            changed(false);
            return;
        }
        case kCmdSlotRequest: {
            if (pit == peers_.end() || going_) return;
            const int country = r.i32(), color = r.i32(), team = r.i32(), start = r.i32();
            const int slot = pit->second.slot;
            MpSlot& s = model_.slots[slot];
            if (color >= 0 && color_owner(model_, color, slot) < 0) s.color = std::min(color, 7);
            else if (color < 0) s.color = kRandom;
            else if (color != s.color) {
                // W9-03-24: a lost colour race is answered like the original
                // (TXT_COLOR_IN_USE, FUN_005E35A0) -- a system line to the asker.
                ChatPacket c;
                c.text = "The requested color is already taken.";
                c.color = -1;
                c.game_hash = game_name_hash(room_);
                ch_.send(from, encode_chat(c), true, now);
            }
            if (start >= 0 && start < model_.map.max_players && start_owner(model_, start, slot) < 0) s.start = start;
            else if (start < 0) s.start = kRandomStart;
            s.country = country < 0 ? kRandom : country;
            s.team = team < 0 ? kNoTeam : std::min(team, 3);
            s.ready = false;
            changed(false);   // rebroadcast even when the request was refused (the client resyncs)
            return;
        }
        case kCmdReadyToGo: {
            if (pit == peers_.end() || going_) return;
            MpSlot& s = model_.slots[pit->second.slot];
            const bool ready = r.u8() != 0;
            const bool has_map = r.u8() != 0;
            if (s.ready != ready || s.has_map != has_map) {
                s.ready = ready && has_map;
                s.has_map = has_map;
                changed(false);
            }
            return;
        }
        case kCmdMessage: {
            if (pit == peers_.end()) return;
            ChatPacket c;
            if (!decode_chat(body, c) || c.text.empty()) return;
            const MpSlot& s = model_.slots[pit->second.slot];
            c.sender = s.name;   // stamped from the peer, never trusted from the body
            c.color = s.color;
            c.game_hash = game_name_hash(room_);
            add_chat(ChatLine{c.sender, c.text, c.color});
            const std::vector<std::uint8_t> out = encode_chat(c);
            for (const auto& kv : peers_)
                if (kv.first != from) ch_.send(kv.first, out, true, now);
            return;
        }
        case kCmdSignOff:
            drop_peer(from, " left the game.");
            return;
        case kCmdAway: {
            // W9-04-07: a client's app went to the background / came back.
            if (pit == peers_.end()) return;
            const bool back = r.u8() != 0;
            const std::uint16_t secs = std::min<std::uint16_t>(r.u16(), kMaxAwaySeconds);
            const std::string& who = model_.slots[pit->second.slot].name;
            if (back) {
                if (pit->second.away_until == 0) return;   // the repeat
                pit->second.away_until = 0;
                system_line(who + " is back.");
                send_options(from, now);   // anything it missed while frozen
            } else {
                if (pit->second.away_until != 0) return;
                pit->second.away_until = now + 1000ull * secs;
                system_line(who + " is away (" + std::to_string(secs) + " s).");
            }
            if (on_event) on_event(who + (back ? " back" : " away"));
            return;
        }
        case kCmdPing:
        default:
            return;
    }
}

void LobbyHost::away(bool back, std::uint16_t seconds) {
    BodyWriter w;
    w.u32(kCmdAway);
    w.u8(back ? 1 : 0);
    w.u16(seconds);
    // Unreliable, twice (like SIGN_OFF): a frozen process cannot resend.
    for (const auto& kv : peers_) {
        ch_.send(kv.first, w.b, false, now_);
        ch_.send(kv.first, w.b, false, now_);
    }
    ch_.flush();
}

void LobbyHost::resume(std::uint64_t now) {
    now_ = now;
    for (auto& kv : peers_) {
        kv.second.heard = std::max(kv.second.heard, now);
        kv.second.pinged = 0;
        ch_.forget(kv.first);
    }
    dirty_ = true;   // the options again (anything un-ACKed was dropped)
}

void LobbyHost::update(std::uint64_t now) {
    now_ = now;
    std::vector<LobbyChannel::In> in;
    std::vector<PeerId> lost;
    ch_.poll(now, in, lost);
    for (const auto& m : in) handle(m.from, m.body, now);
    for (PeerId p : lost) {
        // W9-04-07: an away peer's un-ACKed packets are dropped, not the peer.
        const auto it = peers_.find(p);
        if (it != peers_.end() && now < it->second.away_until) ch_.forget(p);
        else drop_peer(p, " lost connection.");
    }
    std::vector<PeerId> silent;
    for (auto& kv : peers_) {
        if (now - std::max(kv.second.heard, kv.second.away_until) > kSilenceMs && now >= kv.second.away_until)
            silent.push_back(kv.first);
        else if (now < kv.second.away_until) continue;   // frozen: no pings to queue up
        else if (now - kv.second.pinged >= kPingMs) {
            ch_.send(kv.first, simple(kCmdPing), false, now);
            kv.second.pinged = now;
        }
    }
    for (PeerId p : silent) drop_peer(p, " timed out.");
    if (dirty_ && !going_) {
        broadcast_options(now);
        dirty_ = false;
    }
    if (going_ && !started_) {
        bool all = true;
        for (const auto& kv : peers_) all = all && ch_.idle_to(kv.first);
        if (all || now - go_at_ >= kGoWaitMs) {
            std::vector<std::pair<int, PeerId>> sp;
            for (const auto& kv : peers_) sp.emplace_back(kv.second.slot, kv.first);
            build_session(true, sp);
        }
    }
}

// ------------------------------------------------------------------ client --

LobbyClient::LobbyClient(MpNet& t, PeerId host, const MpIdentity& me) : ch_(t), host_(host), me_(me) {}

// W9-03-13: the host may move a seated human to another row (a smaller map
// compacts the rows); the row that carries our player id is ours.
void LobbyClient::rebind_slot() {
    if (local_slot_ < 0) return;
    const std::uint32_t lo = static_cast<std::uint32_t>(me_.player_id);
    const MpSlot& cur = model_.slots[local_slot_];
    if (cur.kind == SlotKind::Human && cur.pid_lo == lo) return;
    for (int i = 0; i < kSlots; ++i)
        if (model_.slots[i].kind == SlotKind::Human && !model_.slots[i].host && model_.slots[i].pid_lo == lo) {
            local_slot_ = i;
            return;
        }
}

void LobbyClient::request_slot(int country, int color, int team, int start) {
    if (state_ != State::Joined) return;
    BodyWriter w;
    w.u32(kCmdSlotRequest);
    w.i32(country);
    w.i32(color);
    w.i32(team);
    w.i32(start);
    ch_.send(host_, w.b, true, now_);
    ready_ = false;
}

void LobbyClient::send_ready(std::uint64_t now) {
    BodyWriter w;
    w.u32(kCmdReadyToGo);
    w.u8(ready_ ? 1 : 0);
    w.u8(has_map_ ? 1 : 0);
    ch_.send(host_, w.b, true, now);
}

void LobbyClient::set_ready(bool ready) {
    if (state_ != State::Joined) return;
    ready_ = ready && has_map_;
    send_ready(now_);
}

void LobbyClient::chat(const std::string& text) {
    if (state_ != State::Joined || text.empty()) return;
    ChatPacket c;
    c.sender = sanitize_name(me_.nick);
    c.text = text;
    c.color = local_slot_ >= 0 ? model_.slots[local_slot_].color : -1;
    c.game_hash = game_name_hash(room_);
    add_chat(ChatLine{c.sender, text, c.color});
    ch_.send(host_, encode_chat(c), true, now_);
}

void LobbyClient::leave() {
    if (state_ == State::Joined || state_ == State::Joining) {
        ch_.send(host_, simple(kCmdSignOff), false, now_);
        ch_.send(host_, simple(kCmdSignOff), false, now_);
    }
    state_ = State::Rejected;
    reason_ = RejectReason::None;
}

void LobbyClient::away(bool back, std::uint16_t seconds) {
    if (state_ != State::Joined) return;
    BodyWriter w;
    w.u32(kCmdAway);
    w.u8(back ? 1 : 0);
    w.u16(seconds);
    ch_.send(host_, w.b, false, now_);
    ch_.send(host_, w.b, false, now_);
    ch_.flush();
}

void LobbyClient::resume(std::uint64_t now) {
    now_ = now;
    heard_ = std::max(heard_, now);
    pinged_ = 0;
    ch_.forget(host_);
    if (state_ == State::Joined) send_ready(now);   // the host may have missed a change
}

void LobbyClient::handle(const std::vector<std::uint8_t>& body, std::uint64_t now) {
    const std::uint32_t cmd = body_cmd(body);
    BodyReader r(body);
    r.u32();
    switch (cmd) {
        case kCmdConfirmJoin:
            // Joined only once the first GAME_OPTIONS arrived too (the UI
            // must never show a confirmed seat with an empty lobby).
            if (state_ == State::Joining) {
                local_slot_ = r.u8();
                // NET-PLAY fix: the CONFIRM can arrive AFTER the options (it
                // was lost and resent; the global channel does not order
                // packets) -- then the options in hand complete the join.
                if (rev_ > 0 && local_slot_ >= 0 && local_slot_ < kSlots &&
                    model_.slots[local_slot_].kind == SlotKind::Human) {
                    state_ = State::Joined;
                    system_line("Joined the game.");
                    has_map_ = have_map ? have_map(model_.map) : true;
                    ready_ = model_.slots[local_slot_].ready;
                    if (model_.slots[local_slot_].has_map != has_map_) send_ready(now);
                }
            }
            return;
        case kCmdRejectJoin: {
            reason_ = static_cast<RejectReason>(r.u8());
            const std::string host_version = sanitize_name(r.fixed(24), 24);   // W9-05-02 (absent: "")
            reject_message_ = ra2mp::reject_message(reason_, me_.version, r.bad ? std::string() : host_version);
            state_ = State::Rejected;
            system_line(reject_message_);
            return;
        }
        case kCmdGameOptions: {
            const std::uint32_t rev = r.u32();
            const std::uint8_t part = r.u8(), nparts = r.u8();
            const std::uint16_t len = r.u16();
            if (r.bad || nparts == 0 || part >= nparts || len > kOptionsChunk || rev < rev_) return;
            if (rev != asm_rev_ || parts_.size() != nparts) {
                asm_rev_ = rev;
                parts_.assign(nparts, std::string());
                got_.assign(nparts, false);
            }
            std::string chunk;
            for (std::uint16_t i = 0; i < len; ++i) chunk += static_cast<char>(r.u8());
            parts_[part] = chunk;
            got_[part] = true;
            for (bool g : got_)
                if (!g) return;   // still missing a part
            std::string all;
            for (const std::string& p : parts_) all += p;
            parts_.clear();
            got_.clear();
            MpLobbyModel m;
            if (!decode_options(all, m)) return;
            model_ = m;
            rev_ = rev;
            rebind_slot();
            if (state_ == State::Joining && local_slot_ >= 0) {
                state_ = State::Joined;
                system_line("Joined the game.");
            }
            if (local_slot_ >= 0) {
                const MpSlot& me = model_.slots[local_slot_];
                has_map_ = have_map ? have_map(model_.map) : true;
                ready_ = me.ready;                  // the host is authoritative (it clears ready on changes)
                if (me.has_map != has_map_) send_ready(now);   // report whether we have this map
            }
            if (go_pending_ && go_rev_ == rev_) {
                model_.seed = go_seed_;
                go_pending_ = false;
                state_ = State::Started;
                build_session(false, go_peers_);
            }
            return;
        }
        case kCmdGo: {
            go_rev_ = r.u32();
            go_seed_ = r.u32();
            go_peers_.clear();
            const int n = r.u8();
            for (int i = 0; i < n && i < kSlots && !r.bad; ++i) {
                const int slot = r.u8();
                const PeerId p = r.u64();
                if (!r.bad && slot != local_slot_) go_peers_.emplace_back(slot, p);
            }
            go_peers_.emplace_back(0, host_);   // the host: the peer we already talk to
            if (go_rev_ == rev_ && state_ == State::Joined) {
                model_.seed = go_seed_;
                state_ = State::Started;
                build_session(false, go_peers_);
            } else {
                go_pending_ = true;
            }
            return;
        }
        case kCmdMessage: {
            ChatPacket c;
            if (decode_chat(body, c)) add_chat(ChatLine{c.sender, c.text, c.color});
            return;
        }
        case kCmdSignOff:
            state_ = State::Rejected;
            reason_ = RejectReason::HostLeft;
            system_line(reject_text(reason_));
            return;
        case kCmdAway: {   // W9-04-07: the host's app went to the background / came back
            const bool back = r.u8() != 0;
            const std::uint16_t secs = std::min<std::uint16_t>(r.u16(), kMaxAwaySeconds);
            if (back) {
                if (host_away_until_ == 0) return;
                host_away_until_ = 0;
                system_line("The host is back.");
            } else {
                if (host_away_until_ != 0) return;
                host_away_until_ = now + 1000ull * secs;
                system_line("The host is away (" + std::to_string(secs) + " s).");
            }
            return;
        }
        default:
            return;
    }
}

void LobbyClient::update(std::uint64_t now) {
    now_ = now;
    if (heard_ == 0) heard_ = now;
    std::vector<LobbyChannel::In> in;
    std::vector<PeerId> lost;
    ch_.poll(now, in, lost);
    for (const auto& m : in) {
        if (m.from != host_) continue;   // the lobby is a star: only the host talks to us
        heard_ = now;
        if (state_ == State::Rejected || state_ == State::Lost) continue;
        handle(m.body, now);
    }
    if (state_ == State::Joining && local_slot_ >= 0 && now - heard_ > kSilenceMs) {
        state_ = State::Lost;
        reason_ = RejectReason::Lost;
        system_line(reject_text(reason_));
        return;
    }
    if (state_ == State::Joining && local_slot_ < 0 && (last_query_ == 0 || now - last_query_ >= kQueryMs)) {
        if (queries_ >= kMaxQueries) {
            state_ = State::Lost;
            reason_ = RejectReason::Lost;
            system_line("No answer from the host.");
            return;
        }
        BodyWriter w;
        w.u32(kCmdQueryJoin);
        w.u16(me_.net_proto);
        w.u32(me_.sim_ver);
        w.u32(me_.rules_crc);
        w.u64(me_.player_id);
        w.fixed(sanitize_name(me_.nick), 20);
        w.fixed(me_.version, 24);
        w.u8(me_.platform);
        ch_.send(host_, w.b, false, now);   // repeated until CONFIRM / REJECT
        last_query_ = now;
        ++queries_;
    }
    if (state_ == State::Joined) {
        // W9-04-07: while the host is away its silence and our un-ACKed
        // packets are excused (they were dropped: it resends the options).
        if (!lost.empty() && now < host_away_until_) {
            ch_.forget(host_);
            lost.clear();
        }
        if (!lost.empty() || (now - std::max(heard_, host_away_until_) > kSilenceMs && now >= host_away_until_)) {
            state_ = State::Lost;
            reason_ = RejectReason::Lost;
            system_line(reject_text(reason_));
            return;
        }
        if (now - pinged_ >= kPingMs) {
            ch_.send(host_, simple(kCmdPing), false, now);
            pinged_ = now;
        }
    }
}

}  // namespace ra2mp
