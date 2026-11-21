#pragma once
// Wave-5 unit and building abilities (agent C, branch w5-abil): the plain data
// the new mechanics need, kept in ONE header so the per-type / per-object /
// per-World additions are three struct members (BuildingTypeDef::w5,
// UnitTypeDef::w5, Unit::w5, Building::w5, World::w5_) instead of dozens of
// loose fields across shared files. Every default is INERT: a World whose
// rosters never fill these (every gate) behaves exactly as before.
//
// RE notes: docs/re/abilities-w5.md.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ability_types.hpp"   // WeaponSpec (P4 building death weapon)

namespace ra2engine {

// Spy purposes (World::InfiltratePurpose as int) beyond the legacy six.
// [RE: abilities-w5.md §1, BuildingClass::Infiltrate FUN_004571E0]
enum : int {
    kSpyStolenAllied = 6,   // BuildTech type with AIBasePlanningSide=0 -> house+0x2BE
    kSpyStolenSoviet = 7,   // AIBasePlanningSide=1 -> house+0x2BD
    kSpyStolenThird = 8,    // any other side -> house+0x2BC
};
// Stolen-tech bits (HouseClass Side0/1/2TechInfiltrated).
enum : std::uint8_t { kStolenAllied = 1, kStolenSoviet = 2, kStolenThird = 4 };

// Per building TYPE (filled by fill_building_abilities from rulesmd).
struct BldTypeW5 {
    int spy_purpose = -1;        // resolved InfiltratePurpose (int); -1 = legacy heuristic
    bool radar = false;          // Radar=yes (+0x16A4)
    bool prism = false;          // this type == [General] PrismType (ATESLA)
    bool overpowerable = false;  // Overpowerable=yes (TESLA)
    int gap_radius = 0;          // GapGenerator=yes -> GapRadiusInCells (0 = not a gap)
    int inf_self_heal = 0;       // InfantryGainSelfHeal= (CATHOSP 1)
    int unit_self_heal = 0;      // UnitsGainSelfHeal= (CAMACH 1)
    bool cloning = false;        // Cloning=yes (NACLON)
    int psychic_radius = 0;      // PsychicDetectionRadius= (NAPSIS)
    int sensors_sight = 0;       // SensorArray=yes -> SensorsSight=
    int delayed_fire_delay = 0;  // art IsAnimDelayedFire=yes -> DelayedFireDelay= (GAPRIS 28)
    // P4 [RE: FUN_00700DA0 death path -> FireDeathWeapon FUN_0070CAC0, shared by
    // every TechnoClass]: Explodes=yes (+0xD15) buildings fire DeathWeapon
    // (+0xD18) where they die (barrels CAMISC01/02, AMMOCRAT: BarrelExplosion);
    // EligibleForDelayKill (BuildingType +0x1551) turns a lethal CausesDelayKill
    // hit into a timed death. Armed by GameScene only (real play, wire_w5).
    bool explodes = false;
    bool delay_kill_eligible = false;
    WeaponSpec death;
    // W7-01-22 [rules CAMISC01/02/06, AMMOCRAT CanC4=no; RE 0x51A2E3..0x51A3F0]:
    // a CanC4=no building refuses a C4 plant (save v35).
    bool can_c4 = true;
};

// Per UNIT TYPE in a producible roster.
struct UnitTypeW5 {
    std::uint8_t requires_stolen = 0;   // kStolen* bits the house must hold
    int base_tech_level = 0;            // TechLevel= before the stolen-tech gate
};

// Per building OBJECT.
struct BldW5 {
    // Prism forwarding [RE: abilities-w5.md §2]: BuildingClass PrismStage
    // (+0x704: 0 idle / 1 master / 2 slave), DelayBeforeFiring (+0x714),
    // SupportingPrisms (+0x664); `master` = the building a slave beams at.
    int prism_stage = 0;
    int delay = 0;
    int supporting = 0;
    int master = -1;
    // Tesla charging [§3]: Overpowerers (+0x66C, trooper unit indices) and
    // IsOverpowered (+0x661).
    std::vector<int> overpowerers;
    bool overpowered = false;
    // Gap Generator [§4]: GeneratingGap (TechnoClass +0x269).
    bool gap_on = false;
};

// Per-unit-TYPE profile bound by image (wave-5 unit abilities).
struct UnitProfileW5 {
    bool assault = false;        // Secondary warhead ElectricAssault=yes (Tesla Trooper)
    int assault_rof = 25;        // that weapon's ROF (AssaultBolt 25)
    bool open_topped = false;    // OpenTopped=yes (Battle Fortress)
    int deploy_frames = 0;       // transport DeployTime= (minutes) * 900, ftol (.022 -> 19)
    // Boris (R-046): AirstrikeTeam= / EliteAirstrikeTeam= planes of
    // AirstrikeTeamType= every AirstrikeRechargeTime= frames.
    int airstrike_team = 0, elite_airstrike_team = 0;
    std::string airstrike_type;
    int airstrike_recharge = 0, elite_airstrike_recharge = 0;
    double flare_range = 0.0;    // the designating weapon's Range (Flare 12)
    // Giant Squid (R-049): the primary warhead's Paralyzes= (the grabbed
    // host cannot move) and Culling= (a host in the red is killed outright).
    bool paralyzes = false;
    bool culling = false;
};

// Per unit OBJECT.
struct UnitW5 {
    bool bound = false;
    UnitProfileW5 p;
    int charging = -1;           // building index this trooper is overpowering
    std::int64_t next_assault = 0;
    std::int64_t door_until = -1;   // transport: unload doors open at this frame
    int defuse_unit = -1;        // engineer: bombed unit to disarm (BombDisarm)
    int defuse_bld = -1;         // ... or bombed building
    bool healer = false;         // current weapon heals (IFV + Engineer RepairBullet)
    int heal_target = -1;        // damaged friendly vehicle being repaired
    // Mirage Tank (R-047): the TerrainType it poses as while disguised
    // (DefaultMirageDisguises pick); the presentation draws it for enemies.
    std::string mirage_tree;
    double base_flight_speed = -1.0;   // aircraft FASTER: unscaled speeds
    double base_fly_cps = -1.0;
    int last_hp = -1;                  // SCATTER: HP at the previous pass
    std::int64_t airstrike_ready = 0;  // Boris: next frame an airstrike may be called
    int strike_of = -1;                // an airstrike plane: the designator (Boris)
    int strike_bld = -1;               // ... and its target building
    std::int64_t c4_frame = -1;        // a ship carrying C4: detonation frame (ability frame)
    int c4_by = -1;
};

// Recovered globals.
struct RulesW5 {
    int prism_support_modifier = 150;   // [General] PrismSupportModifier (percent)
    int prism_support_max = 8;
    int prism_support_delay = 45;       // frames
    int prism_support_duration = 15;
    int self_heal_inf_frames = 50, self_heal_inf_amount = 20;
    int self_heal_unit_frames = 75, self_heal_unit_amount = 5;
    double open_topped_range_bonus = 2.0;
    double open_topped_damage_mult = 1.2;
    std::vector<std::string> mirage_trees;   // [General] DefaultMirageDisguises (TREE01..04)
};

// World-wide state.
struct WorldW5 {
    std::vector<std::uint8_t> stolen_tech;   // per house: kStolen* bits
    bool armed = false;                      // wire_w5 ran (real play only)
    RulesW5 rules;
    std::map<std::string, UnitProfileW5> profiles;   // by image
    std::uint64_t frame = 0;                 // the global Frame of this pass
    bool gap_created = false;                // a gap switched on this frame
    long long passenger_shots = 0;           // OpenTopped shots fired (probe witness)
    long long prism_supports = 0;            // support beams fired (probe witness)
    long long defusals = 0;                  // Ivan bombs disarmed by engineers
    long long repairs = 0;                   // IFV RepairBullet heals
    long long ai_garrisons = 0;              // AI garrison orders (R-081)
    long long ai_tech_captures = 0;          // AI engineer tech-capture orders
    long long scatters = 0;                  // SCATTER ability dodges
    long long airstrikes = 0;                // Boris airstrikes called
    long long culls = 0;                     // Squid Culling kills
    long long ship_c4 = 0;                   // C4 charges planted on ships
};

}  // namespace ra2engine
