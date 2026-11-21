// P9 map triggers / scripting [RE: docs/re/map-triggers.md]. See header.
#include "map_trigger.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace ra2engine {

namespace {

bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i])) return false;
    return true;
}

// strtok(value, ",") semantics: consecutive delimiters collapse, empty
// tokens never appear (the original strtoks every field [RE §1]).
struct Tok {
    std::vector<std::string> t;
    std::size_t i = 0;
    explicit Tok(const std::string& s) {
        std::string cur;
        for (char c : s) {
            if (c == ',') { if (!cur.empty()) t.push_back(cur); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) t.push_back(cur);
    }
    const std::string* next() { return i < t.size() ? &t[i++] : nullptr; }
};

// C atoi: leading whitespace, optional sign, digits; anything else -> 0.
int c_atoi(const std::string* s) {
    if (!s) return 0;
    return std::atoi(s->c_str());
}

std::string trim_ctl(const std::string& s) {   // ParamKind 4 trim (<= 0x20)
    std::size_t b = 0, e = s.size();
    while (b < e && (unsigned char)s[b] < 0x21) ++b;
    while (e > b && (unsigned char)s[e - 1] < 0x21) --e;
    return s.substr(b, e - b);
}

bool is_none(const std::string& s) { return ieq(s, "<none>") || ieq(s, "none"); }

int find_ci(const std::vector<std::string>& v, const std::string& s) {
    for (std::size_t i = 0; i < v.size(); ++i)
        if (ieq(v[i], s)) return (int)i;
    return -1;
}

// Object-raised kinds that must match the raised kind [RE §4 preamble]
// (FUN_0071ebf0's StateA list; 19/20/21 are excluded from the match check).
bool object_kind(int k) {
    switch (k) {
    case 1: case 2: case 3: case 4: case 6: case 7: case 18: case 19: case 20:
    case 21: case 22: case 23: case 24: case 25: case 26: case 29: case 31:
    case 33: case 34: case 35: case 38: case 39: case 40: case 41: case 42:
    case 43: case 44: case 48: case 49: case 50: case 53: case 54: case 59:
        return true;
    default:
        return false;
    }
}
bool state_b(int k) {   // FUN_0071ec60
    return !(k == 1 || k == 6 || k == 31 || k == 44 || k == 59);
}
// HasOccurred cases this engine does not evaluate (world facts it does not
// model) -> false + counted [spec §2].
bool unimplemented_event(int k) {
    switch (k) {
    // W5-A [RE: HasOccurred FUN_0071DBE0]: 3 ThievedBy (house +0x244), 18
    // CiviliansEvacuated (house +0x1F9), 35 EnemyInSpotlight (spotlights) and
    // 45/46 AmbientLightBelow/Above (scenario +0x352C, raised only on an
    // ambient change, actions 71-73) have no modelled state -- none of the 24
    // missions or Boot Camp uses them.
    case 3: case 18: case 35: case 45: case 46:
        return true;
    default:
        return false;
    }
}

const char* action_label(int k) {
    switch (k) {
    case 0: return "None"; case 1: return "Win"; case 2: return "Lose";
    case 3: return "ProductionBegins"; case 4: return "CreateTeam";
    case 5: return "DestroyTeam"; case 6: return "AllToHunt";
    case 7: return "Reinforcement"; case 10: return "PlayMovie";
    case 11: return "TextTrigger"; case 12: return "DestroyTrigger";
    case 13: return "AutocreateBegins"; case 16: return "RevealAllMap";
    case 17: return "RevealAroundWaypoint"; case 19: return "PlaySoundEffect";
    case 20: return "PlayMusicTheme"; case 21: return "PlaySpeech";
    case 22: return "ForceTrigger"; case 23: return "TimerStart";
    case 24: return "TimerStop"; case 25: return "TimerExtend";
    case 26: return "TimerShorten"; case 27: return "TimerSet";
    case 28: return "GlobalSet"; case 29: return "GlobalClear";
    case 41: return "PlayAnimAt"; case 46: return "LockInput";
    case 47: return "UnlockInput"; case 48: return "MoveCameraToWaypoint";
    case 53: return "EnableTrigger"; case 54: return "DisableTrigger";
    case 55: return "CreateRadarEvent"; case 56: return "LocalSet";
    case 57: return "LocalClear"; case 67: return "AnnounceWin";
    case 68: return "AnnounceLose"; case 74: return "AITriggersBegin";
    case 75: return "AITriggersStop"; case 80: return "ReinforcementAt";
    case 99: return "PlaySoundAtWaypoint"; case 100: return "PlayIngameMovie";
    case 103: return "TimerText"; case 104: return "FlashTeam";
    case 107: return "ReinforcementByChrono"; case 112: return "CenterCamera";
    case 113: return "MakeHouseCheer"; case 114: return "SetTabTo";
    case 115: return "FlashCameo"; case 116: return "PlayMovieAndPause";
    case 125: return "CreateBuilding"; case 60: return "SellBuilding"; case 111: return "EvictOccupiers"; case 108: return "CreateCrate"; case 101: return "ReshroudMapAtWaypoint"; case 9: return "FireSale"; case 131: return "FlashBuildingsOfType";
    case 35: return "PreferredTarget"; case 40: return "ResizeView"; case 44: return "IonStorm";
    case 51: return "Reshroud"; case 61: return "TurnOff"; case 62: return "TurnOn";
    case 70: return "DestroyTag"; case 77: return "RatioAir"; case 78: return "RatioInfantry";
    case 89: return "RemoveParticleAnim"; case 117: return "PlayIngameMoviePause";
    default: return "?";
    }
}

bool presentation_action(int k) {
    switch (k) {
    case 10: case 11: case 19: case 20: case 21: case 41: case 46: case 47:
    case 48: case 55: case 89: case 99: case 100: case 103: case 104: case 112:
    case 113: case 114: case 115: case 116: case 117: case 131:
        return true;
    default:
        return false;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
unsigned map_event_flags(int k) {
    unsigned f = 0;
    switch (k) {
    case 0: case 1: case 4: case 8: case 24: case 25: case 26: case 31:
    case 53: case 54: case 59:
        f |= kAttachCell;
    }
    switch (k) {
    case 0: case 1: case 2: case 4: case 6: case 7: case 8: case 29: case 33:
    case 34: case 35: case 38: case 39: case 40: case 41: case 42: case 43:
    case 44: case 48: case 49:
        f |= kAttachObject;
    }
    if (k == 8 || k == 24) f |= kAttachMap;
    switch (k) {
    case 3: case 5: case 8: case 9: case 10: case 11: case 12: case 15:
    case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 30:
    case 32: case 52: case 55: case 56: case 57: case 58:
        f |= kAttachHouse;
    }
    switch (k) {
    case 8: case 13: case 14: case 23: case 27: case 28: case 36: case 37:
    case 45: case 46: case 47: case 50: case 51: case 60: case 61:
        f |= kAttachLogic;
    }
    return f;
}

unsigned map_action_flags(int k) {
    switch (k) {
    case 14: case 32: case 60: case 61: case 62: case 91: case 111:
        return kAttachObject;
    default:
        return 0;
    }
}

int map_waypoint_letters(const std::string& s) {
    if (s.empty() || !std::isalpha((unsigned char)s[0])) return -1;
    int w = std::toupper((unsigned char)s[0]) - 'A';
    if (s.size() >= 2 && std::isalpha((unsigned char)s[1]))
        return std::toupper((unsigned char)s[1]) - 0x27 + w * 26;
    return w;
}

int MapTriggerData::find_trigger(const std::string& id) const {
    for (std::size_t i = 0; i < triggers.size(); ++i)
        if (ieq(triggers[i].id, id)) return (int)i;
    return -1;
}
int MapTriggerData::find_tag(const std::string& id) const {
    for (std::size_t i = 0; i < tags.size(); ++i)
        if (ieq(tags[i].id, id)) return (int)i;
    return -1;
}
int MapTriggerData::find_house_type(const std::string& name) const {
    return find_ci(house_types, name);
}
int MapTriggerData::find_building_type(const std::string& id) const {
    return find_ci(building_types, id);
}

std::vector<std::string> ini_list_values(const ra2assets::Ini& ini, const std::string& section) {
    std::vector<std::string> out;
    for (const auto& kv : ini.section(section)) {
        // Inline ';' comments are not part of the value (rulesmd [Countries]
        // carries "0=Americans ; Starts at zero ...").
        std::string v = kv.second.substr(0, kv.second.find(';'));
        while (!v.empty() && (unsigned char)v.back() <= 0x20) v.pop_back();
        std::size_t b = 0;
        while (b < v.size() && (unsigned char)v[b] <= 0x20) ++b;
        v = v.substr(b);
        if (!v.empty()) out.push_back(v);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Parser [RE §1].
namespace {

struct Parser {
    const ra2assets::Ini& ini;
    MapTriggerData d;

    explicit Parser(const ra2assets::Ini& i) : ini(i) {}

    // FUN_00726d40: "<none>"/"none" -> null; else find-or-create.
    int trigger_ref(const std::string& id) {
        if (is_none(id)) return -1;
        int i = d.find_trigger(id);
        if (i >= 0) return i;
        MapTriggerType t;
        t.id = id;
        // ctor defaults (FUN_00725f20): Enabled + all difficulties = 1.
        t.enabled = true;
        t.difficulty[0] = t.difficulty[1] = t.difficulty[2] = true;
        d.triggers.push_back(t);
        return (int)d.triggers.size() - 1;
    }
    // FUN_006e5880.
    int tag_ref(const std::string& id) {
        if (is_none(id)) return -1;
        int i = d.find_tag(id);
        if (i >= 0) return i;
        MapTagType t;
        t.id = id;
        d.tags.push_back(t);
        return (int)d.tags.size() - 1;
    }
    int team_ref(const std::string& tok) {
        if (is_none(tok)) return -1;
        return find_ci(d.team_ids, tok);
    }
    // ParamKind 1/2/3/5 shape: atoi == -1 -> null; strlen < 3 -> array index;
    // else ID lookup [RE §1.3].
    template <typename F>
    int indexed_ref(const std::string* tok, int array_size, F by_id) {
        if (!tok) return -1;
        const int n = c_atoi(tok);
        if (n == -1) return -1;
        if (tok->size() < 3) return (n >= 0 && n < array_size) ? n : -1;
        return by_id(*tok);
    }

    void read_trigger(int idx, const std::string& value) {
        Tok tk(value);
        MapTriggerType& t0 = d.triggers[idx];
        t0.defined = true;
        const std::string* house = tk.next();
        t0.house = house ? *house : std::string();
        if (house && ieq(*house, "<none>")) t0.house_type = 0;
        else d.triggers[idx].house_type = house ? d.find_house_type(*house) : -1;
        const std::string* linked = tk.next();
        const int link = linked ? trigger_ref(*linked) : -1;   // may grow triggers
        MapTriggerType& t = d.triggers[idx];
        t.linked = link;
        const std::string* name = tk.next();
        t.name = name ? name->substr(0, 0x30) : std::string();
        const std::string* dis = tk.next();
        t.enabled = !(dis == nullptr || c_atoi(dis) != 0);
        for (int k = 0; k < 3; ++k) {
            const std::string* f = tk.next();
            t.difficulty[k] = f != nullptr && c_atoi(f) != 0;
        }
        const std::string* mt = tk.next();
        if (mt && c_atoi(mt) != 0) t.must_transfer = true;

        // [Events]: count, then (kind, param, value[, string]) prepended.
        const std::string ev = ini.get("Events", t.id, "");
        if (!ev.empty()) {
            Tok et(ev);
            int n = c_atoi(et.next());
            std::vector<MapEvent> evs;
            for (; n > 0; --n) {
                MapEvent e;
                e.kind = c_atoi(et.next());
                e.param = c_atoi(et.next());
                const std::string* vtok = et.next();
                e.value = c_atoi(vtok);
                if (e.param == 2) {
                    const std::string* s = et.next();
                    if (s) e.str = s->substr(0, 0x18);
                } else if (e.param == 1 && vtok) {
                    e.str = *vtok;   // TeamType reference
                }
                evs.insert(evs.begin(), e);   // prepend [RE §1.2]
            }
            d.triggers[idx].events = evs;
        }

        // [Actions]: count, then 8-token groups, appended.
        const std::string ac = ini.get("Actions", d.triggers[idx].id, "");
        if (!ac.empty()) {
            Tok at(ac);
            int n = c_atoi(at.next());
            std::vector<MapAction> acts;
            for (; n > 0; --n) {
                MapAction a;
                a.kind = c_atoi(at.next());
                a.param_kind = c_atoi(at.next());
                const std::string* p1 = at.next();
                const int iv = c_atoi(p1);
                switch (a.param_kind) {
                case 0: case 11:
                    a.value = iv;
                    break;
                case 1: case 5:
                    a.team = indexed_ref(p1, (int)d.team_ids.size(),
                                         [&](const std::string& s) { return team_ref(s); });
                    if (p1) a.text = *p1;
                    break;
                case 2:
                    a.trigger = indexed_ref(p1, (int)d.triggers.size(),
                                            [&](const std::string& s) { return trigger_ref(s); });
                    break;
                case 3:
                    a.tag = indexed_ref(p1, (int)d.tags.size(),
                                        [&](const std::string& s) { return tag_ref(s); });
                    break;
                case 4:
                    if (iv != -1 && p1) a.text = trim_ctl(*p1).substr(0, 0x1F);
                    break;
                case 6: case 7: case 8:   // speech / sound / theme name
                    if (p1) a.text = *p1;
                    break;
                case 9: case 10:
                    if (p1) a.text = p1->substr(0, 0x18);
                    break;
                default:
                    break;
                }
                for (int k = 0; k < 4; ++k) a.p[k] = c_atoi(at.next());
                const std::string* last = at.next();
                if (last) {
                    if (a.param_kind == 5 || a.param_kind == 9) a.value = c_atoi(last);
                    else if (a.param_kind == 11) a.value2 = c_atoi(last);
                    else a.waypoint = map_waypoint_letters(*last);
                }
                acts.push_back(a);
            }
            d.triggers[idx].actions = acts;
        }
    }

    void run(const std::vector<std::string>& rules_countries,
             const std::vector<std::string>& rules_buildings) {
        // Type lists are FindOrAllocate'd by name: a repeated entry does not
        // take a new index (rulesmd [BuildingTypes] lists NAPSYA twice; the
        // sov07tmd value 300 = YACNST only holds with the duplicate folded)
        // [RE §6].
        for (const std::string& c : rules_countries)
            if (find_ci(d.house_types, c) < 0) d.house_types.push_back(c);
        for (const std::string& c : ini_list_values(ini, "Countries"))
            if (find_ci(d.house_types, c) < 0) d.house_types.push_back(c);
        for (const std::string& b : rules_buildings)
            if (find_ci(d.building_types, b) < 0) d.building_types.push_back(b);
        for (const std::string& b : ini_list_values(ini, "BuildingTypes"))
            if (find_ci(d.building_types, b) < 0) d.building_types.push_back(b);

        // TeamTypes (create pass, then read pass: Tag= creates TagTypes) [RE §1.7/§1.8].
        for (const std::string& id : ini_list_values(ini, "TeamTypes"))
            if (find_ci(d.team_ids, id) < 0) d.team_ids.push_back(id);
        for (const std::string& id : d.team_ids) {
            const std::string tag = ini.get(id, "Tag", "");
            if (!tag.empty()) tag_ref(tag);
        }

        // Triggers [RE §1.1]: find-or-create, then read, in key order.
        for (const auto& kv : ini.section("Triggers")) {
            const int idx = trigger_ref(kv.first);
            if (idx < 0) continue;
            read_trigger(idx, kv.second);
        }

        // Tags [RE §1.4].
        for (const auto& kv : ini.section("Tags")) {
            const int idx = tag_ref(kv.first);
            if (idx < 0) continue;
            Tok tk(kv.second);
            const int pers = c_atoi(tk.next());
            const std::string* name = tk.next();
            const std::string* trig = tk.next();
            const int first = trig ? trigger_ref(*trig) : -1;
            MapTagType& t = d.tags[idx];
            t.defined = true;
            t.persistence = pers;
            t.name = name ? name->substr(0, 0x30) : std::string();
            t.first_trigger = first;
        }

        // Locals [RE §1.5].
        d.local_names.assign(100, std::string());
        d.local_init.assign(100, 0);
        {
            const auto& vn = ini.section("VariableNames");
            const std::size_t n = std::min<std::size_t>(vn.size(), 100);
            for (std::size_t i = 0; i < n; ++i) {
                const int idx = std::atoi(vn[i].first.c_str());
                if (idx < 0 || idx >= 100) continue;
                Tok tk(vn[i].second);
                const std::string* name = tk.next();
                d.local_names[idx] = name ? *name : std::string();
                const std::string* val = tk.next();
                if (val) d.local_init[idx] = c_atoi(val) != 0 ? 1 : 0;
            }
        }

        // CellTags [RE §1.6] (NewINIFormat >= 4 packing; first tag per cell).
        for (const auto& kv : ini.section("CellTags")) {
            const int tag = tag_ref(kv.second);
            const int v = std::atoi(kv.first.c_str());
            MapCellTag c;
            c.x = v % 1000;
            c.y = v / 1000;
            c.tag = tag;
            if (tag < 0) continue;
            bool dup = false;
            for (const MapCellTag& o : d.cell_tags)
                if (o.x == c.x && o.y == c.y) { dup = true; break; }
            if (!dup) d.cell_tags.push_back(c);
        }
    }
};

}  // namespace

MapTriggerData parse_map_triggers(const ra2assets::Ini& map,
                                  const std::vector<std::string>& rules_countries,
                                  const std::vector<std::string>& rules_buildings) {
    Parser p(map);
    p.run(rules_countries, rules_buildings);
    return p.d;
}

// ---------------------------------------------------------------------------
// Runtime [RE §2/§3].

MapTriggerSystem::MapTriggerSystem(const MapTriggerData& data, int difficulty,
                                   std::uint32_t rng_seed)
    : d_(data),
      difficulty_(std::max(0, std::min(2, difficulty))),
      rng_(rng_seed ? rng_seed : 0x2545F491u),
      globals_(50, 0),
      locals_(100, 0),
      tag_of_type_(data.tags.size(), -1),
      house_lists_(data.house_types.size()),
      house_flags_(data.house_types.size(), 0) {
    ev_base_.resize(d_.triggers.size(), 0);
    int total = 0;
    for (std::size_t i = 0; i < d_.triggers.size(); ++i) {
        ev_base_[i] = total;
        total += (int)d_.triggers[i].events.size();
    }
    ev_house_.assign((std::size_t)total, -1);
}

void MapTriggerSystem::logf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    log_.emplace_back(buf);
}

std::uint32_t MapTriggerSystem::rand_inclusive(int hi) {
    // xorshift32 [RE §7 item 1: same inclusive range, own stream].
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    if (hi <= 0) return 0;
    return rng_ % (std::uint32_t)(hi + 1);
}

bool MapTriggerSystem::local(int i) const {
    return i >= 0 && i < 100 && locals_[(std::size_t)i] != 0;
}
bool MapTriggerSystem::global(int i) const {
    return i >= 0 && i < 50 && globals_[(std::size_t)i] != 0;
}

void MapTriggerSystem::notify_variable(bool is_global, int idx) {
    // FUN_006e4d60/90 -> FUN_00725640/670: every tag, every trigger whose type
    // has a Set/Cleared event on idx -> ResetTimers [RE §3.6].
    const int k1 = is_global ? kEvGlobalSet : kEvLocalSet;
    const int k2 = is_global ? kEvGlobalCleared : kEvLocalCleared;
    for (Tag& tag : tags_) {
        if (tag.destroyed) continue;
        for (Trig& t : tag.trigs) {
            for (const MapEvent& e : d_.triggers[(std::size_t)t.type].events)
                if ((e.kind == k1 || e.kind == k2) && e.value == idx) {
                    reset_timers(t);
                    break;
                }
        }
    }
}

void MapTriggerSystem::set_local(int i, bool v) {
    if (i < 0 || i >= 100) return;
    const std::uint8_t nv = v ? 1 : 0;
    if (locals_[(std::size_t)i] == nv) return;
    locals_[(std::size_t)i] = nv;
    vars_changed_ = true;
    notify_variable(false, i);
}
void MapTriggerSystem::set_global(int i, bool v) {
    if (i < 0 || i >= 50) return;
    const std::uint8_t nv = v ? 1 : 0;
    if (globals_[(std::size_t)i] == nv) return;
    globals_[(std::size_t)i] = nv;
    vars_changed_ = true;
    notify_variable(true, i);
}

unsigned MapTriggerSystem::house_flags(int ht) const {
    return (ht >= 0 && ht < (int)house_flags_.size()) ? house_flags_[(std::size_t)ht] : 0u;
}

void MapTriggerSystem::reset_timers(Trig& t) {
    // FUN_007256a0 [RE §3.5]: events in memory order; the last timer event wins.
    const MapTriggerType& ty = d_.triggers[(std::size_t)t.type];
    unsigned b = 0;
    for (const MapEvent& e : ty.events) {
        if (e.kind == kEvElapsedTime) {
            t.t_start = frame_;
            t.t_left = (long)e.value * 15;
            t.bits &= ~(1u << (b & 31));
        }
        if (e.kind == kEvRandomDelay) {
            const int r = (int)rand_inclusive(e.value);
            t.t_start = frame_;
            t.t_left = (long)(e.value / 2 + r) * 15;
            t.bits &= ~(1u << (b & 31));
        }
        ++b;
    }
}

int MapTriggerSystem::get_instance(int tag_type) {
    if (tag_type < 0 || tag_type >= (int)d_.tags.size()) return -1;
    if (tag_of_type_[(std::size_t)tag_type] >= 0) return tag_of_type_[(std::size_t)tag_type];
    Tag tag;
    tag.type = tag_type;
    // FUN_006e4350: one new TriggerClass per chained TriggerType, PREPENDED.
    std::set<int> seen;
    for (int tt = d_.tags[(std::size_t)tag_type].first_trigger; tt >= 0 && !seen.count(tt);
         tt = d_.triggers[(std::size_t)tt].linked) {
        seen.insert(tt);
        Trig t;
        t.type = tt;
        t.t_start = frame_;
        t.t_left = 0;
        reset_timers(t);
        const MapTriggerType& ty = d_.triggers[(std::size_t)tt];
        // FUN_00725240 difficulty gate [RE §3.7].
        if (!ty.enabled || !ty.difficulty[difficulty_]) t.enabled = false;
        tag.trigs.insert(tag.trigs.begin(), t);
    }
    tags_.push_back(tag);
    const int idx = (int)tags_.size() - 1;
    tag_of_type_[(std::size_t)tag_type] = idx;
    return idx;
}

void MapTriggerSystem::start(TriggerHost& host) {
    for (std::size_t i = 0; i < 100 && i < d_.local_init.size(); ++i)
        locals_[i] = d_.local_init[i];
    std::fill(globals_.begin(), globals_.end(), 0);
    // Cell tags are instantiated while the map is read, before scenario
    // start (FUN_004acd80 precedes FUN_00684670) [RE §1.6/§1.8].
    for (const MapCellTag& c : d_.cell_tags) get_instance(c.tag);
    // FUN_00684670 [RE §3.2].
    for (int tt = 0; tt < (int)d_.tags.size(); ++tt) {
        unsigned flags = 0;
        std::set<int> seen;
        for (int tr = d_.tags[(std::size_t)tt].first_trigger; tr >= 0 && !seen.count(tr);
             tr = d_.triggers[(std::size_t)tr].linked) {
            seen.insert(tr);
            for (const MapEvent& e : d_.triggers[(std::size_t)tr].events) flags |= map_event_flags(e.kind);
            for (const MapAction& a : d_.triggers[(std::size_t)tr].actions) flags |= map_action_flags(a.kind);
        }
        if (flags & kAttachMap) map_list_.push_back(get_instance(tt));
        if (flags & kAttachLogic) logic_list_.push_back(get_instance(tt));
        if (flags & kAttachHouse) {
            const int inst = get_instance(tt);
            const int first = d_.tags[(std::size_t)tt].first_trigger;
            const int ht = first >= 0 ? d_.triggers[(std::size_t)first].house_type : -1;
            if (ht >= 0 && ht < (int)house_lists_.size() && host.house_exists(ht))
                house_lists_[(std::size_t)ht].push_back(inst);
        }
    }
    logf("f=%ld start tags=%d logic=%d map=%d", frame_, (int)tags_.size(),
         (int)logic_list_.size(), (int)map_list_.size());
}

int MapTriggerSystem::trigger_house(const Trig& t, TriggerHost& host) const {
    const int ht = d_.triggers[(std::size_t)t.type].house_type;
    return (ht >= 0 && host.house_exists(ht)) ? ht : -1;
}

int MapTriggerSystem::find_house(const Trig& t, int v, TriggerHost& host) const {
    // FUN_006e3b50.
    if (v == 8997) return t.house_override;
    if (v == -1) return -1;
    return host.house_exists(v) ? v : -1;
}

bool MapTriggerSystem::has_occurred(int type, int ev_idx, int kind, int trig_house,
                                    const RaiseContext& ctx, Trig& t, bool& persistent,
                                    TriggerHost& host) {
    const MapEvent& e = d_.triggers[(std::size_t)type].events[(std::size_t)ev_idx];
    auto expired = [&](long start, long left) {
        if (start != -1) {
            const long el = frame_ - start;
            if (left <= el) return true;
            left -= el;
        }
        return left == 0;
    };
    switch (e.kind) {
    case kEvElapsedTime:
    case kEvRandomDelay:
        return expired(t.t_start, t.t_left);
    case kEvMissionTimerExpired:
        return mt_start_ != -1 && expired(mt_start_, mt_left_);
    case kEvGlobalSet: return global(e.value);
    case kEvGlobalCleared: return !global(e.value);
    case kEvLocalSet: return local(e.value);
    case kEvLocalCleared: return !local(e.value);
    case kEvElapsedScenarioTime: return (long)e.value <= frame_ / 15;
    case kEvTechTypeExists: {
        const int n = host.techno_count(e.str);
        if (n < 0) return false;
        // no techno at all -> false; else count >= Value [RE §4].
        return host.techno_count(std::string()) > 0 && n >= e.value;
    }
    case kEvTechTypeDoesntExist: {
        const int n = host.techno_count(e.str);
        if (n < 0) return false;
        return n == 0;
    }
    default:
        break;
    }
    if (e.kind == kEvNone) return false;
    if (e.kind == 5) return e.value >= 0 && host.house_discovered(e.value);   // W4-SQ-11
    if (unimplemented_event(e.kind)) return false;
    if (e.kind != 19 && e.kind != 20 && e.kind != 21 && object_kind(e.kind) && kind != e.kind)
        return false;
    if (e.kind == 1 || e.kind == 24 || e.kind == 25 || e.kind == 26 || e.kind == 59) {
        if (e.value != -1 && !host.house_exists(e.value)) return false;
        if (!ctx.has_object) return false;
        if (e.value != -1 && ctx.object_house_type != e.value) return false;
        persistent = true;
        ev_house_[(std::size_t)(ev_base_[(std::size_t)type] + ev_idx)] = ctx.object_house_type;
        return true;
    }
    // W5-A [RE: FUN_0071DBE0 cases 0x17/0x22/0x35/0x36]:
    if (e.kind == 23) {   // TeamLeavesMap: a team of the event's TeamType left with no members
        if (!host.team_left_map(e.str)) return false;
        persistent = true;
        return true;
    }
    if (e.kind == 34) {   // ComesNearWaypoint: the object within 0x500 leptons (5 cells) of waypoint Value
        int wx = 0, wy = 0;
        if (!ctx.has_object || ctx.obj_x < 0 || !host.waypoint_cell(e.value, wx, wy)) return false;
        const long dx = (long)(ctx.obj_x - wx) * 256, dy = (long)(ctx.obj_y - wy) * 256;
        return dx * dx + dy * dy <= 0x500L * 0x500L;
    }
    if (e.kind == 53) {   // SpyAsHouse: the object poses as house Value
        if (e.value == -1 || !host.house_exists(e.value)) return false;
        return ctx.disguise_house_type >= 0 && ctx.disguise_house_type == e.value;
    }
    if (e.kind == 54) {   // SpyAsInfantry: the object poses as InfantryTypes[Value]
        if (e.value == -1 || ctx.disguise_infantry.empty()) return false;
        return ieq(host.infantry_type_id(e.value), ctx.disguise_infantry);
    }
    if (e.kind == kEvAttackedByHouse) {
        if (ctx.source_house_type < 0) return false;
        if (e.value != ctx.source_house_type) return false;
    }
    if (trig_house >= 0) {
        const int ht = trig_house;
        switch (e.kind) {
        case kEvCreditsExceed:
            if (host.house_credits(ht) < e.value) return false;
            break;
        case kEvDestroyedBuildingsNum:
            if (host.house_destroyed_buildings(ht) < e.value) return false;
            break;
        case kEvDestroyedUnitsNum:
            if (host.house_destroyed_units(ht) < e.value) return false;
            break;
        case kEvNoFactoriesLeft:
            if (host.house_buildings(ht) > 0 && host.house_has_factory(ht)) return false;
            break;
        case kEvBuildBuildingType:
            if (host.house_last_built(ht, 0) != e.value) return false;
            persistent = true;
            break;
        case kEvBuildUnitType:
            if (host.house_last_built(ht, 1) != e.value) return false;
            persistent = true;
            break;
        case kEvBuildInfantryType:
            if (host.house_last_built(ht, 2) != e.value) return false;
            persistent = true;
            break;
        case kEvBuildAircraftType:
            if (host.house_last_built(ht, 3) != e.value) return false;
            persistent = true;
            break;
        case kEvBuildingExists:
            if (host.house_building_type_count(ht, e.value) == 0) return false;
            persistent = true;
            break;
        case kEvCreditsBelow:
            if (e.value < host.house_credits(ht)) return false;
            break;
        case kEvBuildingDoesNotExist:
            if (host.house_building_type_count(ht, e.value) != 0) return false;
            persistent = true;
            break;
        default:
            break;
        }
    }
    // The value-house switch: no such house -> TRUE [RE §4].
    const int vh = e.value;
    if (vh < 0 || !host.house_exists(vh)) return true;
    switch (e.kind) {
    case kEvDestroyedUnitsAll:
        if (host.house_vehicles(vh) > 0 || host.house_infantry(vh) > 0) return false;
        break;
    case kEvDestroyedBuildingsAll:
        if (host.house_buildings(vh) > 0) return false;
        break;
    case kEvDestroyedAll:
        if (host.house_buildings(vh) > 0 || host.house_vehicles(vh) > 0 ||
            host.house_infantry(vh) > 0)
            return false;
        break;
    // W2-CA-06 [RE p9out tev0_0071dbe0.c case 0x37/0x38]: 55 = the house's
    // naval unit count (+0x2EC) <= 0; 56 = land vehicles (units +0x2E8 minus
    // naval +0x2EC) <= 0 and infantry (+0x2F4) <= 0.
    case kEvDestroyedUnitsNaval:
        if (host.house_naval_units(vh) > 0) return false;
        break;
    case kEvDestroyedUnitsLand:
        if (host.house_vehicles(vh) - host.house_naval_units(vh) > 0 || host.house_infantry(vh) > 0)
            return false;
        break;
    case kEvLowPower:
        if (host.house_power_ratio(vh) >= 1.0) return false;
        break;
    case kEvPowerFull:
        if (host.house_power_ratio(vh) < 1.0) return false;
        break;
    default:
        break;
    }
    return true;
}

bool MapTriggerSystem::register_event(Trig& t, int kind, const RaiseContext& ctx,
                                      bool persistent, TriggerHost& host) {
    // FUN_00725760 [RE §3.3].
    if (!t.enabled || t.destroyed) return false;
    const MapTriggerType& ty = d_.triggers[(std::size_t)t.type];
    const int th = trigger_house(t, host);
    bool ok = true;
    for (std::size_t b = 0; b < ty.events.size(); ++b) {
        const std::uint32_t bit = 1u << (b & 31);
        if ((t.bits & bit) == 0) {
            if (!has_occurred(t.type, (int)b, kind, th, ctx, t, persistent, host)) {
                ok = false;
                continue;
            }
        }
        const int eh = ev_house_[(std::size_t)(ev_base_[(std::size_t)t.type] + (int)b)];
        if (eh >= 0) t.house_override = eh;
        const int ek = ty.events[b].kind;
        if (persistent && object_kind(ek) && state_b(ek)) t.bits |= bit;
    }
    if (!ok) return false;
    if (persistent) reset_timers(t);
    return true;
}

void MapTriggerSystem::fire_actions(Tag* tag, int trig_slot, Trig& t, TriggerHost& host) {
    (void)tag; (void)trig_slot;
    // FUN_00725860.
    if (!t.enabled || t.destroyed) return;
    const MapTriggerType& ty = d_.triggers[(std::size_t)t.type];
    ++fired_;
    logf("f=%ld fire %s \"%s\" k=%d", frame_, ty.id.c_str(), ty.name.c_str(), raise_depth_kind_);
    const std::string saved = cur_trigger_;
    cur_trigger_ = ty.id;
    for (const MapAction& a : ty.actions) execute(a, t, host);
    cur_trigger_ = saved;
}

int MapTriggerSystem::force_fire(const std::string& name, TriggerHost& host) {
    int type = -1;
    for (std::size_t i = 0; i < d_.triggers.size() && type < 0; ++i)
        if (ieq(d_.triggers[i].name, name) || ieq(d_.triggers[i].id, name)) type = (int)i;
    if (type < 0) return 0;
    int n = 0;
    for (Tag& tag : tags_)
        for (std::size_t i = tag.trigs.size(); i-- > 0;)
            if (tag.trigs[i].type == type && !tag.trigs[i].destroyed) {
                tag.trigs[i].enabled = true;
                fire_actions(nullptr, -1, tag.trigs[i], host);
                ++n;
            }
    return n;
}

void MapTriggerSystem::execute(const MapAction& a, Trig& t, TriggerHost& host) {
    // FUN_006dce20 [RE §5].
    const int th = trigger_house(t, host);
    const int player = host.player_house_type();
    auto remaining = [&]() -> long {
        if (mt_start_ == -1) return mt_left_;
        const long el = frame_ - mt_start_;
        return mt_left_ > el ? mt_left_ - el : 0;
    };
    auto for_each_instance = [&](int type, auto fn) {
        // TriggerClass array order = creation order (tag order, chain order).
        for (Tag& tag : tags_)
            for (std::size_t i = tag.trigs.size(); i-- > 0;)
                if (tag.trigs[i].type == type) fn(tag.trigs[i]);
    };
    auto effect = [&](int house) {
        TriggerEffect e;
        e.action = a.kind;
        e.house_type = house;
        e.trigger_house_type = th;
        if (a.team >= 0 && a.team < (int)d_.team_ids.size()) e.team = d_.team_ids[(std::size_t)a.team];
        e.waypoint = (a.kind == 17 || a.kind == 18 || (campaign_mode_ && a.kind == 63)) ? a.value : a.waypoint;
        e.value = a.value;
        e.text = a.text;
        for (int k = 0; k < 4; ++k) e.p[k] = a.p[k];
        e.trigger_id = cur_trigger_;
        e.trigger_type = t.type;
        const bool ok = host.apply(e);
        const std::string note = host.take_note();
        logf("  a%d %s team=%s wp=%d v=%d %s -> %s%s%s", a.kind, action_label(a.kind),
             e.team.empty() ? "-" : e.team.c_str(), e.waypoint, e.value,
             e.text.empty() ? "-" : e.text.c_str(), ok ? "ok" : "skipped",
             note.empty() ? "" : " ", note.c_str());
    };
    switch (a.kind) {
    case 0:
        logf("  a0 None");
        return;
    case 1:
        set_outcome(a.value == player ? 1 : 2);
        logf("  a1 Win v=%d -> outcome=%d", a.value, outcome_);
        return;
    case 2:
        set_outcome(a.value != player ? 1 : 2);
        logf("  a2 Lose v=%d -> outcome=%d", a.value, outcome_);
        return;
    case 67:
        set_outcome(1);
        logf("  a67 AnnounceWin -> outcome=%d", outcome_);
        return;
    case 68:
        set_outcome(2);
        logf("  a68 AnnounceLose -> outcome=%d", outcome_);
        return;
    case 3: case 13: case 74: case 75: {
        const int h = find_house(t, a.value, host);
        if (h >= 0 && h < (int)house_flags_.size()) {
            unsigned& f = house_flags_[(std::size_t)h];
            if (a.kind == 3) f |= 1u;
            if (a.kind == 13) f |= 2u;
            if (a.kind == 74) f |= 4u;
            if (a.kind == 75) f &= ~4u;
        }
        logf("  a%d %s house=%d -> %s", a.kind, action_label(a.kind), h, h >= 0 ? "ok" : "no-house");
        return;
    }
    case 6: {
        const int h = find_house(t, a.value, host);
        if (h < 0) { logf("  a6 AllToHunt house=-1 -> no-house"); return; }
        effect(h);
        return;
    }
    case 12: {
        int n = 0;
        for_each_instance(a.trigger, [&](Trig& o) { o.destroyed = true; ++n; });
        logf("  a12 DestroyTrigger %s n=%d",
             a.trigger >= 0 ? d_.triggers[(std::size_t)a.trigger].id.c_str() : "-", n);
        return;
    }
    case 22: {
        logf("  a22 ForceTrigger %s",
             a.trigger >= 0 ? d_.triggers[(std::size_t)a.trigger].id.c_str() : "-");
        if (a.trigger < 0) return;
        if (force_depth_ > 8) return;   // self-forcing guard
        ++force_depth_;
        for_each_instance(a.trigger, [&](Trig& o) { fire_actions(nullptr, -1, o, host); });
        --force_depth_;
        return;
    }
    case 53: case 54: {
        int n = 0;
        for_each_instance(a.trigger, [&](Trig& o) {
            if (a.kind == 54) { o.enabled = false; ++n; return; }
            if (d_.triggers[(std::size_t)a.trigger].difficulty[difficulty_]) {
                o.enabled = true;
                reset_timers(o);
                ++n;
            }
        });
        logf("  a%d %s %s n=%d", a.kind, action_label(a.kind),
             a.trigger >= 0 ? d_.triggers[(std::size_t)a.trigger].id.c_str() : "-", n);
        return;
    }
    case 23:
        if (mt_start_ == -1) mt_start_ = frame_;
        break;
    case 24:
        if (mt_start_ != -1) { mt_left_ = remaining(); mt_start_ = -1; }
        break;
    case 25: {
        const long r = remaining();
        mt_start_ = frame_;
        mt_left_ = r + (long)a.value * 15;
        break;
    }
    case 26: {
        const long r = remaining();
        mt_start_ = frame_;
        mt_left_ = (r <= (long)a.value * 15) ? 0 : r - (long)a.value * 15;
        break;
    }
    case 27:
        mt_start_ = frame_;
        mt_left_ = (long)a.value * 15;
        break;
    case 28: set_global(a.value, true); break;
    case 29: set_global(a.value, false); break;
    case 56: set_local(a.value, true); break;
    case 57: set_local(a.value, false); break;
    case 4: case 5: case 7: case 80: case 107:
        if (a.team < 0 || ((a.kind == 80 || a.kind == 107) && a.waypoint == -1)) {
            logf("  a%d %s -> no-team/waypoint", a.kind, action_label(a.kind));
            return;
        }
        effect(-1);
        return;
    case 16: case 17:
        effect(player);
        return;
    case 125:
        if (th < 0) { logf("  a125 CreateBuilding -> no-house"); return; }
        effect(th);
        return;
    case 14: case 36: case 37: case 38:
        if (!campaign_mode_) goto unhandled;
        effect(find_house(t, a.value, host));
        return;
    case 81: case 82: case 83:   // W4-SQ-05 Wakeup* (object / all sleepers / all harmless)
    case 18:                     // W4-SQ-11 RevealZone (the waypoint cell's zone)
        effect(a.kind == 18 ? player : th);
        break;
    case 32: case 63:
    case 60: case 111:   // W2-CA-13: SellBuilding / EvictOccupiers on the tagged objects
        if (!campaign_mode_) goto unhandled;
        effect(th);
        return;
    // gF G-75 [RE: docs/re/mcv-deploy.md §8, Execute jump table 0x6DF35C]:
    // 108 CreateCrate = PlaceCrate(waypoint cell, Value) (case @0x6DEC0B ->
    // FUN_0056B9F0); 101 ReshroudMapAtWaypoint (@0x6DDE27 -> FUN_006E0FE0);
    // 9 FireSale on house Value (@0x6DE046: house state +0x250 = 4).
    case 108: case 101:
        if (!campaign_mode_) goto unhandled;
        effect(a.kind == 101 ? player : th);
        return;
    case 9:
        if (!campaign_mode_) goto unhandled;
        effect(find_house(t, a.value, host));
        return;
    // W3-FX [RE: Execute 0x2a -> FUN_006e1900 DoExplosionAt (WeaponTypes[Value]
    // anim + DamageArea at the waypoint), 0x5a -> FUN_00539e70 LightningStrikeAt
    // (a storm cloud + bolt at the waypoint), 0x66 -> LightningStorm::Start at
    // the waypoint for the trigger house unless a storm is already active]:
    // campaign mode only (they damage), resolved by the host.
    case 42: case 90: case 102:
        if (!campaign_mode_) goto unhandled;
        effect(th);
        return;
    // W5-A R-029/R-021 [RE: Execute FUN_006DCE20, table 0x6DF35C[kind-1]]:
    // 35 PreferredTarget: trigger house +0x54EC = Value (the AI superweapon
    //    quarry, FUN_00509710 -> FUN_0050CF90 when != 1);
    // 40 ResizeView: Map LocalSize = (P2, P3, P4, P5) (FUN_006E1750);
    // 44 IonStorm (YR: LightningStorm::Start(Value duration) unless active);
    // 51 Reshroud: the whole map back under shroud (FUN_005777C0);
    // 61 / 62 TurnOff / TurnOn: every building carrying the trigger's tag
    //    loses / regains power (FUN_00452350 / FUN_00452250);
    // 77 / 78: trigger house +0x5660 / +0x5664 = Value (AI air / infantry ratio).
    case 35: case 40: case 44: case 51: case 61: case 62: case 77: case 78:
        if (!campaign_mode_) goto unhandled;
        effect(a.kind == 51 ? player : th);
        return;
    // 70 DestroyTag [RE: case @0x6DE604]: every live tag whose type is P1
    // is destroyed (vtbl +0x20).
    case 70: {
        int n = 0;
        for (std::size_t i = 0; i < tags_.size(); ++i)
            if (tags_[i].type == a.tag && !tags_[i].destroyed) { tags_[i].destroyed = true; ++n; }
        logf("  a70 DestroyTag n=%d", n);
        return;
    }
    default:
    unhandled:
        if (campaign_mode_) {
            switch (a.kind) {
            case 41: case 46: case 47: case 48: case 55: case 88: case 104: case 112: case 113: case 114:
            case 115: case 131: {
                VisualRequest v;
                v.action = a.kind;
                v.value = a.value;
                v.waypoint = a.waypoint;
                v.text = a.text;
                if (a.team >= 0 && a.team < (int)d_.team_ids.size()) v.team = d_.team_ids[(std::size_t)a.team];
                if (a.kind == 113) v.house_type = find_house(t, a.value, host);
                host.present(v);
                break;
            }
            default:
                break;
            }
        }
        if (a.kind == 103) timer_text_ = a.text;   // W6-05-04 TimerText
        if (presentation_action(a.kind)) {
            logf("  a%d %s v=%d wp=%d %s (recorded)", a.kind, action_label(a.kind), a.value,
                 a.waypoint, a.text.empty() ? "-" : a.text.c_str());
        } else {
            logf("  a%d unimplemented v=%d", a.kind, a.value);
        }
        return;
    }
    // Timer / variable actions fall through to here.
    if (a.kind >= 23 && a.kind <= 27)
        logf("  a%d %s v=%d -> timer start=%ld left=%ld", a.kind, action_label(a.kind), a.value,
             mt_start_, mt_left_);
    else
        logf("  a%d %s idx=%d", a.kind, action_label(a.kind), a.value);
}

bool MapTriggerSystem::raise(int tag_idx, int kind, const RaiseContext& ctx, TriggerHost& host) {
    // FUN_006e4910 [RE §3.4].
    if (tag_idx < 0 || tag_idx >= (int)tags_.size()) return false;
    {
        Tag& tag = tags_[(std::size_t)tag_idx];
        if (tag.destroyed || tag.executing) return false;
        tag.executing = true;
    }
    const int persist = d_.tags[(std::size_t)tags_[(std::size_t)tag_idx].type].persistence;
    const int saved_kind = raise_depth_kind_;
    raise_depth_kind_ = kind;
    bool any = false, del = false;
    for (std::size_t s = 0; s < tags_[(std::size_t)tag_idx].trigs.size(); ++s) {
        Trig& t = tags_[(std::size_t)tag_idx].trigs[s];
        if (!register_event(t, kind, ctx, persist == kPersistPersistent, host)) continue;
        bool fire = false;
        if (persist == kPersistVolatile) {
            fire = true;
        } else if (persist == kPersistSemi) {
            fire = tags_[(std::size_t)tag_idx].instance_count == 1;
            if (!fire && campaign_mode_) {
                // RE §3.4: not the last object -> this object just detaches.
                Tag& tg = tags_[(std::size_t)tag_idx];
                if (tg.instance_count > 0) --tg.instance_count;
                semi_detached_ = true;
                break;
            }
        } else if (persist == kPersistPersistent) {
            fire = true;
        }
        if (!fire) continue;
        fire_actions(&tags_[(std::size_t)tag_idx], (int)s, t, host);
        any = true;
        if (kind == kEvGlobalSet || kind == kEvGlobalCleared || kind == kEvLocalSet ||
            kind == kEvLocalCleared)
            ++variable_fired_;
        if (persist != kPersistPersistent) {
            t.destroyed = true;
            del = true;
        }
    }
    raise_depth_kind_ = saved_kind;
    Tag& tag = tags_[(std::size_t)tag_idx];
    tag.executing = false;
    if (del) tag.pending_delete = true;
    return any;
}

bool MapTriggerSystem::raise_tag(int tag_type, int kind, const RaiseContext& ctx,
                                 TriggerHost& host) {
    const int idx = get_instance(tag_type);
    return raise(idx, kind, ctx, host);
}

bool MapTriggerSystem::raise_cell(int x, int y, int kind, const RaiseContext& ctx,
                                  TriggerHost& host) {
    for (const MapCellTag& c : d_.cell_tags)
        if (c.x == x && c.y == y) {
            const int idx = (c.tag >= 0 && c.tag < (int)tag_of_type_.size())
                                ? tag_of_type_[(std::size_t)c.tag] : -1;
            return raise(idx, kind, ctx, host);
        }
    return false;
}

bool MapTriggerSystem::tag_waits_on(int tag_type, int kind) const {
    if (tag_type < 0 || tag_type >= (int)d_.tags.size()) return false;
    std::vector<int> seen;
    for (int t = d_.tags[(std::size_t)tag_type].first_trigger; t >= 0 && t < (int)d_.triggers.size();
         t = d_.triggers[(std::size_t)t].linked) {
        if (std::find(seen.begin(), seen.end(), t) != seen.end()) break;   // a cyclic chain
        seen.push_back(t);
        for (const MapEvent& e : d_.triggers[(std::size_t)t].events)
            if (e.kind == kind) return true;
    }
    return false;
}

bool MapTriggerSystem::cell_waits_on(int x, int y, int kind) const {
    for (const MapCellTag& c : d_.cell_tags)
        if (c.x == x && c.y == y) return tag_waits_on(c.tag, kind);
    return false;
}

bool MapTriggerSystem::raise_logic(int kind, const RaiseContext& ctx, TriggerHost& host) {
    // Only the tags whose chain waits on `kind` [RE-FALLBACK: the original's
    // raise site for 23 was not traced; raising it on every logic tag would
    // also re-evaluate unrelated timer / variable triggers].
    bool any = false;
    const std::vector<int> list = logic_list_;   // a raise may destroy tags
    for (int ti : list) {
        if (ti < 0 || ti >= (int)tags_.size()) continue;
        bool wants = false;
        for (const Trig& t : tags_[(std::size_t)ti].trigs)
            for (const MapEvent& e : d_.triggers[(std::size_t)t.type].events)
                if (e.kind == kind) wants = true;
        if (wants && raise(ti, kind, ctx, host)) any = true;
    }
    return any;
}

bool MapTriggerSystem::attach_tag(int tag_type) {
    if (tag_type < 0 || tag_type >= (int)d_.tags.size()) return false;
    const int idx = get_instance(tag_type);
    if (idx < 0) return false;
    ++tags_[(std::size_t)idx].instance_count;
    return true;
}

void MapTriggerSystem::detach_tag(int tag_type) {
    if (tag_type < 0 || tag_type >= (int)tag_of_type_.size()) return;
    const int idx = tag_of_type_[(std::size_t)tag_type];
    if (idx < 0) return;
    if (tags_[(std::size_t)idx].instance_count > 0) --tags_[(std::size_t)idx].instance_count;
}

bool MapTriggerSystem::tag_alive(int tag_type) const {
    if (tag_type < 0 || tag_type >= (int)tag_of_type_.size()) return false;
    const int idx = tag_of_type_[(std::size_t)tag_type];
    return idx >= 0 && !tags_[(std::size_t)idx].destroyed && !tags_[(std::size_t)idx].pending_delete;
}

MapTriggerSystem::ObjectRaise MapTriggerSystem::raise_object(int tag_type, int kind,
                                                             const RaiseContext& ctx,
                                                             TriggerHost& host) {
    ObjectRaise r;
    if (!tag_alive(tag_type)) { r.tag_gone = true; return r; }
    semi_detached_ = false;
    r.fired = raise(tag_of_type_[(std::size_t)tag_type], kind, ctx, host);
    r.detached = semi_detached_;
    semi_detached_ = false;
    if (!tag_alive(tag_type)) r.tag_gone = true;
    return r;
}

bool MapTriggerSystem::tag_has_trigger(int tag_type, int trigger_type) const {
    if (tag_type < 0 || tag_type >= (int)d_.tags.size()) return false;
    std::set<int> seen;
    for (int tt = d_.tags[(std::size_t)tag_type].first_trigger; tt >= 0 && !seen.count(tt);
         tt = d_.triggers[(std::size_t)tt].linked) {
        if (tt == trigger_type) return true;
        seen.insert(tt);
    }
    return false;
}

void MapTriggerSystem::tick(TriggerHost& host) {
    const RaiseContext none;
    // LogicClass::AI (FUN_0055ac40) [RE §3.2].
    for (std::size_t li = 0; li < logic_list_.size(); ++li) {
        const int tag = logic_list_[li];
        const int before = fired_;
        bool fired = false;
        if (vars_changed_)
            fired = raise(tag, kEvGlobalSet, none, host) || raise(tag, kEvGlobalCleared, none, host) ||
                    raise(tag, kEvLocalSet, none, host) || raise(tag, kEvLocalCleared, none, host);
        if (!fired) fired = raise(tag, kEvElapsedTime, none, host);
        if (!fired) fired = raise(tag, kEvRandomDelay, none, host);
        if (!fired && mt_start_ != -1) {
            const long el = frame_ - mt_start_;
            if (mt_left_ <= el || mt_left_ == 0) raise(tag, kEvMissionTimerExpired, none, host);
        }
        logic_fired_ += fired_ - before;
    }
    // Expired mission timer stops; flags clear (LAB_0055ae68).
    if (mt_start_ != -1) {
        const long el = frame_ - mt_start_;
        if (mt_left_ <= el) {
            mt_left_ = 0;
            mt_start_ = -1;
            logf("f=%ld mission timer expired", frame_);
        }
    }
    vars_changed_ = false;
    // HouseClass::AI every 8th frame, each list last->first, kind 8.
    if (frame_ % 8 == 0) {
        for (int ht : host.house_instances()) {
            if (ht < 0 || ht >= (int)house_lists_.size()) continue;
            const std::vector<int> list = house_lists_[(std::size_t)ht];
            for (std::size_t i = list.size(); i-- > 0;) {
                const int before = fired_;
                raise(list[i], kEvAnyEvent, none, host);
                house_fired_ += fired_ - before;
            }
        }
    }
    // Deferred deletion (the pending-delete list, FUN_00724f10).
    for (std::size_t i = 0; i < tags_.size(); ++i) {
        Tag& tag = tags_[i];
        if (!tag.pending_delete || tag.destroyed) continue;
        tag.destroyed = true;
        for (Trig& t : tag.trigs) t.destroyed = true;
        const int idx = (int)i;
        auto drop = [idx](std::vector<int>& v) { v.erase(std::remove(v.begin(), v.end(), idx), v.end()); };
        drop(map_list_);
        drop(logic_list_);
        for (std::vector<int>& hl : house_lists_) drop(hl);
        if (tag.type >= 0) tag_of_type_[(std::size_t)tag.type] = -1;
    }
    ++frame_;
}

bool MapTriggerSystem::trigger_enabled(int trigger_type) const {
    for (const Tag& tag : tags_)
        for (const Trig& t : tag.trigs)
            if (t.type == trigger_type && !t.destroyed) return t.enabled;
    return false;
}
bool MapTriggerSystem::trigger_destroyed(int trigger_type) const {
    for (const Tag& tag : tags_)
        for (const Trig& t : tag.trigs)
            if (t.type == trigger_type && !t.destroyed) return false;
    return true;
}

std::uint64_t MapTriggerSystem::state_hash() const {
    std::uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xff;
            h *= 0x100000001b3ull;
        }
    };
    mix((std::uint64_t)frame_);
    for (std::uint8_t g : globals_) mix(g);
    for (std::uint8_t l : locals_) mix(l);
    mix((std::uint64_t)mt_start_);
    mix((std::uint64_t)mt_left_);
    mix((std::uint64_t)outcome_);
    for (unsigned f : house_flags_) mix(f);
    for (const Tag& tag : tags_) {
        mix((std::uint64_t)tag.type);
        mix(tag.destroyed ? 1 : 0);
        for (const Trig& t : tag.trigs) {
            mix((std::uint64_t)t.type);
            mix(t.enabled ? 1 : 0);
            mix(t.destroyed ? 1 : 0);
            mix(t.bits);
            mix((std::uint64_t)t.t_start);
            mix((std::uint64_t)t.t_left);
            mix((std::uint64_t)t.house_override);
        }
    }
    for (const std::string& s : log_)
        for (char c : s) { h ^= (unsigned char)c; h *= 0x100000001b3ull; }
    return h;
}

}  // namespace ra2engine
