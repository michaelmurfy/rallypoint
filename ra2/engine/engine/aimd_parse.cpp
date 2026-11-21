#include "aimd_parse.hpp"
#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace ra2engine {
namespace {

std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
// RA2 boolean values: yes/no (also true/1 tolerated) [RE: ai-team.md §1].
bool truthy(const std::string& v) {
    if (v.empty()) return false;
    const char c = (char)std::toupper((unsigned char)v[0]);
    return c == 'Y' || c == 'T' || c == '1';
}
bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit((unsigned char)c)) return false;
    return true;
}
// "count,UNITID" / "action,argument": split on the FIRST comma.
bool split2(const std::string& v, std::string& a, std::string& b) {
    const std::size_t comma = v.find(',');
    if (comma == std::string::npos) return false;
    a = v.substr(0, comma);
    b = v.substr(comma + 1);
    return true;
}
std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}
// Split the WHOLE value on every comma (trigger lines are pure comma fields;
// stock Name fields carry no embedded comma — the FormatForSaving format
// string writes pure comma fields [RE-GATE APPLIED: ai-trigger.md §1 —
// CONFIRMED]).
std::vector<std::string> split_commas(const std::string& v) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = v.find(',', start);
        if (comma == std::string::npos) { out.push_back(trim(v.substr(start))); break; }
        out.push_back(trim(v.substr(start, comma - start)));
        start = comma + 1;
    }
    return out;
}
int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// Little-endian dword from 8 hex chars (byte pairs low-to-high).
bool hex_dword(const std::string& s, std::size_t off, int& out) {
    if (off + 8 > s.size()) return false;
    unsigned v = 0;
    for (int b = 0; b < 4; ++b) {
        const int hi = hex_nibble(s[off + b * 2]);
        const int lo = hex_nibble(s[off + b * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        v |= (unsigned)((hi << 4) | lo) << (8 * b);
    }
    out = (int)v;
    return true;
}

}  // namespace

int waypoint_from_letter(const std::string& s) {
    // Bijective base-26: A=0..Z=25, AA=26 [RE-GATE APPLIED: ai-team.md §1].
    if (s.empty()) return -1;
    long v = 0;
    for (char c : s) {
        const char u = (char)std::toupper((unsigned char)c);
        if (u < 'A' || u > 'Z') return -1;
        v = v * 26 + (u - 'A' + 1);
    }
    return (int)(v - 1);
}

AimdData parse_aimd(const ra2assets::Ini& ini,
                    const std::function<int(const std::string&)>& unit_index) {
    AimdData d;
    std::unordered_map<std::string, int> tf_ix, sc_ix;  // entity id -> table index

    // [TaskForces] index section: N=id, file order [RE: ai-team.md §1].
    for (const auto& kv : ini.section("TaskForces")) {
        const std::string id = upper(kv.second);
        if (id.empty() || !ini.has(id) || tf_ix.count(id)) continue;  // dangling/dup
        TaskForce tf;
        tf.id = id;
        bool ok = true;
        for (const auto& e : ini.section(id)) {
            if (e.first == "NAME") tf.name = e.second;
            else if (e.first == "GROUP") tf.group = std::atoi(e.second.c_str());
            else if (all_digits(e.first)) {   // N=count,UNITID
                std::string cnt, uid;
                if (!split2(e.second, cnt, uid)) { ok = false; break; }
                TaskForceMember m;
                m.count = std::atoi(cnt.c_str());
                m.unit = unit_index(upper(uid));
                if (m.count <= 0 || m.unit < 0) { ok = false; break; }  // art-guard-drop
                tf.members.push_back(m);
            }
        }
        if (!ok || tf.members.empty()) continue;  // whole TaskForce dropped
        tf_ix[id] = (int)d.taskforces.size();
        d.taskforces.push_back(std::move(tf));
    }

    // [ScriptTypes] index section: N=id [RE: ai-team.md §1/§3]. Every action is
    // KEPT even if this engine interprets it inert — the inert split is the
    // INTERPRETER's job (ai_team), not the parser's.
    for (const auto& kv : ini.section("ScriptTypes")) {
        const std::string id = upper(kv.second);
        if (id.empty() || !ini.has(id) || sc_ix.count(id)) continue;
        ScriptType sc;
        sc.id = id;
        for (const auto& e : ini.section(id)) {
            if (e.first == "NAME") sc.name = e.second;
            else if (all_digits(e.first)) {   // N=action,argument
                std::string act, arg;
                if (!split2(e.second, act, arg)) continue;  // malformed line skipped
                ScriptAction a;
                a.action = std::atoi(act.c_str());
                a.argument = std::atoi(arg.c_str());
                sc.actions.push_back(a);
            }
        }
        if (sc.actions.empty()) continue;
        sc_ix[id] = (int)d.scripts.size();
        d.scripts.push_back(std::move(sc));
    }

    // [TeamTypes] index section: N=id [RE-GATE APPLIED: ai-team.md §1 — the
    // named-key set]. A team whose TaskForce/Script was dropped is dropped too.
    for (const auto& kv : ini.section("TeamTypes")) {
        const std::string id = upper(kv.second);
        if (id.empty() || !ini.has(id)) continue;
        TeamType t;
        t.id = id;
        t.name = ini.get(id, "Name");
        t.house = ini.get(id, "House");
        const auto tf = tf_ix.find(upper(ini.get(id, "TaskForce")));
        const auto sc = sc_ix.find(upper(ini.get(id, "Script")));
        if (tf == tf_ix.end() || sc == sc_ix.end()) continue;  // art-guard-drop
        t.taskforce = tf->second;
        t.script = sc->second;
        t.priority = ini.get_int(id, "Priority", 0);
        t.max = ini.get_int(id, "Max", 0);
        t.full = truthy(ini.get(id, "Full"));
        t.autocreate = truthy(ini.get(id, "Autocreate"));
        t.aggressive = truthy(ini.get(id, "Aggressive"));   // [RE: ai-team.md §1]
        t.reinforce = truthy(ini.get(id, "Reinforce"));
        t.suicide = truthy(ini.get(id, "Suicide"));
        t.tech_level = ini.get_int(id, "TechLevel", 0);
        t.is_base_defense = truthy(ini.get(id, "IsBaseDefense"));  // DEFENSIVE marker
        t.waypoint = waypoint_from_letter(ini.get(id, "Waypoint"));
        d.teams.push_back(std::move(t));
    }
    return d;
}

bool decode_comparator(const std::string& hex, TriggerComparator& out) {
    // [RE-GATE APPLIED: ai-trigger.md §2 — CONFIRMED]: the blob is
    // AITriggerConditionComparator Conditions[4] = 32 bytes = 64 hex chars =
    // 8 LE dwords, VALUE-FIRST: dword [0] (chars 0-7) = the count/value N
    // (+0xe4), dword [1] (chars 8-15) = the operator 0..5 (+0xe8); dwords
    // [2..7] (Conditions[1..3]) are reserved — always 0 in all 165 stock
    // lines — validated as hex, IGNORED for meaning.
    if (hex.size() != 64) return false;
    if (!hex_dword(hex, 0, out.value) || !hex_dword(hex, 8, out.op))
        return false;
    int reserved = 0;                        // slots [2..7]: validity only
    for (std::size_t dw = 2; dw < 8; ++dw)
        if (!hex_dword(hex, dw * 8, reserved)) return false;
    return true;
}

void parse_ai_triggers(const ra2assets::Ini& ini, AimdData& d,
                       const std::function<int(const std::string&)>& unit_index,
                       const std::function<int(const std::string&)>& building_index) {
    std::unordered_map<std::string, int> team_ix;  // TeamType id -> teams index
    for (int i = 0; i < (int)d.teams.size(); ++i) team_ix[d.teams[i].id] = i;
    // FLAT list: every key is one full trigger, file order [RE-GATE APPLIED:
    // ai-trigger.md §1 — the 18-field comma order is CONFIRMED].
    for (const auto& kv : ini.section("AITriggerTypes")) {
        const std::vector<std::string> f = split_commas(kv.second);
        if (f.size() < 18) continue;                 // malformed line skipped
        AITriggerType t;
        t.id = kv.first;                             // key is canonical UPPER
        t.name = f[0];
        const auto t1 = team_ix.find(upper(f[1]));
        if (t1 == team_ix.end()) continue;           // team1 dropped/missing ->
        t.team1 = t1->second;                        // drop the trigger WHOLE
        t.owner_house = f[2];
        t.tech_level = std::atoi(f[3].c_str());
        t.condition_type = std::atoi(f[4].c_str());
        const std::string obj = upper(f[5]);
        if (!obj.empty() && obj != "<NONE>") {
            const int u = unit_index(obj);
            const int b = u < 0 ? building_index(obj) : -1;
            if (u >= 0)      t.condition_object = TriggerObject{TechnoKind::Unit, u};
            else if (b >= 0) t.condition_object = TriggerObject{TechnoKind::Building, b};
            else continue;                           // art-guard-drop: object
        }                                            // unresolvable -> whole
        if (!decode_comparator(f[6], t.comparator)) continue;  // malformed blob
        t.starting_weight = std::atof(f[7].c_str());
        t.min_weight = std::atof(f[8].c_str());
        t.max_weight = std::atof(f[9].c_str());
        t.is_for_skirmish = truthy(f[10]);
        // f[11] is a literal 0, recorded-only [RE-GATE APPLIED: ai-trigger.md
        // §1 — CONFIRMED unused]
        t.side = std::atoi(f[12].c_str());   // SideIndex 0=any/1=Allied/
                                             // 2=Soviet/3=Yuri [RE: §5]
        t.base_defense = truthy(f[13]);
        const auto t2 = team_ix.find(upper(f[14]));
        t.team2 = t2 == team_ix.end() ? -1 : t2->second;  // Team2 OPTIONAL
                                             // ("<none>" on 116/165) — ALSO
                                             // created when set [RE: §3]
        t.enabled[0] = truthy(f[15]);        // Easy,Normal,Hard on-disk order;
        t.enabled[1] = truthy(f[16]);        // the consult inverts at filter
        t.enabled[2] = truthy(f[17]);        // time [RE: §5]
        d.triggers.push_back(std::move(t));
    }
    // [AITriggerTypesEnable] — a SEPARATE per-trigger on/off section of
    // <trigger-id>=<bool> lines; sets is_enabled on the matching trigger.
    // Unlisted ids stay ENABLED (the struct default); ids naming dropped or
    // unknown triggers are ignored (never a crash). It gates individual
    // triggers, NOT the whole subsystem [RE-GATE APPLIED: ai-trigger.md §5 —
    // CONFIRMED: FUN_0041f300 applies the list to IsEnabled(+0xa4)].
    for (const auto& kv : ini.section("AITriggerTypesEnable")) {
        for (AITriggerType& t : d.triggers)
            if (t.id == kv.first) { t.is_enabled = truthy(kv.second); break; }
    }
}

}  // namespace ra2engine
