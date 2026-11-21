// P10 Campaign missions [RE: docs/re/campaign.md]. See campaign.hpp.
#include "campaign.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "map_trigger.hpp"   // map_waypoint_letters

namespace ra2engine {

namespace {

std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
bool ieq(const std::string& a, const std::string& b) { return upper(a) == upper(b); }

// Values may carry a trailing "; comment" (rules [Countries] lines do).
std::string strip(std::string v) {
    const std::size_t sc = v.find(';');
    if (sc != std::string::npos) v.erase(sc);
    while (!v.empty() && std::isspace((unsigned char)v.back())) v.pop_back();
    std::size_t i = 0;
    while (i < v.size() && std::isspace((unsigned char)v[i])) ++i;
    return v.substr(i);
}

bool yes(const std::string& v) {
    const std::string u = upper(strip(v));
    return !u.empty() && (u[0] == 'Y' || u[0] == 'T' || u[0] == '1');
}

std::vector<std::string> split_csv(const std::string& v) {
    std::vector<std::string> f;
    std::string cur;
    for (char c : v) {
        if (c == ',') { f.push_back(strip(cur)); cur.clear(); }
        else cur += c;
    }
    f.push_back(strip(cur));
    return f;
}

bool is_number(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!std::isdigit((unsigned char)c)) return false;
    return true;
}

std::string movie(const std::string& v) {
    const std::string s = strip(v);
    return (s.empty() || ieq(s, "<none>")) ? std::string() : s;
}

}  // namespace

// ---------------------------------------------------------------------------
CampaignTables CampaignTables::parse(const ra2assets::Ini& battle, const ra2assets::Ini& mission,
                                     const ra2assets::Ini& mapsel) {
    CampaignTables t;
    for (const auto& kv : battle.section("Battles")) {
        const std::string id = strip(kv.second);
        if (id.empty() || !battle.has(id)) continue;
        BattleEntry b;
        b.id = id;
        b.scenario = strip(battle.get(id, "Scenario", ""));
        b.description = strip(battle.get(id, "Description", ""));
        b.final_movie = strip(battle.get(id, "FinalMovie", ""));
        b.cd = battle.get_int(id, "CD", -1);
        b.debug_only = yes(battle.get(id, "DebugOnly", "no"));
        t.battles_.push_back(b);
    }
    t.mission_ = mission;
    t.mapsel_ = mapsel;
    // Every section with a Scenario= key is a stage (FUN_005cf300).
    for (const std::string& sec : mapsel.section_names()) {
        bool has_scen = false;
        MapSelStage st;
        st.name = sec;
        for (const auto& kv : mapsel.section(sec)) {
            if (kv.first == "SCENARIO") { has_scen = true; st.scenario = strip(kv.second); }
            else if (is_number(kv.first)) {
                const std::string n = strip(kv.second);
                if (!n.empty()) st.next.push_back(n);
            }
        }
        if (has_scen) t.stages_.push_back(st);
    }
    return t;
}

const BattleEntry* CampaignTables::battle(const std::string& id) const {
    for (const BattleEntry& b : battles_) if (ieq(b.id, id)) return &b;
    return nullptr;
}

MissionInfo CampaignTables::mission(const std::string& map_name) const {
    MissionInfo m;
    const std::string sec = upper(map_name);
    if (!mission_.has(sec)) return m;
    m.found = true;
    m.briefing = strip(mission_.get(sec, "Briefing", ""));
    m.ui_name = strip(mission_.get(sec, "UIName", ""));
    m.ls_load_message = strip(mission_.get(sec, "LSLoadMessage", ""));
    m.ls_load_briefing = strip(mission_.get(sec, "LSLoadBriefing", ""));
    m.ls640_bkgd = strip(mission_.get(sec, "LS640BkgdName", ""));
    m.ls800_bkgd = strip(mission_.get(sec, "LS800BkgdName", ""));
    m.ls800_brief_x = mission_.get_int(sec, "LS800BriefLocX", 20);
    m.ls800_brief_y = mission_.get_int(sec, "LS800BriefLocY", 20);
    return m;
}

std::vector<std::string> CampaignTables::side_stages(const std::string& side) const {
    std::vector<std::string> out;
    for (const auto& kv : mapsel_.section(side))
        if (is_number(kv.first)) {
            const std::string n = strip(kv.second);
            if (!n.empty()) out.push_back(n);
        }
    return out;
}

const MapSelStage* CampaignTables::stage(const std::string& name) const {
    for (const MapSelStage& s : stages_) if (ieq(s.name, name)) return &s;
    return nullptr;
}

std::string CampaignTables::next_scenario(const std::string& side,
                                          const std::string& current_map) const {
    // FUN_005ce910 loads the side's stage list; FUN_005cf180 finds the current
    // stage by scenario-name compare; the stage's numbered keys are the choices.
    for (const std::string& sn : side_stages(side)) {
        const MapSelStage* st = stage(sn);
        if (!st || !ieq(st->scenario, current_map)) continue;
        for (const std::string& nx : st->next) {
            const MapSelStage* ns = stage(nx);
            if (ns && !ns->scenario.empty()) return ns->scenario;
        }
        return std::string();
    }
    return std::string();
}

// ---------------------------------------------------------------------------
ScenarioBasic parse_scenario_basic(const ra2assets::Ini& map) {
    ScenarioBasic b;
    b.player = strip(map.get("Basic", "Player", "Americans"));
    b.briefing = strip(map.get("Basic", "Briefing", ""));
    b.intro = movie(map.get("Basic", "Intro", ""));
    b.brief = movie(map.get("Basic", "Brief", ""));
    b.win = movie(map.get("Basic", "Win", ""));
    b.lose = movie(map.get("Basic", "Lose", ""));
    b.action = movie(map.get("Basic", "Action", ""));
    b.post_score = movie(map.get("Basic", "PostScore", ""));
    b.pre_map_select = movie(map.get("Basic", "PreMapSelect", ""));
    const std::string th = strip(map.get("Basic", "Theme", ""));
    b.theme = ieq(th, "No theme") ? std::string() : th;
    b.next_scenario = strip(map.get("Basic", "NextScenario", ""));
    b.end_of_game = yes(map.get("Basic", "EndOfGame", "no"));
    b.skip_score = yes(map.get("Basic", "SkipScore", "no"));
    b.one_time_only = yes(map.get("Basic", "OneTimeOnly", "no"));
    b.skip_map_select = yes(map.get("Basic", "SkipMapSelect", "no"));
    b.carry_over_money = std::atof(strip(map.get("Basic", "CarryOverMoney", "0")).c_str());
    b.home_cell = map.get_int("Basic", "HomeCell", 98);
    b.alt_home_cell = map.get_int("Basic", "AltHomeCell", 99);
    return b;
}

namespace {
bool parse4(const std::string& v, int out[4]) {
    int n = 0;
    std::size_t p = 0;
    while (n < 4 && p <= v.size()) {
        const std::size_t c = v.find(',', p);
        const std::string tok = strip(v.substr(p, c == std::string::npos ? std::string::npos : c - p));
        if (tok.empty()) return false;
        out[n++] = std::atoi(tok.c_str());
        if (c == std::string::npos) break;
        p = c + 1;
    }
    return n == 4;
}
}  // namespace

MapLocalSize parse_map_local_size(const ra2assets::Ini& map) {
    MapLocalSize m;
    int sz[4], ls[4];
    if (!parse4(map.get("Map", "Size", ""), sz) || !parse4(map.get("Map", "LocalSize", ""), ls))
        return m;
    if (sz[2] <= 0 || sz[3] <= 0 || ls[2] <= 0 || ls[3] <= 0) return m;
    m.valid = true;
    m.size_w = sz[2];
    m.size_h = sz[3];
    m.x = ls[0];
    m.y = ls[1];
    m.w = ls[2];
    m.h = ls[3];
    return m;
}

bool within_usable_area(const MapLocalSize& m, int cx, int cy, int level) {
    if (!m.valid) return true;
    const int W = m.size_w;
    const int s = cx + cy, d = cx - cy;
    return W + 2 * m.y + level < s && s <= W + 2 + 2 * (m.h + m.y) + level &&
           d < 2 * (m.w + m.x) - W && -d < W - 2 * m.x;
}

// ---------------------------------------------------------------------------
std::vector<MapObjectRec> parse_map_objects(const ra2assets::Ini& map) {
    std::vector<MapObjectRec> out;
    const bool new_fmt = map.get_int("Basic", "NewINIFormat", 4) >= 4;
    auto tag_of = [](const std::string& s) { return ieq(s, "None") ? std::string() : s; };
    auto head = [&](MapObjectRec& r, const std::vector<std::string>& f, std::size_t& i) -> bool {
        // OWNER,TYPE,HEALTH,X,Y (NewINIFormat >= 4) or OWNER,TYPE,HEALTH,CELL.
        if (f.size() < (new_fmt ? 5u : 4u)) return false;
        r.owner = f[0];
        r.type = f[1];
        r.health = std::atoi(f[2].c_str());
        if (new_fmt) {
            r.x = std::atoi(f[3].c_str());
            r.y = std::atoi(f[4].c_str());
            i = 5;
        } else {
            const int v = std::atoi(f[3].c_str());
            r.x = v % 128;
            r.y = v / 128;
            i = 4;
        }
        return true;
    };
    auto at = [](const std::vector<std::string>& f, std::size_t i) -> std::string {
        return i < f.size() ? f[i] : std::string();
    };
    for (const auto& kv : map.section("Structures")) {
        const std::vector<std::string> f = split_csv(kv.second);
        MapObjectRec r;
        r.kind = 0;
        r.key = kv.first;
        std::size_t i = 0;
        if (!head(r, f, i)) continue;
        r.facing = std::atoi(at(f, i).c_str());   // Facing
        r.tag = tag_of(at(f, i + 1));             // Tag
        // fv7c: ...,Tag,AISellable,AIRebuildable,Powered,Upgrades,SpotLight,
        // Upgrade1,Upgrade2,Upgrade3,AIRepairable,Nominal
        r.ai_sellable = std::atoi(at(f, i + 2).c_str()) != 0;
        r.ai_rebuildable = std::atoi(at(f, i + 3).c_str()) != 0;
        r.ai_repairable = std::atoi(at(f, i + 10).c_str()) != 0;
        out.push_back(r);
    }
    for (const auto& kv : map.section("Units")) {
        const std::vector<std::string> f = split_csv(kv.second);
        MapObjectRec r;
        r.kind = 1;
        r.key = kv.first;
        std::size_t i = 0;
        if (!head(r, f, i)) continue;
        r.facing = std::atoi(at(f, i).c_str());    // Facing
        r.mission = at(f, i + 1);                  // Mission
        r.tag = tag_of(at(f, i + 2));              // Tag
        r.veterancy = std::atoi(at(f, i + 3).c_str());
        r.group = f.size() > i + 4 ? std::atoi(at(f, i + 4).c_str()) : -1;
        out.push_back(r);
    }
    for (const auto& kv : map.section("Infantry")) {
        const std::vector<std::string> f = split_csv(kv.second);
        MapObjectRec r;
        r.kind = 2;
        r.key = kv.first;
        std::size_t i = 0;
        if (!head(r, f, i)) continue;
        r.subcell = std::atoi(at(f, i).c_str());    // SubCell
        r.mission = at(f, i + 1);                   // Mission
        r.facing = std::atoi(at(f, i + 2).c_str()); // Facing
        r.tag = tag_of(at(f, i + 3));               // Tag
        r.veterancy = std::atoi(at(f, i + 4).c_str());
        r.group = f.size() > i + 5 ? std::atoi(at(f, i + 5).c_str()) : -1;
        out.push_back(r);
    }
    for (const auto& kv : map.section("Aircraft")) {
        const std::vector<std::string> f = split_csv(kv.second);
        MapObjectRec r;
        r.kind = 3;
        r.key = kv.first;
        std::size_t i = 0;
        if (!head(r, f, i)) continue;
        r.facing = std::atoi(at(f, i).c_str());
        r.mission = at(f, i + 1);
        r.tag = tag_of(at(f, i + 2));
        r.veterancy = std::atoi(at(f, i + 3).c_str());
        r.group = f.size() > i + 4 ? std::atoi(at(f, i + 4).c_str()) : -1;
        out.push_back(r);
    }
    return out;
}

int campaign_strength(int type_strength, int health256, bool building) {
    if (building && health256 > 255) health256 = 256;
    int s = (int)om::lround((double)type_strength * (double)health256 / 256.0);
    if (s > type_strength - 3) s = type_strength;
    if (s < 1) s = 1;
    return s;
}

std::vector<std::string> campaign_house_order(const ra2assets::Ini& map, const std::string& player) {
    std::vector<std::string> houses;
    for (const auto& kv : map.section("Houses")) {
        const std::string h = strip(kv.second);
        if (!h.empty()) houses.push_back(h);
    }
    int pi = -1;
    for (std::size_t i = 0; i < houses.size() && pi < 0; ++i)
        if (ieq(houses[i], player)) pi = (int)i;
    for (std::size_t i = 0; i < houses.size() && pi < 0; ++i)
        if (ieq(strip(map.get(houses[i], "Country", "")), player)) pi = (int)i;
    if (pi > 0) {
        const std::string p = houses[(std::size_t)pi];
        houses.erase(houses.begin() + pi);
        houses.insert(houses.begin(), p);
    }
    return houses;
}

// ---------------------------------------------------------------------------
void MissionEnd::flag(int outcome, long frame) {
    if (outcome_ != 0 || (outcome != 1 && outcome != 2)) return;
    outcome_ = outcome;
    flagged_at_ = frame;
}

int borrowed_time_frames(double savour_delay_minutes) {
    return (int)om::lround(savour_delay_minutes * 900.0);
}

// ---------------------------------------------------------------------------
int MapTeamData::find_team(const std::string& id) const {
    for (std::size_t i = 0; i < teams.size(); ++i) if (ieq(teams[i].id, id)) return (int)i;
    return -1;
}
int MapTeamData::find_script(const std::string& id) const {
    for (std::size_t i = 0; i < scripts.size(); ++i) if (ieq(scripts[i].id, id)) return (int)i;
    return -1;
}
const std::vector<MapTaskMember>* MapTeamData::taskforce(const std::string& id) const {
    for (const auto& tf : taskforces) if (ieq(tf.first, id)) return &tf.second;
    return nullptr;
}

MapTeamData parse_map_teams(const ra2assets::Ini& map) {
    MapTeamData d;
    for (const auto& kv : map.section("ScriptTypes")) {
        const std::string id = strip(kv.second);
        if (!map.has(id)) continue;
        MapScript s;
        s.id = id;
        s.name = map.get(id, "Name", "");
        // Numbered lines 0..49 in numeric order (ScriptTypeClass::ReadINI).
        for (int n = 0; n < 50; ++n) {
            const std::string v = map.get(id, std::to_string(n), "");
            if (v.empty()) break;
            const std::vector<std::string> f = split_csv(v);
            MapScriptAction a;
            a.action = std::atoi(f[0].c_str());
            a.arg = f.size() > 1 ? std::atoi(f[1].c_str()) : 0;
            s.actions.push_back(a);
        }
        d.scripts.push_back(s);
    }
    for (const auto& kv : map.section("TaskForces")) {
        const std::string id = strip(kv.second);
        if (!map.has(id)) continue;
        std::vector<MapTaskMember> ms;
        for (int n = 0; n < 6; ++n) {
            const std::string v = map.get(id, std::to_string(n), "");
            if (v.empty()) continue;
            const std::vector<std::string> f = split_csv(v);
            if (f.size() < 2) continue;
            MapTaskMember m;
            m.count = std::atoi(f[0].c_str());
            m.type = f[1];
            ms.push_back(m);
        }
        d.taskforces.emplace_back(id, ms);
    }
    for (const auto& kv : map.section("TeamTypes")) {
        const std::string id = strip(kv.second);
        if (!map.has(id)) continue;
        MapTeamType t;
        t.id = id;
        t.name = map.get(id, "Name", "");
        t.house = strip(map.get(id, "House", ""));
        t.script = strip(map.get(id, "Script", ""));
        t.taskforce = strip(map.get(id, "TaskForce", ""));
        const std::string tg = strip(map.get(id, "Tag", ""));
        t.tag = ieq(tg, "None") ? std::string() : tg;
        const std::string wp = strip(map.get(id, "Waypoint", ""));
        t.waypoint = wp.empty() ? -1 : map_waypoint_letters(wp);
        t.max = map.get_int(id, "Max", 0);
        t.full = yes(map.get(id, "Full", "no"));
        t.recruitable = yes(map.get(id, "AreTeamMembersRecruitable", "no"));
        t.veteran_level = map.get_int(id, "VeteranLevel", 0);
        t.priority = map.get_int(id, "Priority", 0);
        t.reinforce = yes(map.get(id, "Reinforce", "no"));   // W6-02-16 (+0xAB)
        t.droppod = yes(map.get(id, "Droppod", "no"));
        t.use_transport_origin = yes(map.get(id, "UseTransportOrigin", "no"));
        t.transports_return = yes(map.get(id, "TransportsReturnOnUnload", "no"));
        const std::string twp = strip(map.get(id, "TransportWaypoint", ""));
        t.transport_waypoint = twp.empty() ? -1 : map_waypoint_letters(twp);
        t.aggressive = yes(map.get(id, "Aggressive", "no"));            // fv7c (+0xAD)
        t.suicide = yes(map.get(id, "Suicide", "no"));                  // fv7c (+0xAF)
        t.is_base_defense = yes(map.get(id, "IsBaseDefense", "no"));    // fv7c (+0xF6)
        d.teams.push_back(t);
    }
    return d;
}

int mission_from_name(const std::string& name) {
    static const char* kNames[] = {
        "Sleep", "Attack", "Move", "QMove", "Retreat", "Guard", "Sticky", "Enter",
        "Capture", "Eaten", "Harvest", "Area Guard", "Return", "Stop", "Ambush", "Hunt",
        "Unload", "Sabotage", "Construction", "Selling", "Repair", "Rescue", "Missile",
        "Harmless", "Open", "Patrol"};
    for (int i = 0; i < (int)(sizeof kNames / sizeof kNames[0]); ++i)
        if (ieq(name, kNames[i])) return i;
    return kMissionGuard;
}

MapTeamCommand step_map_team(MapTeamState& st, const MapScript& script, const MapTeamView& view,
                             long frame) {
    MapTeamCommand c;
    if (st.done) { c.kind = MapTeamCommand::Kind::Done; return c; }
    // Guard against a jump cycle that only contains instant actions.
    for (int guard = 0; guard < 64; ++guard) {
        if (st.pc < 0 || st.pc >= (int)script.actions.size() || view.live_members <= 0) {
            st.done = true;
            c.kind = MapTeamCommand::Kind::Done;
            return c;
        }
        const MapScriptAction& a = script.actions[(std::size_t)st.pc];
        c.action = a.action;
        c.arg = a.arg;
        c.fresh = !st.issued;
        auto advance = [&]() { ++st.pc; st.issued = false; st.wait_until = -1; };
        switch (a.action) {
        case kSaMoveWaypoint:
        case kSaPatrol:
        case kSaMoveCell:
        case kSaGatherEnemy:
        case kSaGatherBase:
        case kSaMoveEnemyBuilding:
        case kSaMoveFriendlyBuilding:   // W4-SQ-11: 58, the 47 encoding on own buildings
            if (!view.waypoint_ok && st.issued) { advance(); continue; }
            if (st.issued && view.arrived) { advance(); continue; }
            c.kind = a.action == kSaMoveCell ? MapTeamCommand::Kind::MoveCell
                                             : MapTeamCommand::Kind::MoveWaypoint;
            st.issued = true;
            return c;
        case kSaAttack:
        case kSaAttackWaypoint:
        case kSaAttackEnemyBuilding:
            if (st.issued && (!view.target_exists || view.members_idle)) { advance(); continue; }
            c.kind = a.action == kSaAttack ? MapTeamCommand::Kind::Attack
                   : a.action == kSaAttackWaypoint ? MapTeamCommand::Kind::AttackWaypoint
                                                   : MapTeamCommand::Kind::AttackBuilding;
            st.issued = true;
            return c;
        case kSaGuard:
            if (!st.issued) {
                st.issued = true;
                st.wait_until = frame + (long)a.arg * kMapGuardFramesPerArg;
                c.kind = MapTeamCommand::Kind::Guard;
                return c;
            }
            if (frame >= st.wait_until) { advance(); continue; }
            c.kind = MapTeamCommand::Kind::Guard;
            return c;
        case kSaJump:
            // RE §7: the cursor is set to arg-2 and then advanced -> line arg-1.
            st.pc = a.arg - 1;
            st.issued = false;
            st.wait_until = -1;
            // A jump consumes the tick (no spin).
            c.kind = MapTeamCommand::Kind::None;
            return c;
        case kSaDoMission:
            c.kind = MapTeamCommand::Kind::Mission;
            advance();
            return c;
        case kSaWin: c.kind = MapTeamCommand::Kind::Win; advance(); return c;
        case kSaLose: c.kind = MapTeamCommand::Kind::Lose; advance(); return c;
        case kSaSetGlobal: c.kind = MapTeamCommand::Kind::SetGlobal; advance(); return c;
        case kSaClearGlobal: c.kind = MapTeamCommand::Kind::ClearGlobal; advance(); return c;
        case kSaSetLocal: c.kind = MapTeamCommand::Kind::SetLocal; advance(); return c;
        case kSaClearLocal: c.kind = MapTeamCommand::Kind::ClearLocal; advance(); return c;
        case kSaChangeHouse: c.kind = MapTeamCommand::Kind::ChangeHouse; advance(); return c;
        case kSaScatter: c.kind = MapTeamCommand::Kind::Scatter; advance(); return c;
        // W4-SQ-01 [RE: FUN_006ee670]: Unload orders every member transport
        // to unload and the line completes only once all of them are empty;
        // the completion tick carries the arg mode (who leaves the team).
        case kSaUnload:
            if (st.issued && view.unloaded) {
                c.kind = MapTeamCommand::Kind::UnloadDone;
                advance();
                return c;
            }
            c.kind = MapTeamCommand::Kind::Unload;
            st.issued = true;
            return c;
        // W4-SQ-01 [RE: FUN_006ec760]: members board the team's transport; the
        // line completes when nobody is left on the map to board.
        case kSaLoad:
            if (st.issued && view.loaded) { advance(); continue; }
            c.kind = MapTeamCommand::Kind::Load;
            st.issued = true;
            return c;
        // W4-SQ-01 [RE: case 43 @0x6E9192]: wait until every member's
        // NumPassengers reaches its type's Passengers.
        case kSaWaitLoaded:
            if (view.full) { advance(); continue; }
            c.kind = MapTeamCommand::Kind::None;
            return c;
        // W2-CA-17 [campaign.md §7, FUN_006eca30]: one-shot.
        case kSaDeploy: c.kind = MapTeamCommand::Kind::Deploy; advance(); return c;
        case kSaDeleteMembers: c.kind = MapTeamCommand::Kind::DeleteMembers; advance(); return c;
        // W5-A R-027 [RE: the script executor's table @0x6E94E4, each handler
        // disassembled; docs/re/campaign.md §7]. 42 ForceFacing completes once
        // every member stopped turning -- the clone's facing set is instant.
        case kSaForceFacing: c.kind = MapTeamCommand::Kind::ForceFacing; advance(); return c;
        case kSaIronCurtainMe:
            if (view.iron_state == 2) { c.kind = MapTeamCommand::Kind::None; return c; }   // wait
            c.kind = MapTeamCommand::Kind::IronCurtain;
            advance();
            return c;
        case kSaPanic: c.kind = MapTeamCommand::Kind::Panic; advance(); return c;
        case kSaUnpanic: c.kind = MapTeamCommand::Kind::Unpanic; advance(); return c;
        // 10 Follow never sets the line-complete flag (+0x80): the team keeps
        // following until something else (a trigger) ends it.
        case kSaFollow:
            c.kind = MapTeamCommand::Kind::Follow;
            st.issued = true;
            return c;
        case kSaFlash: c.kind = MapTeamCommand::Kind::Flash; advance(); return c;
        case kSaChangeTeam: c.kind = MapTeamCommand::Kind::ChangeTeam; advance(); return c;
        default:
            c.kind = MapTeamCommand::Kind::Skip;
            advance();
            return c;
        }
    }
    c.kind = MapTeamCommand::Kind::None;
    return c;
}

}  // namespace ra2engine
