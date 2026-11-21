#pragma once
// Gap fix C (G-41) -- TechnoType weapon slots from rulesmd, the way the
// type loader FUN_00711510 fills them (docs/re/weapons.md §1):
//   TurretCount < 1 : Primary -> slot 0, Secondary -> slot 1,
//                     ElitePrimary / EliteSecondary -> elite slots 0 / 1
//                     (all skipped when ClearAllWeapons=yes);
//   TurretCount >= 1 and WeaponCount > 0 : WeaponN -> slot N-1,
//                     EliteWeaponN -> elite slot N-1 (SREF: Weapon1=Comet).
#include <string>

#include "weapon.hpp"
#include "unit_combat.hpp"
#include "building.hpp"

namespace ra2assets { class Rules; }

namespace ra2engine {

struct TypeWeapons {
    WeaponSlot slot[2];        // normal slots 0 / 1
    WeaponSlot elite[2];       // elite slots 0 / 1 (valid only when defined)
    int deploy_fire_weapon = 1;  // DeployFireWeapon= (def 1) -- docs/re/weapons.md §3
};

// One weapon id -> WeaponSlot (stats + warhead Verses/CellSpread/PercentAtMax +
// projectile spec + projectile AA/AG + OmniFire). Unknown/empty id -> !valid.
WeaponSlot resolve_weapon_slot(const ra2assets::Rules& rules, const std::string& weapon);

// The two slots (+ elite) of a unit or building type.
TypeWeapons resolve_type_weapons(const ra2assets::Rules& rules, const std::string& type_id);

// The weapon id in slot `i` (0/1) per the loader rule above ("" if none).
// W3-B: a warhead's ProneDamage= as a multiplier (absent -> 1.0).
double warhead_prone_damage(const ra2assets::Rules& rules, const std::string& wh);
// W6-04-04: the warhead's Wall / Wood / WallAbsoluteDestroyer as W6WallFlags.
int warhead_wall_flags(const ra2assets::Rules& rules, const std::string& wh);
std::string type_weapon_id(const ra2assets::Rules& rules, const std::string& type_id, int i,
                           bool elite);

// Fill the Gap-fix-C UnitCombat fields from the resolved slots + type keys:
// secondary/elite secondary (FLH copied from the primary's -- SecondaryFireFLH
// is not modelled), primary AA/AG, the elite projectile, DeployFire
// (Deployer=yes DeployFire=yes whose deploy weapon has no RadLevel -- the GI
// Para / GGI MissileLauncher; the Desolator's RadEruption stays a P4
// ability), Crusher= / Crushable=.
void fill_unit_combat_extras(const ra2assets::Rules& rules, const std::string& type_id,
                             const TypeWeapons& tw, UnitCombat& c);

// Wave-7 fix (units): the UnitTypeW7 keys (role flags, disguise, Turret=,
// Mirage blink, AreaFire deploy, mind-control capacity). Called by
// fill_unit_combat_extras.
void fill_unit_w7(const ra2assets::Rules& rules, const std::string& type_id, const TypeWeapons& tw,
                  UnitTypeW7& w);

// Gap fix C (G-40) [RE: docs/re/defenses.md]: a building type's weapons
// (same slot rule as units), Powered=, ROT=, Turret=, GuardRange=.
void fill_building_weapons(const ra2assets::Rules& rules, const std::string& type_id,
                           BuildingTypeDef& d);

}  // namespace ra2engine
