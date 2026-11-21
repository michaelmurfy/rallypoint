// engine/mp/mp_model.cpp -- see mp_model.hpp.
#include "mp/mp_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "skirmish_lobby.hpp"

#include "savegame.hpp"   // save::kFormatVersion (W9-05-02)

#ifndef RA2_SIM_BUILD
#define RA2_SIM_BUILD "unknown-build"
#endif

namespace ra2mp {

const char* sim_build_id() { return RA2_SIM_BUILD; }

std::uint32_t sim_version() {
    static const std::uint32_t v = [] {
        std::uint32_t h = 2166136261u;
        auto mix = [&h](const std::string& t) {
            for (unsigned char c : t) h = (h ^ c) * 16777619u;
            h = (h ^ 0xFFu) * 16777619u;   // separator
        };
        mix(RA2_SIM_BUILD);
        mix(std::to_string(ra2engine::save::kFormatVersion));
        if (const char* salt = std::getenv("RA2_NET_SIM_SALT"); salt && *salt) mix(salt);
        return h == 0 ? 1u : h;
    }();
    return v;
}

std::string sanitize_name(const std::string& s, std::size_t max_len) {
    std::string o;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || u == 127) continue;
        o += (c == ',' || c == ':' || c == '=') ? '_' : c;
        if (o.size() >= max_len) break;
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    while (!o.empty() && o.front() == ' ') o.erase(o.begin());
    return o;
}

namespace {
const char kKindChar[4] = {'O', 'C', 'H', 'A'};

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

bool to_int(const std::string& s, long long& v, int base = 10) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtoll(s.c_str(), &end, base);
    return end && *end == 0;
}
}  // namespace

std::string encode_options(const MpLobbyModel& m) {
    std::ostringstream o;
    const MpOptions& p = m.opt;
    o << m.seed << ',' << p.game_speed << ',' << p.game_speed << ',' << p.credits << ',' << p.unit_count << ','
      << (p.short_game ? 1 : 0) << ',' << (p.superweapons ? 1 : 0) << ',' << (p.build_off_ally ? 1 : 0) << ','
      << (p.mcv_repacks ? 1 : 0) << ',' << (p.crates ? 1 : 0) << ',' << m.mode << ",0,0," << m.map.size << ','
      << (m.map.official ? 1 : 0) << ",0," << sanitize_name(m.map.file, 64) << ':' << sanitize_name(m.map.digest, 64)
      << ',';
    for (int i = 0; i < kSlots; ++i) {
        const MpSlot& s = m.slots[i];
        if (s.kind != SlotKind::Human) continue;
        const unsigned flags = (s.ready ? 1u : 0u) | (s.host ? 2u : 0u) | (s.has_map ? 4u : 0u);
        char words[64];
        std::snprintf(words, sizeof words, "%x,%x,%x,", static_cast<unsigned>(i), flags, s.pid_lo);
        o << sanitize_name(s.name) << ',' << s.country << ',' << s.color << ',' << s.start << ',' << s.team << ','
          << words;
    }
    for (int i = 0; i < kSlots; ++i) {
        const MpSlot& s = m.slots[i];
        if (s.kind != SlotKind::AI) continue;
        o << s.ai << ',' << s.country << ',' << s.color << ',' << s.start << ',' << s.team << ',';
    }
    std::string layout;
    for (int i = 0; i < kSlots; ++i) layout += kKindChar[static_cast<int>(m.slots[i].kind) & 3];
    o << "tl=" << p.tech_level << ",max=" << m.map.max_players << ",slots=" << layout;
    // NET-PLAY extensions: a leaving player's house -> AI ("ait"), mixed
    // Windows / Android sessions allowed ("xp").
    if (p.ai_takeover) o << ",ait=1";
    if (p.cross_platform) o << ",xp=1";
    return o.str();
}

bool decode_options(const std::string& s, MpLobbyModel& out) {
    std::vector<std::string> t = split(s, ',');
    if (t.size() < 17) return false;
    // Extensions (key=value) at the end.
    std::string layout;
    long long tl = 10, maxp = 8, ait = 0, xp = 0;
    while (!t.empty() && t.back().find('=') != std::string::npos) {
        const std::string kv = t.back();
        t.pop_back();
        const std::size_t eq = kv.find('=');
        const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
        if (k == "slots") layout = v;
        else if (k == "tl") to_int(v, tl);
        else if (k == "max") to_int(v, maxp);
        else if (k == "ait") to_int(v, ait);
        else if (k == "xp") to_int(v, xp);
    }
    if (!t.empty() && t.back().empty()) t.pop_back();   // trailing comma before the extensions
    if (t.size() < 17 || layout.size() != kSlots) return false;
    long long n[16];
    for (int i = 0; i < 16; ++i)
        if (!to_int(t[static_cast<std::size_t>(i)], n[i])) return false;
    MpLobbyModel m;
    m.seed = static_cast<std::uint32_t>(n[0]);
    m.opt.game_speed = static_cast<int>(std::max(0LL, std::min(6LL, n[1])));
    m.opt.credits = static_cast<int>(n[3]);
    m.opt.unit_count = static_cast<int>(n[4]);
    m.opt.short_game = n[5] != 0;
    m.opt.superweapons = n[6] != 0;
    m.opt.build_off_ally = n[7] != 0;
    m.opt.mcv_repacks = n[8] != 0;
    m.opt.crates = n[9] != 0;
    m.mode = static_cast<int>(n[10]);
    m.map.size = static_cast<std::uint32_t>(n[13]);
    m.map.official = n[14] != 0;
    const std::string& scen = t[16];
    const std::size_t colon = scen.find(':');
    if (colon == std::string::npos) return false;
    m.map.file = scen.substr(0, colon);
    m.map.digest = scen.substr(colon + 1);
    m.opt.tech_level = static_cast<int>(tl);
    m.opt.ai_takeover = ait != 0;
    m.opt.cross_platform = xp != 0;
    m.map.max_players = static_cast<int>(std::max(1LL, std::min(8LL, maxp)));
    int humans = 0, ais = 0;
    for (char c : layout) {
        if (c == 'H') ++humans;
        else if (c == 'A') ++ais;
        else if (c != 'O' && c != 'C') return false;
    }
    if (t.size() != 17u + static_cast<std::size_t>(humans) * 8u + static_cast<std::size_t>(ais) * 5u) return false;
    std::size_t k = 17;
    // Humans carry their slot index in the first address word.
    for (int h = 0; h < humans; ++h, k += 8) {
        long long side, color, start, team, slot, flags, pid;
        if (!to_int(t[k + 1], side) || !to_int(t[k + 2], color) || !to_int(t[k + 3], start) ||
            !to_int(t[k + 4], team) || !to_int(t[k + 5], slot, 16) || !to_int(t[k + 6], flags, 16) ||
            !to_int(t[k + 7], pid, 16))
            return false;
        if (slot < 0 || slot >= kSlots || layout[static_cast<std::size_t>(slot)] != 'H') return false;
        MpSlot& sl = m.slots[slot];
        sl.kind = SlotKind::Human;
        sl.name = t[k];
        sl.country = static_cast<int>(side);
        sl.color = static_cast<int>(color);
        sl.start = static_cast<int>(start);
        sl.team = static_cast<int>(team);
        sl.ready = (flags & 1) != 0;
        sl.host = (flags & 2) != 0;
        sl.has_map = (flags & 4) != 0;
        sl.pid_lo = static_cast<std::uint32_t>(pid);
    }
    // AIs fill the 'A' slots in order.
    int ai_slot = 0;
    for (int a = 0; a < ais; ++a, k += 5) {
        while (ai_slot < kSlots && layout[static_cast<std::size_t>(ai_slot)] != 'A') ++ai_slot;
        long long lvl, country, color, start, team;
        if (!to_int(t[k], lvl) || !to_int(t[k + 1], country) || !to_int(t[k + 2], color) ||
            !to_int(t[k + 3], start) || !to_int(t[k + 4], team))
            return false;
        MpSlot& sl = m.slots[ai_slot++];
        sl.kind = SlotKind::AI;
        sl.ai = static_cast<int>(std::max(0LL, std::min(2LL, lvl)));
        sl.country = static_cast<int>(country);
        sl.color = static_cast<int>(color);
        sl.start = static_cast<int>(start);
        sl.team = static_cast<int>(team);
    }
    for (int i = 0; i < kSlots; ++i)
        if (layout[static_cast<std::size_t>(i)] == 'C') m.slots[i].kind = SlotKind::Closed;
    out = m;
    return true;
}

std::string map_digest_of(const std::string& text) {
    std::string section;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t e = text.find('\n', i);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(i, e - i);
        i = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] == '[') {
            const std::size_t c = line.find(']');
            section = line.substr(1, c == std::string::npos ? std::string::npos : c - 1);
            for (char& ch : section) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            continue;
        }
        if (section != "DIGEST") continue;
        const std::size_t eq = line.find('=');
        if (eq != std::string::npos && line.substr(0, eq) == "1") return line.substr(eq + 1);
    }
    return {};
}

int human_count(const MpLobbyModel& m) {
    int n = 0;
    for (const MpSlot& s : m.slots) n += s.kind == SlotKind::Human ? 1 : 0;
    return n;
}

int player_count(const MpLobbyModel& m) {
    int n = 0;
    for (const MpSlot& s : m.slots) n += s.occupied() ? 1 : 0;
    return n;
}

int first_open_slot(const MpLobbyModel& m) {
    for (int i = 0; i < std::min(kSlots, m.map.max_players); ++i)
        if (m.slots[i].kind == SlotKind::Open) return i;
    return -1;
}

int color_owner(const MpLobbyModel& m, int color, int except) {
    if (color < 0) return -1;
    for (int i = 0; i < kSlots; ++i)
        if (i != except && m.slots[i].occupied() && m.slots[i].color == color) return i;
    return -1;
}

int start_owner(const MpLobbyModel& m, int start, int except) {
    if (start < 0) return -1;
    for (int i = 0; i < kSlots; ++i)
        if (i != except && m.slots[i].occupied() && m.slots[i].start == start) return i;
    return -1;
}

StartCheck can_start(const MpLobbyModel& m) {
    if (m.map.file.empty()) return StartCheck::NoMap;
    const int players = player_count(m);
    if (players < 2) return StartCheck::NeedTwoPlayers;
    if (players > m.map.max_players) return StartCheck::TooManyPlayers;
    for (const MpSlot& s : m.slots) {
        if (s.kind != SlotKind::Human) continue;
        if (!s.has_map) return StartCheck::MissingMap;
        if (!s.host && !s.ready) return StartCheck::NotReady;
    }
    return StartCheck::Ok;
}

const char* start_check_text(StartCheck c) {
    switch (c) {
        case StartCheck::Ok: return "";
        case StartCheck::NeedTwoPlayers: return "You need at least two players to start the game!";
        case StartCheck::TooManyPlayers: return "Too many players for this map.";
        case StartCheck::NotReady: return "Waiting for every player to be ready.";
        case StartCheck::MissingMap: return "A player does not have this map.";
        case StartCheck::NoMap: return "Choose a map first.";
    }
    return "";
}

// --- session -------------------------------------------------------------------

namespace {
// The resolution RNG: a 32-bit LCG (Numerical Recipes constants), identical
// on every machine and platform.
struct SessionRng {
    std::uint32_t s;
    explicit SessionRng(std::uint32_t seed) : s(seed ^ 0x5EED5EEDu) {}
    std::uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
    int range(int lo, int hi) { return hi <= lo ? lo : lo + static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1)); }
};
}  // namespace

void resolve_session(MpSessionConfig& s, int country_count) {
    s.players.clear();
    SessionRng rng(s.seed);
    const MpLobbyModel& m = s.lobby;
    bool color_used[8] = {};
    bool start_used[8] = {};
    for (const MpSlot& sl : m.slots)
        if (sl.occupied()) {
            if (sl.color >= 0 && sl.color < 8) color_used[sl.color] = true;
            if (sl.start >= 0 && sl.start < 8) start_used[sl.start] = true;
        }
    const int nc = std::max(1, country_count);
    const int maxp = std::max(1, std::min(8, m.map.max_players));
    for (int i = 0; i < kSlots; ++i) {
        const MpSlot& sl = m.slots[i];
        if (!sl.occupied()) continue;
        MpPlayer p;
        p.slot = i;
        p.house = static_cast<int>(s.players.size());
        p.is_ai = sl.kind == SlotKind::AI;
        p.local = i == s.local_slot;
        p.name = p.is_ai ? std::string() : sl.name;
        p.ai = sl.ai;
        p.team = sl.team;
        p.pid_lo = sl.pid_lo;
        p.country = (sl.country >= 0 && sl.country < nc) ? sl.country : rng.range(0, nc - 1);
        if (sl.color >= 0 && sl.color < 8) {
            p.color = sl.color;
        } else {
            int free = 0;
            for (bool u : color_used) free += u ? 0 : 1;
            int c = rng.range(0, 7);
            while (free > 0 && color_used[c]) c = rng.range(0, 7);
            p.color = c;
            color_used[c] = true;
        }
        if (sl.start >= 0 && sl.start < maxp) {
            p.start = sl.start;
        } else {
            int free = 0;
            for (int k = 0; k < maxp; ++k) free += start_used[k] ? 0 : 1;
            int st = rng.range(0, maxp - 1);
            while (free > 0 && start_used[st]) st = rng.range(0, maxp - 1);
            p.start = st;
            start_used[st] = true;
        }
        if (p.local) s.local_house = p.house;
        for (const auto& sp : s.slot_peers)
            if (sp.first == i && !p.local && !p.is_ai) p.peer = sp.second;
        s.players.push_back(p);
    }
}

namespace {
ra2engine::SkirmishConfig base_config(const MpSessionConfig& s) {
    ra2engine::SkirmishConfig cfg;
    const MpOptions& o = s.lobby.opt;
    cfg.map_file = s.lobby.map.file;
    cfg.use_map_starts = !cfg.map_file.empty();
    cfg.starting_cash = o.credits;
    cfg.unit_count = o.unit_count;
    cfg.tech_level = o.tech_level;
    cfg.crates = o.crates;
    cfg.superweapons = o.superweapons;
    cfg.short_game = o.short_game;
    cfg.game_speed = o.game_speed;
    cfg.mcv_redeploys = o.mcv_repacks ? 1 : 0;
    cfg.build_off_ally = o.build_off_ally;   // W9-05-09 (was never passed on)
    cfg.mp_mode = s.lobby.mode > 0 ? s.lobby.mode : 1;   // W9-05-03 (1 = Battle)
    return cfg;
}

ra2engine::SkirmishSlot to_slot(const MpPlayer& p, const MpConvertCtx& ctx, bool as_ai) {
    ra2engine::SkirmishSlot sl;
    sl.country = p.country >= 0 && p.country < static_cast<int>(ctx.countries.size())
                     ? ctx.countries[static_cast<std::size_t>(p.country)] : std::string();
    sl.side = ctx.side_of ? ctx.side_of(sl.country) : ra2engine::World::Side::Allied;
    sl.color_ix = ra2engine::lobby_color_to_house(p.color);
    sl.alliance_team = p.team >= 0 ? p.team : -1;
    sl.combatant = true;
    sl.is_ai = as_ai;
    sl.diff = as_ai ? ra2engine::lobby_ai_difficulty(p.is_ai ? p.ai : ra2engine::kLobbyAiMedium)
                    : ra2engine::AiDifficulty::Normal;
    return sl;
}

void add_neutral(ra2engine::SkirmishConfig& cfg) {
    cfg.slots.push_back(ra2engine::SkirmishSlot{ra2engine::World::Side::Neutral, 2, "Civilian", -1, false, false,
                                                ra2engine::AiDifficulty::Normal});
}
}  // namespace

ra2engine::SkirmishConfig session_to_skirmish(const MpSessionConfig& s, const MpConvertCtx& ctx) {
    ra2engine::SkirmishConfig cfg = base_config(s);
    for (const MpPlayer& p : s.players) cfg.slots.push_back(to_slot(p, ctx, p.is_ai));
    add_neutral(cfg);
    return cfg;
}

ra2engine::SkirmishConfig session_to_local_preview(const MpSessionConfig& s, const MpConvertCtx& ctx) {
    ra2engine::SkirmishConfig cfg = base_config(s);
    // The local player first (house 0 = the human in the single-player game).
    for (const MpPlayer& p : s.players)
        if (p.local) cfg.slots.push_back(to_slot(p, ctx, false));
    for (const MpPlayer& p : s.players)
        if (!p.local) cfg.slots.push_back(to_slot(p, ctx, true));
    // use_map_starts binds combatant k -> start k ascending; the resolved
    // starts are reported by describe_session (the lockstep layer places them).
    add_neutral(cfg);
    return cfg;
}

std::string describe_session(const MpSessionConfig& s) {
    std::ostringstream o;
    o << "session seed=" << s.seed << " host=" << (s.is_host ? 1 : 0) << " local_slot=" << s.local_slot
      << " local_house=" << s.local_house << " transport=" << (s.transport == MpTransportKind::Lan ? "lan" : "tunnel")
      << " addr=" << s.host_addr << " map=" << s.lobby.map.file << " digest=" << s.lobby.map.digest
      << " players=" << s.players.size() << "\n";
    for (const MpPlayer& p : s.players)
        o << "  house " << p.house << " slot " << p.slot << (p.is_ai ? " AI" : " human") << (p.local ? " (local)" : "")
          << " name=" << p.name << " country=" << p.country << " color=" << p.color << " team=" << p.team
          << " start=" << p.start << "\n";
    o << "  options: " << s.options << "\n";
    return o.str();
}

}  // namespace ra2mp
