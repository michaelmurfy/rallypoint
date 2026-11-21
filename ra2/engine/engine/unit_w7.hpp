#pragma once
// Wave-7 fix (units) per-type / per-object data (docs/gap/wave7/01-04, 08,
// 10). Pure (no World, no Rules): the type half is resolved from rulesmd into
// UnitCombat::w7 (fill_unit_combat_extras), World::add_combat_unit copies it
// onto Unit::w7.t, and the runtime half lives next to it. Every default is
// inert (the pre-wave-7 behaviour), so a hand-built UnitCombat in an old test
// or gate behaves exactly as before.

#include <cstdint>
#include <string>
#include <vector>

namespace ra2engine {

// Type data resolved once per unit type.
struct UnitTypeW7 {
    // W7-01-33 [RE: TechnoTypeClass::LoadFromINI FUN_00711510]: the role flags
    // the spy / engineer orders key off (the clone used to test the SHP image
    // name "SPY" / "ENGINEER").
    bool agent = false;        // Agent=       (the Spy: disguise + infiltrate)
    bool infiltrate = false;   // Infiltrate=  (may enter enemy buildings)
    bool engineer = false;     // Engineer=    (capture / repair / defuse)
    // W7-01-03 [RE: 0x6F34A0 L597-612]: CanDisguise && PermaDisguise -> the
    // unit starts disguised as the side's default disguise in its own house.
    bool perma_disguise = false;
    // W7-01-02 [RE: 0x6F71D0 L455-467, type +0xD31]: DetectDisguise= -- the
    // threat scan of a type without it skips a disguised object.
    bool detect_disguise = false;
    // W7-01-13 [RE: 0x731690, type +0xDBC]: IsSelectableCombatant=.
    bool selectable_combatant = false;
    // W7-01-26: MoveToShroud= (default yes; CLEG / CCOMAND no).
    bool move_to_shroud = true;
    // W7-01-32 / W7-02-08 / W7-04-17 [RE: 0x51CB2A / 0x737BB6, +0xD39]:
    // DefaultToGuardArea= -- an idle unit outside a team enters Area_Guard.
    bool default_to_guard_area = false;
    // W7-03-02 [RE: 0x735A60 @0x735A9C, type +0xCA1]: Turret= -- a turretless
    // vehicle turns its hull to the target before it fires.
    bool has_turret = true;
    // W7-03-04 [RE: 0x740150, WeaponType +0x13C]: the primary / elite
    // primary weapon's DisguiseFakeBlinkTime= (MirageGun 15, MirageGunE 5).
    int fake_blink = 0;
    int fake_blink_elite = 0;
    // W7-10-06 [RE: 0x51F3F0]: an AreaFire deploy weapon (PsiWave) fires once
    // at the own cell and the unit stands up again after UndeployDelay= frames
    // (-1 = never).
    bool deploy_area_fire = false;
    int undeploy_delay = -1;
    // W7-10-02 [RE: CaptureManager ctor 0x4716F0]: the mind-control capacity
    // = the primary weapon's Damage; InfiniteMindControl= lifts the cap.
    int mc_capacity = 1;
    bool mc_infinite = false;
    // W7-04-03 [RE: Time_To_Build FUN_006F3D00 @0x6F3D4E, type +0x608]:
    // BuildTimeMultiplier= (HTNK / MTNK / LTNK 1.5, TTNK / ROBO 1.3).
    double build_time_mult = 1.0;
};

// Per-unit state.
struct UnitW7 {
    UnitTypeW7 t;
    // W7-03-04 [RE: 0x740150 / 0x7456F0]: the Mirage's fake-blink end frame
    // (+0x1EC timer) and the re-disguise delay after an adjacent non-allied
    // infantry revealed it (+0x1E0, InfantryBlinkDisguiseTime).
    int blink_left = 0;        // frames of fake blink left (the tank shows)
    int redisguise_left = 0;   // frames until it may disguise again
    bool flash_seen = false;   // the previous frame's fire_flash (shot edge)
    // W7-03-02: the hull is turning toward the target (turretless vehicle).
    bool hull_aiming = false;
    // W7-04-13 [RE: ExitUnit 0x62A2A0]: a parasite thrown off alive is
    // paralysed (ParalysisTimer +0x6A0 = weapon ROF x 3) until this frame.
    std::int64_t paralysed_until = -1;
    // W7-04-14 [RE: FootClass::ReceiveDamage 0x4D7250]: this frame's largest
    // RAW (pre-armour) damage from anything but the riding parasite.
    int host_raw_hit = 0;
    // W7-01-31 / W7-08-22 [CONFIRMED-DATA Deploy=300,15,0]: the deploy
    // sequence ends (the deploy weapon is live) at this frame.
    std::int64_t deploy_ready_at = -1;
    // W7-10-06: the deployed Yuri stands up again at this frame (-1 none).
    std::int64_t undeploy_at = -1;
    // W7-01-10 [RE: 0x719A70]: the legionnaire that erased this unit (the
    // VeterancyStruct::Add credit), -1 none.
    int erased_by = -1;
    // W7-08-01 / W7-04-06: the Service Depot this vehicle was sent to (-1 none).
    int depot_target = -1;
    // W7-10-02/03: the controller holding this unit (-1 none) -- the
    // original's MindControlledBy (+0x2C0) -- and a controller's victims.
    int mc_controller = -1;
    std::vector<int> mc_victims;
    int mc_shot = -1;   // W7-10-04: the victim of this frame's Controller shot
    // (W7-04-08's per-unit missile_holds list was removed in fv7c / save v40:
    // the PauseFrames + TiltFrames hold lives in the spawn-manager node,
    // AnUnit::sp_state / sp_timer [RE: SpawnManagerClass::AI FUN_006B68B0 / 0x6B72E0].)
};

// Per-building state.
struct BldW7 {
    // W7-08-03 [RE: BuildingClass::Mission_Repair 0x44B780, +0x620]: the
    // depot's own URepairRate accumulator (next paid step frame).
    std::int64_t depot_next = -1;
    // W7-08-03: the depot client last served (EVA_UnitRepaired edge).
    int depot_client = -1;
    // W7-01-11 [RE: 0x71A1A0 -> 0x4521B0]: an erased building has no power.
    bool warp_unpowered = false;
    // W7-02-10: a garrison's structure target (-1 none) -- player-ordered or
    // auto-acquired by the defence rule (W6-04-13) when no unit is in range.
    int garrison_target_building = -1;
};

}  // namespace ra2engine
