#pragma once
// aimd.ini TaskForce/ScriptType/TeamType tables [RE: docs/re/ai-team.md §1].
// Pure parsing over the EXISTING ra2assets::Ini — no Ini-layer changes:
// section() already preserves numbered-key file order, which is exactly what
// the [TaskForces]/[ScriptTypes]/[TeamTypes] index sections and the per-entity
// N=... entries need. Struct layouts + key spellings are HUNT OUTPUTS
// [RE-GATE APPLIED: ai-team.md §1 — the pre-hunt shapes below are the spec's
// approved data model; the RE-GATE confirms or replaces them].
#include <functional>
#include <string>
#include <vector>
#include "ra2assets/ini.hpp"

namespace ra2engine {

// One TaskForce line: N=count,UNITID [RE: ai-team.md §1].
struct TaskForceMember {
    int count = 0;   // how many of this type the team needs
    int unit = -1;   // caller-resolved unit-roster index (see parse_aimd)
};
struct TaskForce {
    std::string id;     // section id (e.g. "01000050-G")
    std::string name;   // Name=
    int group = -1;     // Group= [RE: ai-team.md §1 — recorded; not consumed in AI-2]
    std::vector<TaskForceMember> members;  // file order
};

// One ScriptType line: N=action,argument [RE: ai-team.md §1/§3].
struct ScriptAction { int action = 0; int argument = 0; };
struct ScriptType {
    std::string id;
    std::string name;   // Name=
    std::vector<ScriptAction> actions;  // file order — script_pc indexes this
};

// One TeamType section [RE-GATE APPLIED: ai-team.md §1 — the named-key set;
// the hunt adds any flags that turn out load-bearing for the autocreate path].
struct TeamType {
    std::string id;
    std::string name;        // Name=
    std::string house;       // House= raw token (e.g. "Russians"); "<none>"/"" = any
                             // (universal in stock data — §1)
    int taskforce = -1;      // TaskForce=<id> resolved to AimdData::taskforces index
    int script = -1;         // Script=<id> resolved to AimdData::scripts index
    int priority = 0;        // Priority= (higher = considered first) [RE: ai-team.md §4]
    int max = 0;             // Max= live-instance cap; 0 = UNCAPPED [RE: ai-team.md §4]
    bool full = false;       // Full= (+0xa5): yes = wait for full strength before
                             // acting; no = act under strength [RE: ai-team.md §2]
    bool autocreate = false; // Autocreate= (+0xa9) — AI-2 creates ONLY these [RE: §4]
    bool aggressive = false; // Aggressive= (+0xad) offensive-marker, informational [RE: §1]
    bool reinforce = false;  // Reinforce= (+0xab) [RE: ai-team.md §1]
    bool suicide = false;    // Suicide= (+0xaf) [RE: ai-team.md §1]
    int tech_level = 0;      // TechLevel= (+0xcc) [RE: ai-team.md §1]
    bool is_base_defense = false;  // IsBaseDefense= (+0xf6) marks DEFENSIVE teams —
                             // skipped by the offensive selection [RE: ai-team.md §1]
    int waypoint = -1;       // Waypoint= letter -> index (A=0, B=1, ..., AA=26)
                             // [RE-GATE APPLIED: ai-team.md §1 — encoding pre-hunt]
};

struct AITriggerType;  // AI-3 [RE: ai-trigger.md §1] — defined below

struct AimdData {
    std::vector<TaskForce> taskforces;
    std::vector<ScriptType> scripts;
    std::vector<TeamType> teams;
    std::vector<AITriggerType> triggers;  // AI-3 [RE: ai-trigger.md §1]
};

// Waypoint letter encoding: "A"=0.."Z"=25, "AA"=26.. (bijective base-26).
// Empty/other -> -1 [RE-GATE APPLIED: ai-team.md §1].
int waypoint_from_letter(const std::string& s);

// Parse the three tables from an aimd-shaped Ini [RE: ai-team.md §1]:
// walk each index section ([TaskForces]/[ScriptTypes]/[TeamTypes], index=id,
// file order) via Ini::section(), then parse each entity section.
// `unit_index` maps a canonical-UPPER UNITID to the caller's unit-roster
// index, or -1 when this install cannot field that type (art missing / not
// loaded). ART-GUARD-DROP [spec §2]: a TaskForce containing ANY unresolved
// UNITID (or no members) is dropped WHOLE, and every TeamType referencing a
// dropped/missing TaskForce or a missing ScriptType is dropped too — the team
// is skipped, never a crash (AI-1's AiCatalog -1 discipline). Dangling index
// entries (id with no section) are skipped. Deterministic: output order =
// index-section file order.
AimdData parse_aimd(const ra2assets::Ini& ini,
                    const std::function<int(const std::string&)>& unit_index);

// --- AI-3 AITriggerTypes [RE: docs/re/ai-trigger.md §1] ----------------------
// The [AITriggerTypes] section is a FLAT list: each <id>=Name,field,field,...
// key is ONE complete trigger (NOT a per-id entity section like TaskForces/
// ScriptTypes/TeamTypes) — 165 stock lines. Field order is CONFIRMED
// [RE-GATE APPLIED: ai-trigger.md §1]: Name, Team1, OwnerHouse, TechLevel,
// ConditionType, ConditionObject, Conditions(blob), StartWeight, MinWeight,
// MaxWeight, IsForSkirmish, 0(unused), SideIndex, IsForBaseDefense, Team2,
// Enabled_Easy, Enabled_Normal, Enabled_Hard (the FormatForSaving comma
// order, matched 1:1 by the real aimd.ini data).

// The counted TechnoType may be a UNIT or a BUILDING [spec §2], so it resolves
// through the caller's two resolvers to a {kind, roster index} pair. None =
// no object (non-counting conditions carry "<none>").
enum class TechnoKind { None = 0, Unit = 1, Building = 2 };
struct TriggerObject {
    TechnoKind kind = TechnoKind::None;
    int index = -1;   // caller's unit- or building-roster index (by kind)
};

// Comparator = count threshold + operator, decoded from the on-disk blob
// [RE-GATE APPLIED: ai-trigger.md §2 — CONFIRMED VALUE-FIRST packing:
// dword0 = count/value N (struct +0xe4), dword1 = operator 0..5 (+0xe8);
// the on-disk field is AITriggerConditionComparator Conditions[4] = 32 bytes,
// only slot [0] meaningful]. Operators (ai_trigger.hpp kComp*): 0=< 1=<=
// 2=== 3=>= 4=> 5=!=, tested as `actualCount OP value`.
struct TriggerComparator { int value = 0; int op = 0; };

struct AITriggerType {
    std::string id;            // the flat-list key (canonical UPPER)
    std::string name;          // field 0
    int team1 = -1;            // field 1: TeamType id -> AimdData::teams index
                               // (REQUIRED — trigger dropped whole if missing;
                               // the Team1-identity attribution key [RE-GATE
                               // APPLIED: ai-trigger.md §6])
    int team2 = -1;            // field 14: second team ALSO created for the
                               // owner alongside team1 (may equal team1 -> two
                               // instances) [RE-GATE APPLIED: ai-trigger.md §3
                               // — CONFIRMED, NOT a reciprocal]; "<none>"
                               // (116/165 stock) or a dropped id -> -1
                               // tolerated (Team2 is OPTIONAL)
    std::string owner_house;   // field 2 raw token; "<all>"=Any, country name
                               // =match, "<none>"/""=reject at filter time
                               // [RE-GATE APPLIED: ai-trigger.md §5]
    int tech_level = 0;        // field 3: house tech level must be >= this
    int condition_type = -1;   // field 4: recovered enum −1..7 (ai_trigger.hpp
                               // kCond*) [RE-GATE APPLIED: ai-trigger.md §2]
    TriggerObject condition_object;  // field 5: TechnoType or <none>
    TriggerComparator comparator;    // field 6: 64-hex-char Conditions[4]
                                     // blob, decode_comparator
    double starting_weight = 0.0;    // field 7 (floats in the file)
    double min_weight = 0.0;         // field 8
    double max_weight = 0.0;         // field 9
    bool is_for_skirmish = false;    // field 10 (=1 for ALL stock)
    // field 11 is a literal 0, recorded-only [RE-GATE APPLIED: ai-trigger.md
    // §1 — CONFIRMED unused, not stored by the original either]
    int side = 0;              // field 12: SideIndex 0=any, 1=Allied,
                               // 2=Soviet, 3=Yuri [RE-GATE APPLIED:
                               // ai-trigger.md §5 — CONFIRMED]
    bool base_defense = false; // field 13: IsForBaseDefense (=0 for ALL stock)
                               // [RE-GATE APPLIED: ai-trigger.md §1]
    bool enabled[3] = {false, false, false};  // fields 15/16/17 parse in
                               // Easy,Normal,Hard ON-DISK order — enabled[0]=
                               // Easy, [1]=Normal, [2]=Hard; the RUNTIME
                               // consult is INVERTED (house difficulty index
                               // 0 = HARDEST -> enabled[2]) — ai_trigger.hpp's
                               // trigger_filters_pass owns the inversion
                               // [RE-GATE APPLIED: ai-trigger.md §5 — CONFIRMED]
    bool is_enabled = true;    // [AITriggerTypesEnable] per-trigger flag —
                               // default ENABLED when the id is unlisted
                               // [RE-GATE APPLIED: ai-trigger.md §5]
};

// Decode the on-disk comparator blob [RE-GATE APPLIED: ai-trigger.md §2 —
// CONFIRMED]: 64 hex chars = the 32-byte `Conditions[4]` field = 8 little-
// endian dwords, VALUE-FIRST — dword [0] = count/value N, dword [1] =
// operator (0..5); dwords [2..7] (Conditions[1..3]) are reserved, always 0
// in all 165 stock lines, parsed for hex validity but IGNORED for meaning.
// Returns false (caller drops the trigger) on a malformed blob (length != 64
// or non-hex anywhere).
bool decode_comparator(const std::string& hex, TriggerComparator& out);

// Parse [AITriggerTypes] into d.triggers [RE: ai-trigger.md §1]: one trigger
// per flat-list key, file order, via Ini::section(). d.teams must already be
// parsed (parse_aimd) — team1/team2 resolve against it, and a trigger whose
// team1 was dropped/missing is dropped WHOLE (team2 is optional: "<none>" or
// a dropped id -> -1). condition_object resolves through `unit_index` first,
// then `building_index` (each canonical-UPPER token -> the caller's roster
// index, or -1 = this install cannot field it); an unresolvable non-"<none>"
// object ART-GUARD-DROPS the whole trigger — skipped, never a crash, exactly
// the parse_aimd discipline [spec §2]. Then applies the SEPARATE
// [AITriggerTypesEnable] section (<trigger-id>=<bool>) to each matching
// trigger's is_enabled — a per-trigger on/off list, NOT a subsystem switch;
// unlisted ids stay enabled [RE-GATE APPLIED: ai-trigger.md §5 — CONFIRMED].
// Deterministic: output order = section file order.
void parse_ai_triggers(const ra2assets::Ini& ini, AimdData& d,
                       const std::function<int(const std::string&)>& unit_index,
                       const std::function<int(const std::string&)>& building_index);

}  // namespace ra2engine
