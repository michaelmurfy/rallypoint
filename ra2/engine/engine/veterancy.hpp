#pragma once
// P5 Veterancy [RE: docs/re/veterancy.md]: the pure, World/SDL-free veterancy
// arithmetic recovered from YR-1.001 gamemd.exe, plus the per-unit state
// struct (UnitVet) that World embeds in every Unit.
//
// EXACTNESS: the CRT runs the x87 FPU at 53-bit precision (veterancy.md V16),
// so each formula below is plain IEEE double arithmetic, ONE operation per
// statement (never let the compiler contract a*b+c into an FMA), and `ftol`
// is truncation toward zero (static_cast<int>). The boundary cases
// (5*0.6 == 3.0 in double, 25*1.1 == 27.500000000000004) are pinned in
// tests/test_veterancy.cpp.
//
// BYTE-IDENTITY: nothing here runs unless World::enable_veterancy was called
// (spec §Global constraints); UnitVet's defaults are all inert.
#include <cstdint>
#include <string>
#include <vector>

namespace ra2engine {

// The 18 abilities, in the original's bool-array order (TechnoType+0x29c
// veteran / +0x2ae elite) [RE: veterancy.md V4/V14].
enum class Ability : int {
    Faster = 0, Stronger, Firepower, Scatter, Rof, Sight, Cloak, TiberiumProof,
    VeinProof, SelfHeal, Explodes, RadarInvisible, Sensors, Fearless, C4,
    TiberiumHeal, GuardArea, Crusher
};
using AbilityMask = std::uint32_t;
inline bool ability_bit(AbilityMask m, Ability a) {
    return (m >> static_cast<int>(a)) & 1u;
}
// "STRONGER,FIREPOWER,..." -> mask. Case-insensitive, whitespace-tolerant, a
// trailing ';' comment is ignored, unknown tokens are skipped (ability
// defaults are all false -- veterancy.md V14).
AbilityMask parse_abilities(const std::string& csv);

// [General] veteran factors + the two shared keys this phase reads. DEFAULTS
// ARE this install's rulesmd.ini values [RE: veterancy.md §0]; main.cpp
// resolves the real ones (resolve_veterancy_rules) and the tests inject
// non-default values to prove the wiring.
struct VeterancyRules {
    double ratio = 3.0;         // VeteranRatio
    double combat = 1.1;        // VeteranCombat (firepower multiplier)
    double speed = 1.2;         // VeteranSpeed
    double sight = 0.0;         // VeteranSight (0.0 -> the sight branch is dead, V9)
    double armor = 1.5;         // VeteranArmor (incoming damage divisor)
    double rof = 0.6;           // VeteranROF (rearm-delay multiplier)
    double cap = 2.0;           // VeteranCap
    double repair_rate = 0.016; // [General] RepairRate (self-heal cadence, V11)
    int elite_flash = 150;      // [AudioVisual] EliteFlashTimer (frames, V12)
};

// One weapon's combat stats (the normal Primary or the ElitePrimary).
struct WeaponStats {
    int damage = 0;
    int rof = 0;                 // frames
    double range = 0.0;          // cells
    std::vector<int> verses;
    double cell_spread = 0.0;
    double percent_at_max = 1.0;
    bool affects_allies = true;   // W5-B R-060: warhead AffectsAllies=
    // W3-B [RE: InfantryClass::ReceiveDamage FUN_00517db5]: the warhead's
    // InfDeath= (death anim / Die sequence of a killed infantry; absent = 0)
    // and ProneDamage= (damage multiplier on a prone infantry; absent = 1).
    int inf_death = 0;
    double prone_damage = 1.0;
    int burst = 1;                // W6-04-01: weapon Burst= (+0x9C)
    bool omni_fire = false;       // W6-04-06: weapon OmniFire= (+0x12B)
    bool sonic = false;           // W6-04-09: warhead Sonic= (+0x14B)
    int wall_flags = 0;           // W6-04-04: warhead Wall/Wood/WallAbsoluteDestroyer
    // W6-04-02 [RE: BulletTypeClass ShrapnelWeapon/ShrapnelCount -- keys of the
    // weapon's PROJECTILE ([LargeCometP], [Electricbounce], [SuperCometP])]:
    // the fragment weapon's Damage/Range/Verses and the fragment count.
    int shrapnel_count = 0;
    int shrapnel_damage = 0;
    double shrapnel_range = 0.0;
    std::vector<int> shrapnel_verses;
};

// V1: rank bands. 0 = rookie (incl. negative), 1 = veteran, 2 = elite.
bool is_veteran(float v);   // 1.0f <= v < 2.0f
bool is_elite(float v);     // v >= 2.0f
int vet_rank(float v);

// V2: VeterancyStruct::Add(ownerCost, victimCost) -- computed in double, the
// cap compared against the UNROUNDED double sum, stored as float.
float veterancy_add(float v, int owner_cost, int victim_cost, const VeterancyRules& r);

// V3: the victim's experience value: Cost, x2 if veteran, x3 if elite, 0 if
// the killer's house is allied with the victim.
int kill_value(int victim_cost, float victim_vet, bool allied);

// V4: HasAbility = (vet && V[a]) || (elite && (V[a] || E[a])).
bool has_ability(float v, AbilityMask vet, AbilityMask elite, Ability a);

// V5: d = ftol(fp_mult * damage); FIREPOWER -> ftol(d * VeteranCombat).
int fire_damage(int damage, double fp_mult, bool firepower, double vet_combat,
                double house_fp = 1.0);   // house_fp: AssignHandicap FirePower [G-63]
// V6: d = ftol(d / armor_mult); STRONGER -> ftol(d / VeteranArmor); max(d, 1).
// (Applied BEFORE the warhead Verses. Callers only use it for d >= 0.)
int receive_damage(int d, double armor_mult, bool stronger, double vet_armor);
// V7: r = ftol(rof); ROF ability -> ftol(r * VeteranROF). (The original's
// +Random(0,2) jitter is NOT reproduced -- spec §Scope.)
int rof_frames(int rof, bool rof_ability, double vet_rof,
               double house_rof = 1.0);   // house_rof: AssignHandicap ROF [G-63]
// V8: s = ftol(speed * speed_mult); FASTER -> ftol(s * VeteranSpeed).
int speed_value(int speed, double speed_mult, bool faster, double vet_speed);
// V9: sight x VeteranSight only when VeteranSight != 0.0.
int sight_value(int sight, bool sight_ability, double vet_sight);

// V11: interval = ftol(RepairRate * 900) (14 at .016); heal iff eligible,
// frame % interval == 0, 0 < hp < max_hp.
int self_heal_interval(double repair_rate);
bool should_self_heal(std::uint64_t frame, int interval, bool eligible, int hp, int max_hp);

// C6 Veteran crate: `levels` iterations of {veteran->elite; rookie->veteran;
// negative->rookie}, each test on the updated value.
float promote_levels(float v, double levels);

// Per-unit veterancy state, embedded in Unit as `vet`. ALL defaults inert:
// a World that never calls enable_veterancy never reads any of it except the
// killed_by stamp combat.cpp writes at a unit's death (read by nothing else).
struct UnitVet {
    // --- instance state ---
    float veterancy = 0.0f;       // Techno+0x150 [V1]
    int last_rank = -1;           // Update's stored rank (+0x13c), -1 = unseen [V12]
    int elite_flash = 0;          // frames of elite flash left [V12] (render-only)
    double firepower_mult = 1.0;  // FirepowerMultiplier (Firepower crate)
    double armor_mult = 1.0;      // ArmorMultiplier (Armor crate)
    double speed_mult = 1.0;      // SpeedMultiplier (Speed crate)
    bool recv = false;            // combat.cpp receive-damage hook armed (vet worlds only)
    double vet_armor = 1.5;       // VeteranArmor copy for the combat hook
    int killed_by = -1;           // unit index of the killer (combat.cpp stamp)
    bool credited = false;        // kill credit already resolved
    // --- type data (copied from UnitCombat at spawn) ---
    int cost = 0;
    bool trainable = true;        // Trainable (default yes, V14)
    bool dont_score = false;      // DontScore
    bool self_healing = false;    // SelfHealing= (type flag, V11)
    AbilityMask vet_abilities = 0;
    AbilityMask elite_abilities = 0;
    int rules_speed = 0;          // integer Speed= (V8 domain)
    WeaponStats base;             // Primary as resolved
    bool has_elite = false;       // ElitePrimary defined (V10)
    WeaponStats elite;            // ElitePrimary
    double base_speed_cps = 0.0;  // spawn speed_cps snapshot (0 = World move_cps)
    int base_sight = 0;           // spawn sight snapshot
};

// The combat.cpp receive-damage hook for a unit victim [V6]: armor mult,
// then STRONGER (rank-gated), then max(1). Called only when v.recv is set.
inline int vet_receive(int d, const UnitVet& v) {
    const bool stronger = has_ability(v.veterancy, v.vet_abilities, v.elite_abilities,
                                      Ability::Stronger);
    return receive_damage(d, v.armor_mult, stronger, v.vet_armor);
}

}  // namespace ra2engine
