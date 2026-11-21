#pragma once
// P4 Unit special abilities (batch 1) -- pure DATA types [RE: abilities.md].
// No logic, no World: included by world.hpp (Unit carries an AbilityProfile)
// and by abilities.hpp (the pure math). Every default below is INERT -- a
// default-constructed AbilityProfile is "no ability at all", which is what
// every unit in every pre-P4 gate carries (the byte-identity firewall).
#include <cstdint>
#include <vector>

#include "projectile.hpp"   // ProjectileSpec (wave-5 IFV projectile swap)

namespace ra2engine {

// What a weapon does INSTEAD of plain damage when combat.cpp would fire it.
// Derived from the weapon/warhead flags, never from the unit id:
//   Parasite  <- warhead Parasite=yes      (TryInfect, §B1)
//   Temporal  <- warhead Temporal=yes      (TemporalClass::Fire, §C2)
//   IvanBomb  <- warhead IvanBomb=yes      (BombList::Plant, §E)
//   Suicide   <- weapon  Suicide=yes       (TechnoClass::Fire early-out, §F)
enum class AbilityKind : std::uint8_t { None = 0, Parasite, Temporal, IvanBomb, Suicide };

// One resolved weapon (rules id -> numbers). valid=false = "no such weapon".
struct WeaponSpec {
    bool valid = false;
    int damage = 0;
    int rof = 0;                       // frames
    double range = 0.0;                // cells
    std::vector<int> verses;           // 11 warhead percents (combat.md §1)
    double cell_spread = 0.0;          // warhead CellSpread (cells)
    double percent_at_max = 1.0;       // warhead PercentAtMax
    int rad_level = 0;                 // weapon RadLevel (§A1; 0 = no RadSite)
    AbilityKind kind = AbilityKind::None;
    // Wave-5 (R-048): the weapon's projectile AA=/AG= and flight -- an IFV
    // passenger swap now also swaps what the gun can hit and how it flies.
    bool slot_set = false;             // aa/ag/proj below were resolved
    bool aa = false, ag = true;
    ProjectileSpec proj;
    // P4 [RE: FUN_00700DA0 @0x701327..0x7013F5]: warhead CausesDelayKill
    // (+0x130), DelayKillFrames (+0x134), DelayKillAtMax (+0x138).
    bool delay_kill = false;
    int delay_kill_frames = 0;
    double delay_kill_at_max = 1.0;
    int burst = 1;                     // W6-04-01: weapon Burst= (+0x9C)
    bool omni_fire = false;            // W6-04-06: weapon OmniFire= (+0x12B)
    // W6-04-03: the warhead keys the faithful DamageArea reads (ability blasts
    // now go through it): AffectsAllies (+0x179), ProneDamage, InfDeath.
    bool affects_allies = true;
    double prone_damage = 1.0;
    int inf_death = 0;
    int wall_flags = 0;   // W6-04-04: warhead Wall/Wood/WallAbsoluteDestroyer
};

// Per-type ability profile, resolved once from rules by the caller (World is
// asset-free) and bound to units by IMAGE through
// World::register_ability_profile. ALL defaults inert.
struct AbilityProfile {
    AbilityKind fire_kind = AbilityKind::None;  // the primary weapon's kind
    bool c4 = false;                 // InfantryType C4=yes (§D)
    double c4_range = 0.0;           // the Secondary (Sapper) range, cells
    bool teleporter = false;         // Teleporter=yes (§C1)
    bool explodes = false;           // Explodes=yes (§F)
    WeaponSpec death;                // DeathWeapon, else Primary (§F)
    bool deployer = false;           // Deployer=yes + DeployFire=yes (§A)
    WeaponSpec deploy;               // the deploy-fire weapon (Secondary)
    bool immune_radiation = false;   // ImmuneToRadiation (+0xD37)
    bool parasiteable = true;        // Parasiteable (+0xD38) -- foot units yes
    bool warpable = true;            // Warpable (+0xD3A)
    bool no_passive_acquire = false; // CanPassiveAquire=no
    bool no_retaliate = false;       // CanRetaliate=no
    int suppression_threshold = 0;   // SuppressionThreshold (+0xD6C), drone
    int ifv_mode = 0;                // IFVMode (+0x688) -- as a passenger
    bool gunner = false;             // Gunner=yes (+0x805) -- as an IFV
    std::vector<WeaponSpec> ifv_weapons;  // Weapon1..WeaponN, 0-based (§G)
    std::vector<WeaponSpec> ifv_weapons_elite;   // W6-04-28: EliteWeapon1..N (invalid = the base one)
    bool infantry = false;           // the parasite's instant-kill branch (§B2)
};

// [IvanWH] on this install: Verses 100x7,250,20,100,100 CellSpread=1.5
// PercentAtMax=.25; damage = IvanDamage [RE: abilities.md §E].
inline WeaponSpec default_ivan_warhead() {
    WeaponSpec w;
    w.valid = true;
    w.damage = 450;
    w.verses = {100, 100, 100, 100, 100, 100, 100, 250, 20, 100, 100};
    w.cell_spread = 1.5;
    w.percent_at_max = 0.25;
    return w;
}

// The recovered global knobs. Defaults ARE this install's real rulesmd values
// ([Radiation] / [General] / [CombatDamage]) so a World that never calls
// set_ability_rules already behaves like the original [RE: abilities.md].
struct AbilityRules {
    int rad_duration_multiple = 1;     // RadDurationMultiple (+0x1804)
    int rad_application_delay = 16;    // RadApplicationDelay (+0x1808)
    int rad_level_max = 500;           // RadLevelMax (+0x180C)
    int rad_level_delay = 90;          // RadLevelDelay (+0x1810)
    double rad_level_factor = 0.2;     // RadLevelFactor (+0x1818)
    std::vector<int> rad_verses = {100, 100, 100, 50, 10, 10, 0, 0, 0, 100, 100};  // [RadSite]
    int chrono_distance_factor = 48;   // ChronoDistanceFactor (+0xBF4)
    bool chrono_trigger = true;        // ChronoTrigger (+0xBF8)
    int chrono_minimum_delay = 16;     // ChronoMinimumDelay (+0xBFC)
    int chrono_range_minimum = 0;      // ChronoRangeMinimum (+0xC00)
    double c4_delay_minutes = 0.03;    // C4Delay (+0x1750), MINUTES
    int ivan_damage = 450;             // IvanDamage (+0xFCC)
    int ivan_timed_delay = 450;        // IvanTimedDelay (+0xFD0)
    WeaponSpec ivan_warhead = default_ivan_warhead();  // IvanWarhead=IvanWH
};

}  // namespace ra2engine
