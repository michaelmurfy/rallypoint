#pragma once
// Wave-6 fix #2 (missions): per-type / per-object targeting data and the
// usable-area (IsInPlayfield) state. All defaults are INERT: an object is in
// the playfield, a legal target and significant, and no World area is set, so
// a World that never calls World::set_playfield behaves exactly as before.
#include <string>

namespace ra2engine {

// Type data resolved once from the rules (map overrides merged in a campaign)
// by resolve_combat_unit and copied onto Unit::tg.t at add_combat_unit.
struct UnitTypeTgt {
    // W6-02-15: the rules type id (E1), not its art Image= (GI). Empty for a
    // hand-built UnitCombat (tests / old gates).
    std::string rules_id;
    bool insignificant = false;    // Insignificant=   (ObjectType +0x232)
    bool legal_target = true;      // LegalTarget=     (ObjectType +0x231)
    bool immune_psionics = false;  // ImmuneToPsionics= (W6-02-12)
    double guard_range = 0.0;     // (AirRangeBonus= lives in wave-6 #4's UnitTypeW6)      // GuardRange= cells (TechnoType +0x5B8; 0 = unset)
    // --- Wave 7 shared combat (w7fix-combat) ---
    // W7-07-05 [RE: TechnoType +0xD39 DefaultToGuardArea, loader @0x7142F8;
    // Enter_Idle_Mode 0x737BB6 / 0x51CB0E]: idles in Area Guard.
    bool default_to_guard_area = false;
    // W7-03-03 [RE: TechnoType +0x6AF OpportunityFire, loader 0x713BDD; read by
    // the scan permission FUN_00708710 @0x70885F]: scans while moving.
    bool opportunity_fire = false;
    // W7-01-13 [RE: TechnoType +0xDBC IsSelectableCombatant, loader @0x714B09;
    // the P hotkey filter 0x731690]. Default no (ctor @0x7109EB).
    bool selectable_combatant = false;
    // W7-07-09 [RE: TechnoType +0xC96 ToProtect; FUN_00707500 call-for-help].
    bool to_protect = false;
    // W7-07-03 [RE: FUN_0070C190 threat value]: ThreatPosed (+0x2FC) and
    // SpecialThreatValue (+0x310).
    int threat_posed = 0;
    double special_threat_value = 0.0;
    // W7-02-01 [RE: TechnoType +0x6C1 AttackCursorOnFriendlies, loader
    // @0x714603, read at What_Action 0x6FF865].
    bool attack_cursor_on_friendlies = false;
    // W7-07-12 [RE: TechnoType +0x6C8 PreventAttackMove].
    bool prevent_attack_move = false;
    // W7-07-09 cost (the call-for-help value budget, Cost=).
    int cost = 0;
};

// Per-unit state.
struct UnitTgt {
    UnitTypeTgt t;
    // W6-02-08 [RE: TechnoClass +0x3D5 IsInPlayfield -- Unlimbo FUN_006F61D0
    // sets it from IsWithinUsableArea FUN_00577E90, a unit moving in sets it
    // (FUN_006F45F0, false -> true only), ResizeView FUN_00566E60 recomputes
    // every object; Evaluate_Object FUN_006F71D0 rejects a target without it].
    bool in_playfield = true;
    // W6-02-11 [RE: Mission Area_Guard (11) scans with threat 2 (@0x4D4234):
    // FUN_007072E0(1) = 2 x (GuardRange or the larger weapon range), at most
    // 16 cells]: a map object with Mission=Area Guard or a unit the player
    // ordered to Guard (GuardObject FUN_0072FE30 = Area_Guard). Any other order
    // clears it.
    bool area_guard = false;
    // --- Wave 7 shared combat (w7fix-combat): the mission model ---
    // W7-07-02 [RE: TechnoClass +0xAC CurrentMission; FUN_005B3430 returns
    // &MissionControl[mission]]: an EXPLICIT mission that the clone's implicit
    // state (moving / attack_order / area_guard) cannot express -- Sleep (0),
    // Sticky (6), Harmless (23), Attack Move (29). -1 = derived (World::
    // unit_mission).
    int mission = -1;
    // W7-07-08 [RE: Rules+0xE08 NormalTargetingDelay / +0xE04
    // GuardAreaTargetingDelay; 0x708CA0 / 0x4D41A0 tail]: frames until the
    // next threat scan (0 = scan now).
    int scan_timer = 0;
    // W7-03-03: the target was acquired on the move (OpportunityFire / attack
    // move / Aggressive team): fire at will, never approach, never leash.
    bool opportunity = false;
    // W7-07-01 [RE: FootClass::Override_Mission 0x4D8E60]: retaliation saved
    // the old order -- the move goal (x,y; -1 = none) and whether the unit was
    // in area guard -- and resumes it when the target is gone.
    bool overridden = false;
    int resume_x = -1, resume_y = -1;
    bool resume_area_guard = false;
    // W7-07-06 [RE: Mission_Area_Guard FUN_004D41A0 +0x5DC]: the area-guard
    // home cell (-1 = unset: the unit's cell on the next scan).
    int home_x = -1, home_y = -1;
    // W7-07-12: attack-move destination (-1 = none).
    int amove_x = -1, amove_y = -1;
    // W7-07-09 [RE: +0x650]: frame before which no new call for help.
    int help_frame = 0;
    // W7-04-19 / W7-07-12: an Aggressive (non-Suicide) team member scans
    // while it moves; a Suicide team member never retaliates.
    bool team_aggressive = false;
    bool team_suicide = false;
    // W7-07-21 [RE: UnitClass::Crush(cell, warn=1) 0x7404B0]: the cell index
    // (y*w+x) whose occupants this crusher already warned (-1 = none).
    int crush_warn = -1;
    // W7-07-14 / W7-07-15 [RE: GetROF FUN_006FC460 0x6FC55E..0x6FC60C, Fire
    // 0x6FD800]: the active slot's raw ROF, the house ROF / FirePower
    // multipliers and the VeteranROF factor (1 without the ROF ability), so
    // every rearm is ftol(ftol(ROF*houseROF + Random(0,2)) * VeteranROF) and a
    // slot switch keeps the house handicap. w7_rof = armed (World W7 rules).
    bool w7_rof = false;
    int rof_raw = 0;
    double house_rof = 1.0, house_fp = 1.0, rof_vet = 1.0;
};

// Per-building state (the campaign host sets the type flags at placement).
struct BldTgt {
    bool in_playfield = true;
    bool insignificant = false;
    bool legal_target = true;
};

// World-wide: the usable area (Map LocalSize) and the campaign switch of the
// Evaluate_Object Insignificant rule. Set only by the campaign host.
struct PlayfieldW6 {
    bool campaign = false;    // GameMode == campaign (DAT_00A8A1A0 == 0)
    bool valid = false;       // a usable area is configured
    int size_w = 0;           // [Map] Size= width
    int x = 0, y = 0, w = 0, h = 0;   // [Map] LocalSize= (or the last ResizeView)
};

}  // namespace ra2engine
