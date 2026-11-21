#pragma once
#include "ra2assets/ini.hpp"
#include <string>
#include <vector>

namespace ra2assets {

// Thin, minimal lookup for the unit/weapon/warhead fields the engine needs
// from rulesmd.ini: art (image), movement (speed), and combat (strength/armor/
// primary + weapon damage/rof/range/warhead/report + warhead verses). Still
// deliberately NOT a general rules.ini model -- fields land only when the sim
// needs them (YAGNI).
//
// Rules wraps an already-parsed Ini by reference (not by copy) to stay
// "thin" -- the caller must keep the Ini alive for at least as long as the
// Rules object built from it.
class Rules {
public:
    static Rules load(const Ini& ini);

    // Art image (SHP base name) for a unit id, e.g. "E1" -> "GI" (then
    // GI.SHP). Reads [unit_id] Image=; falls back to unit_id itself if the
    // section/key is absent (this also covers unknown unit ids).
    std::string image(const std::string& unit_id) const;

    // [unit_id] Speed=, or 0 if the section/key is absent (unknown unit ids
    // included).
    int speed(const std::string& unit_id) const;

    // [unit_id] Strength=, or 0 if the section/key is absent.
    int strength(const std::string& unit_id) const;

    // Shroud/Fog phase [RE: shroud.md §2]: Sight= in whole cells, hard-clamped
    // to 10 (the recovered "Vegas crash" cap). 0 when absent (no [General]
    // default). A plain stat like strength/speed -- not read by world_hash.
    int sight(const std::string& id) const;

    // Shroud/Fog phase [RE: shroud.md §7A]: [id] SpySat= (yes/true). True for a
    // whole-map reveal provider (Allied GASPYSAT SpySat=yes); false when absent.
    // Tolerant yes/true parse (SpySat=yes cannot go through section_double).
    bool spy_sat(const std::string& id) const;

    // [unit_id] Armor=, or "none" if absent.
    std::string armor(const std::string& unit_id) const;

    // [unit_id] Primary=, or "" if absent.
    std::string primary(const std::string& unit_id) const;

    // [unit_id] VoiceSelect=, or "" if absent -- soundmd.ini section name for
    // the "unit selected" voice line (e.g. E1 -> "GISelect").
    std::string voice_select(const std::string& unit_id) const;

    // [unit_id] VoiceMove=, or "" if absent -- soundmd.ini section name for
    // the "move order acknowledged" voice line.
    std::string voice_move(const std::string& unit_id) const;

    // [unit_id] VoiceAttack=, or "" if absent -- soundmd.ini section name for
    // the "attack order acknowledged" voice line.
    std::string voice_attack(const std::string& unit_id) const;

    // [unit_id] DieSound=, or "" if absent -- soundmd.ini section name for the
    // unit's death voice line.
    std::string die_sound(const std::string& unit_id) const;

    // [weapon] Damage=, or 0 if absent.
    int weapon_damage(const std::string& weapon) const;

    // [weapon] ROF=, or 0 if absent.
    int weapon_rof(const std::string& weapon) const;

    // [weapon] Range=, or 0 if absent. A double (not int) because real
    // weapons use fractional cell ranges, e.g. 120mm's Range=5.75 -- an int
    // truncated that to 5.
    double weapon_range(const std::string& weapon) const;

    // [weapon] Warhead=, or "" if absent.
    std::string weapon_warhead(const std::string& weapon) const;

    // [weapon] Report=, or "" if absent.
    std::string weapon_report(const std::string& weapon) const;

    // Projectile phase (P2) [RE: projectile.md §2]: [weapon] Projectile= (the
    // [Projectile] section name, e.g. 120mm -> "Cannon"), or "" if absent.
    std::string weapon_projectile(const std::string& weapon) const;

    // Projectile phase (P2) [RE: projectile.md §0.2/§6.2]: [weapon] Speed= as
    // the raw ini integer (0 if absent). NOT a launch speed for ROT==0
    // projectiles -- only the Vertical/homing cap after weapon_speed_leptons.
    int weapon_speed(const std::string& weapon) const;

    // [warhead] Verses= parsed into percent integers; empty vector if absent.
    // Tolerates spaces and missing '%' signs in the comma-separated list.
    std::vector<int> verses(const std::string& warhead) const;

    // [unit_id] ROT= (rotation speed, in game "steps per tick" units), or 0
    // if absent.
    int rot(const std::string& unit_id) const;

    // [unit_id] Turret=, true iff the value is "yes"/"true" (case-
    // insensitive); false if absent.
    bool has_turret(const std::string& unit_id) const;

    // First comma-separated entry of [unit_id] Explosion= (the VoxelAnim/
    // animation name used for the unit's death explosion), or "" if absent.
    std::string explosion_first(const std::string& unit_id) const;

    // [id] Cost=, or 0 if absent.
    int cost(const std::string& id) const;

    // [id] Power=, or 0 if absent (negative = drain, positive = generation).
    int power(const std::string& id) const;

    // [id] Adjacent=, or 0 if absent.
    int adjacent(const std::string& id) const;

    // [id] TechLevel=, or 0 if absent.
    int tech_level(const std::string& id) const;

    // [id] ConstructionYard=, true iff the value is "yes"/"true" (case-
    // insensitive); false if absent.
    bool is_construction_yard(const std::string& id) const;

    // [id] Prerequisite= split on commas, whitespace-trimmed and uppercased;
    // empty if absent.
    std::vector<std::string> prerequisite(const std::string& id) const;

    // [General] Prerequisite<group>= (e.g. group="Power" reads
    // PrerequisitePower=) split on commas, whitespace-trimmed and
    // uppercased; empty if absent.
    std::vector<std::string> prerequisite_group(const std::string& group) const;

    // [General] key= parsed as a double, tolerating a trailing '%' (percent
    // values are returned as given, e.g. "50%" -> 50.0). Falls back to
    // [AudioVisual] key= if the key isn't in [General] -- ConditionYellow/
    // ConditionRed live there in the real rulesmd.ini (loaded by
    // FUN_00668C20's [AudioVisual] reader, per docs/re/building-render.md
    // §4), while BuildSpeed/RefundPercent/LowPowerPenaltyModifier live in
    // [General]; this getter serves all five known keys regardless of which
    // section actually holds them. Returns `fallback` if absent from both.
    double general_double(const std::string& key, double fallback) const;

    // [General] key= as a STRING id with the inline ';' comment and trailing
    // whitespace stripped (real rulesmd carries e.g. "AlliedCrew=E1   ; soldier
    // that emerges..."). Returns `fallback` if absent or empty. Used for the
    // survivor crew ids [RE: repair-sell.md §D1: AlliedCrew=E1/SovietCrew=E2].
    std::string general_string(const std::string& key, const std::string& fallback) const;

    // [section] key= parsed as a double (same tolerant parse as
    // general_double), for the [AI] and difficulty sections the AI phase
    // reads [RE: ai-base.md §2/§4]. Returns `fallback` if absent.
    double section_double(const std::string& section, const std::string& key,
                          double fallback) const;

    // Generic per-section BOOLEAN key with an explicit fallback. Same shape as
    // section_double, but yes/1/true-style values via the same is_truthy the
    // dedicated flag readers (is_construction_yard, spy_sat) already use.
    // Needed because the Repair & Sell phase reads THREE per-type flags whose
    // absent-key defaults DIFFER: Unsellable -> false, Crewed -> false,
    // ClickRepairable -> TRUE [RE: repair-sell.md §A4/§B3/§B5].
    bool section_flag(const std::string& id, const std::string& key, bool fallback) const;

    // Generic per-section STRING key: the value with any inline ';' comment
    // removed and whitespace trimmed (the Ini keeps inline comments), or
    // `fallback` if absent/empty. P5 Veterancy +
    // Crates [RE: veterancy.md §0] reads VeteranAbilities/EliteAbilities lists,
    // ElitePrimary and the [Powerups] "weight,anim,naval,arg" tuples with it.
    std::string section_string(const std::string& section, const std::string& key,
                               const std::string& fallback) const;

    // [MultiplayerDialogSettings] Money=, fallback 10000 if absent.
    int mp_money() const;

    // [id] Value=, or 0 if absent.
    int value(const std::string& id) const;

    // [id] GrowthPercentage=, or 0.0 if absent.
    double growth_percentage(const std::string& id) const;

    // [id] SpreadPercentage=, or 0.0 if absent.
    double spread_percentage(const std::string& id) const;

    // [id] Storage=, or 0 if absent.
    int storage(const std::string& id) const;

    // [id] Harvester=, true iff the value is "yes"/"true" (case-insensitive);
    // false if absent.
    bool harvester(const std::string& id) const;

    // [id] Dock= split on commas, whitespace-trimmed and uppercased; empty if
    // absent.
    std::vector<std::string> dock(const std::string& id) const;

    // [id] FreeUnit=, or "" if absent.
    std::string free_unit(const std::string& id) const;

    // [id] Locomotor= (the CLSID string), or "" if absent [RE: aircraft.md §1].
    std::string locomotor(const std::string& id) const;

    // [id] SpeedType= (Foot/Track/Wheel/Hover/Float/FloatBeach/Amphibious), or
    // "" if absent [RE: naval.md §2]. Diagnostic/log use in CB-3 (the naval
    // domain is decided by the Locomotor CLSID); the SpeedType speed table is a
    // carry-forward.
    std::string speed_type(const std::string& id) const;

    // [id] BalloonHover=, true iff "yes"/"true" (case-insensitive) [aircraft.md §2].
    bool balloon_hover(const std::string& id) const;

    // [weapon] OmniFire=, true iff "yes"/"true" (case-insensitive) [aircraft.md §4].
    bool weapon_omnifire(const std::string& weapon) const;

    // [id] NumberOfDocks=, or 0 if absent.
    int number_of_docks(const std::string& id) const;

    // [General] GrowthRate=, or 5.0 if absent.
    double growth_rate_minutes() const;

    // [id] UnloadingClass=, or "" if absent.
    std::string unloading_class(const std::string& id) const;

    // Producible category [RE-GATE APPLIED: production.md §1.1]. Resolved from
    // INI list-section membership plus the recovered BuildCat= key -- the
    // Structure-vs-Defense queue classifier is BuildCat==Combat, NOT
    // IsBaseDefense (GTGCAN/GAWALL are BuildCat=Combat with IsBaseDefense=None):
    //   [BuildingTypes] member + BuildCat=Combat        -> Defense
    //   [BuildingTypes] member otherwise                -> Structure
    //                    (BuildCat Power/Resource/Tech/None)
    //   [InfantryTypes] member                          -> Infantry
    //   [VehicleTypes]  member + Naval!=yes             -> Vehicle
    //   [VehicleTypes]  member + Naval=yes              -> None (Ship queue,
    //                    out of scope this phase, production.md §1.1)
    //   anything else (aircraft/unknown)                -> None
    // Membership is by VALUE (the id, e.g. "GI"/"E1"): the list sections map
    // numbered keys ("0=E1") to ids, so we scan values, not keys.
    enum class BuildCategory { None, Structure, Defense, Infantry, Vehicle };
    BuildCategory build_category(const std::string& id) const;

    // [id] appears as a value in [InfantryTypes] / [VehicleTypes] respectively.
    bool is_infantry(const std::string& id) const;
    bool is_vehicle(const std::string& id) const;

    // [id] BuildCat=, true iff the value is "Combat" (case-insensitive); false
    // if absent [RE-GATE APPLIED: production.md §1.1 -- BuildCat==Combat(5) is
    // the recovered Structure-vs-Defense queue classifier (BuildingType +0xE08,
    // branch in FUN_004FA170); IsBaseDefense (+0x1706) is a separate
    // AI-targeting flag and is NOT the queue key].
    bool is_buildcat_combat(const std::string& id) const;

private:
    explicit Rules(const Ini& ini) : ini_(&ini) {}
    const Ini* ini_;
};

} // namespace ra2assets
