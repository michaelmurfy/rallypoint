#pragma once
// Wave-6 #4 (units and weapons) per-type / per-object data. Pure (no World,
// no Rules): main.cpp resolves the type half from rulesmd into UnitCombat::w6,
// World::add_combat_unit copies it onto Unit::w6.t, and the runtime half lives
// next to it. Every default is inert (the pre-wave-6 behaviour).

#include <string>

namespace ra2engine {

// Type data resolved once per unit type (docs/gap/wave6/04-units.md).
struct UnitTypeW6 {
    // W6-04-01 [RE: WeaponTypeClass::LoadFromINI Burst -> +0x9C]: the PRIMARY
    // weapon's Burst= (the elite and secondary slots carry their own).
    int burst = 1;
    // W6-04-06 [RE: WeaponType OmniFire +0x12B, aircraft.md §4]: the PRIMARY
    // weapon's OmniFire= (fires without turning to face).
    bool omni_fire = false;
    bool sonic = false;   // W6-04-09: the PRIMARY warhead's Sonic=
    bool bomb_disarm = false;   // the PRIMARY warhead's BombDisarm= (+0x16E; the Engineer's DefuseKit)
    // W6-04-15 [rules key, abilities-w5.md §6]: InfantryType
    // OpenTransportWeapon= -- the slot a passenger fires out of an OpenTopped
    // transport (E1/GGI 1, GHOST/TANY/BORIS 0); -1 = the normal choice.
    int open_transport_weapon = -1;
    // W6-04-16 [RE-FALLBACK: TechnoType AirRangeBonus added to the weapon
    // range against an airborne target]: [FV] AirRangeBonus=4 (cells).
    double air_range_bonus = 0.0;
    // W6-04-29: DeployedCrushable=no (the deployed Guardian GI).
    bool deployed_crushable = true;
    // W6-04-30: CanApproachTarget=no (the Mirage Tank holds its disguise).
    bool can_approach_target = true;
    // W6-04-14 [RE-FALLBACK, rules semantics; MovementZone=CrusherAll]:
    // OmniCrusher= (the Battle Fortress) crushes vehicles too;
    // OmniCrushResistant= (BFRT, MCVs, SMIN) cannot be omni-crushed.
    bool omni_crusher = false;
    bool omni_crush_resistant = false;
    int wall_flags = 0;   // W6-04-04: the PRIMARY warhead's W6WallFlags
    // W6-04-25 [RE: DecloakToFire WeaponType+0x133, read by GetFireError
    // FUN_006FB5D0 @0x6FBF4A]: the primary's DecloakToFire= (default yes);
    // SubTorpedo / SonicZap / SquidGrab say no -- they fire submerged.
    bool decloak_to_fire = true;
    // W6-04-18 [rules [ROBO] PoweredUnit=yes "powered by presence of
    // building", [GAROBO] PowersUnit=ROBO "one of these buildings must be
    // present and powered, or else these guys turn off"]: deactivation is
    // RE-FALLBACK (the unit stops: no move, no fire).
    bool powered_unit = false;
};

// Per-unit state.
struct UnitW6 {
    UnitTypeW6 t;
    // W6-04-01 [RE: TechnoClass::Fire FUN_006FD210 @0x6FD8xx, GetROF
    // FUN_006FC460]: the ACTIVE weapon's Burst= (swapped with w_rof) and the
    // techno's CurrentBurstIndex (+0x3B8).
    int burst = 1;
    int burst_index = 0;
    // W6-04-06: the ACTIVE weapon's OmniFire= (swapped with w_rof; the
    // type-level Unit::omni_fire of fliers stays as it was).
    bool omni_fire = false;
    // W6-04-09 [RE: FootClass::ReceiveDamage FUN_004D7250]: the ACTIVE
    // weapon's warhead Sonic= (+0x14B, the Dolphin), and the per-frame marks
    // a hit leaves on a parasite host -- a Sonic hit or a healing hit makes
    // the parasite exit (ParasiteClass::ExitUnit FUN_00629F40).
    bool sonic = false;
    bool sonic_hit = false;
    bool heal_hit = false;
    int wall_flags = 0;   // W6-04-04: the ACTIVE warhead's W6WallFlags
    bool unpowered = false;   // W6-04-18: a PoweredUnit with no powered PowersUnit building
    // W8-04-05 [RE: HoverLocomotionClass Power_Off FUN_00516A10 +0x68]: the
    // signed spin counter (+-Random(20,30), 0 = not spinning) of a Hover
    // unit switched off (save v42).
    int hover_spin = 0;
};

// Per-building state.
struct BldW6 {
    int burst_index = 0;   // W6-04-01: a defence's CurrentBurstIndex
    int wall_stage = 0;    // W6-04-04: a built wall's damage stage (OverlayData >> 4)
    int def_target_building = -1;   // W6-04-13: a defence's structure target (-1 none)
};

// W6-04-04/05 [RE: DamageArea FUN_00489190 @0x489581..0x4895BD]: the warhead
// keys that let a blast touch a wall -- Wall (+0x144), Wood (+0x147, only a
// wall whose Armor is wood) and WallAbsoluteDestroyer (+0x145).
enum W6WallFlags : int { kW6Wall = 1, kW6Wood = 2, kW6WallAbs = 4 };

// Per-building-TYPE data (BuildingTypeDef::w6).
struct BldTypeW6 {
    // W6-04-04: a Wall=yes type's art DamageLevels= (GAWALL/NAWALL 3); 0 =
    // not a wall. A wall has no HP model: it takes damage stages.
    int wall_levels = 0;
    // W6-04-18: PowersUnit= -- the unit IMAGE this building keeps active ("" none).
    std::string powers_unit;
    // R-210: Gate=yes (GAGATE_A / NAGATE_A ...) [RE: IsCellOccupied reads the
    // building type's +0x16B7 flag -> the gate opens for an allied mover].
    bool gate = false;
};

// W6-04-05: one wall OverlayType (rules Strength=, art DamageLevels=, Armor=wood).
struct WallOvType {
    int strength = 1;
    int levels = 1;
    bool wood = false;
};

// W6-04-04 [RE: Reduce_Wall FUN_00480BC0]: one hit of `dmg` (-1 = destroy,
// WallAbsoluteDestroyer) on a wall at damage `stage`. When dmg < Strength the
// stage advances only if Random(0, Strength) < dmg (the sync RNG); then
// stage+1. Reaching DamageLevels-1 (with DamageLevels > 2) cascades
// Reduce_Wall(200) onto the N/E/S/W neighbours of the same type still at stage
// 0. The wall is gone at stage >= DamageLevels, or at DamageLevels-1 when it
// has no neighbour of its type (connection nibble 0).
struct WallStep {
    int stage = 0;
    bool advanced = false;
    bool destroyed = false;
    bool cascade = false;
};

}  // namespace ra2engine
