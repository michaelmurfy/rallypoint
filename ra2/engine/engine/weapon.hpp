#pragma once
// Gap fix C (G-41/G-44/G-50) -- one resolved weapon slot and the per-target
// weapon choice. Pure (no World, no Rules): engine/weapon_resolve.cpp fills
// these from rulesmd, combat.cpp / building_combat.cpp consume them.
#include <string>
#include <vector>

#include "projectile.hpp"  // ProjectileSpec

namespace ra2engine {

// One TechnoType weapon slot (Weapon[i] / EliteWeapon[i] in the type,
// stride 0x1C at type+0x898 / +0xA94 -- docs/re/weapons.md §1).
struct WeaponSlot {
    bool valid = false;            // a weapon id resolved to a [section]
    std::string id;                // "M60", "Comet", ...
    int damage = 0;
    int rof = 0;                   // logic frames
    double range = 0.0;            // cells
    std::vector<int> verses;       // warhead Verses percents
    double cell_spread = 0.0;
    double percent_at_max = 1.0;
    bool affects_allies = true;    // W5-B R-060: warhead AffectsAllies= (WarheadType +0x179)
    ProjectileSpec proj;           // projectile flight (P2)
    bool aa = false;               // projectile AA= (can hit airborne)
    bool ag = true;                // projectile AG= (can hit ground)
    bool omni_fire = false;        // weapon OmniFire=
    bool wall = false;             // warhead Wall= (G-49: may break bridges, FUN_00489190)
    // W3-B [RE: InfantryClass::ReceiveDamage FUN_00517db5]: the warhead's
    // InfDeath= (death anim / Die sequence of a killed infantry; absent = 0)
    // and ProneDamage= (damage multiplier on a prone infantry; absent = 1).
    int inf_death = 0;
    double prone_damage = 1.0;
    int burst = 1;                 // W6-04-01: weapon Burst= (+0x9C)
    bool sonic = false;            // W6-04-09: warhead Sonic= (+0x14B)
    bool bomb_disarm = false;      // warhead BombDisarm= (+0x16E): GetFireError ILLEGAL on an unbombed target
    int wall_flags = 0;            // W6-04-04: warhead Wall/Wood/WallAbsoluteDestroyer
    double min_range = 0.0;        // W6-04-22: weapon MinimumRange= (+0xB8, cells)
    // W6-04-02 [RE: BulletTypeClass ShrapnelWeapon/ShrapnelCount -- keys of the
    // weapon's PROJECTILE ([LargeCometP], [Electricbounce], [SuperCometP])]:
    // the fragment weapon's Damage/Range/Verses and the fragment count.
    int shrapnel_count = 0;
    int shrapnel_damage = 0;
    double shrapnel_range = 0.0;
    std::vector<int> shrapnel_verses;
    // W7-01-01 [rules [VirtualScanner] NeverUse=yes]: a weapon that only widens
    // the scan; it is never chosen to fire (save v35).
    bool never_use = false;
};

// GetFireError's warhead gate [RE: combat.md §2, FUN_006fb5d0]: a weapon whose
// Verses vs the target's armor is 0 % cannot fire at it. Empty Verses = 100 %.
bool verses_allows(const std::vector<int>& verses, int armor_ix);

// Can `w` engage a target that is airborne (`air`) with armor `armor_ix`?
// AA/AG from the projectile (ThreatFlags, FUN_007718b0) + the Verses gate.
bool slot_can_target(const WeaponSlot& w, bool air, int armor_ix);

// The per-target weapon choice used by units and buildings: the primary
// (slot 0) when it can engage, else the secondary (slot 1), else -1.
// [RE-FALLBACK: docs/re/weapons.md §2 -- TechnoClass::SelectWeapon's body was
// not located in this build; this is the GetFireError-feasibility order.]
int choose_weapon(const WeaponSlot& primary, const WeaponSlot& secondary, bool air,
                  int armor_ix);

}  // namespace ra2engine
