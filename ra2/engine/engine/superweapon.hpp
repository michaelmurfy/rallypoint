#pragma once
// CB-4 Superweapon framework [RE: docs/re/superweapon.md §1/§4]: a pure,
// SDL/World/RNG-free per-house recharge-timer lifecycle + the recovered
// superweapon type roster. World embeds a HouseSuperweapon per granted house
// (world.hpp); this header owns only the type-agnostic lifecycle arithmetic.
// The struct methods are header-inline (small scalar math -- the Unit::moving()
// precedent) so any including TU gets them with no link dependency on the .cpp;
// superweapon.cpp holds the enum<->id mapping helpers.
#include <cstdint>

namespace ra2engine {

// The superweapon roster, from the real rulesmd [SuperWeaponTypes]
// [RE-GATE APPLIED: superweapon.md §4]. Ordering follows the RECOVERED binary
// Type name-table at 0x841560 (MultiMissile..PsychicReveal, 12 entries), shifted
// by +1 so our None=0 stays first and Nuke (= the binary's MultiMissile idx 0) is
// the one ACTIVE type. Deltas the pre-hunt list missed: AmerParaDrop is a DISTINCT
// entry (binary idx 6), NOT folded into ParaDrop; SpyPlane is binary idx 8 (after
// PsychicDominator). None = 0 so a value-initialized record is inert. CB-4 acts
// ONLY on Nuke (Type=MultiMissile); every other value is a mapped enum with no
// CB-4 effect (deferred, spec §Out).
enum class SuperweaponType {
    None             = 0,
    Nuke             = 1,   // [NukeSpecial]              Type=MultiMissile  (binary 0) (ACTIVE)
    IronCurtain      = 2,   // [IronCurtainSpecial]       Type=IronCurtain     (binary 1)
    LightningStorm   = 3,   // [LightningStormSpecial]    Type=LightningStorm  (binary 2)
    ChronoSphere     = 4,   // [ChronoSphereSpecial]      Type=ChronoSphere    (binary 3)
    ChronoWarp       = 5,   // [ChronoWarpSpecial]        Type=ChronoWarp      (binary 4)
    ParaDrop         = 6,   // [ParaDropSpecial]          Type=ParaDrop        (binary 5)
    AmerParaDrop     = 7,   // [AmericanParaDropSpecial]  Type=AmerParaDrop    (binary 6, DISTINCT)
    PsychicDominator = 8,   // [PsychicDominatorSpecial]  Type=PsychicDominator(binary 7)
    SpyPlane         = 9,   // [SpyPlaneSpecial]          Type=SpyPlane        (binary 8)
    GeneticConverter = 10,  // [GeneticConverterSpecial]  Type=GeneticConverter(binary 9)
    ForceShield      = 11,  // [ForceShieldSpecial]       Type=ForceShield     (binary 10)
    PsychicReveal    = 12,  // [PsychicRevealSpecial]     Type=PsychicReveal   (binary 11)
};

// The pure per-superweapon recharge lifecycle over plain scalars (spec §2 data
// model). `recharge_secs` is the recovered RechargeTime already converted to
// SECONDS [RE-GATE APPLIED: superweapon.md §1 -- RechargeTime is MINUTES,
// converted at load by trunc(RechargeTime*900) int FRAMES (= *60 s @ 15 fps;
// single FMUL [0x7F3100]=900.0 + _ftol, SWType+0xB0); [NukeSpecial]=10 -> 9000
// frames = 600 s = 10 min. This engine carries the lifecycle in SECONDS, so
// main.cpp's resolve_superweapon does the RechargeTime*60 conversion (the *900
// frame form is *60 s * 15 fps)]. No RNG, no World dep.
struct Superweapon {
    SuperweaponType type = SuperweaponType::None;
    double recharge_secs = 0.0;
    double timer = 0.0;
    bool ready = false;

    // Advance the charge by dt seconds: accumulate, clamp at recharge_secs (no
    // overshoot), and latch `ready` at the threshold. No-op once ready or when
    // uninitialized (type None). A deterministic countdown -- no RNG (§1 item 5).
    void tick(double dt) {
        if (type == SuperweaponType::None || ready) return;
        timer += dt;
        if (timer >= recharge_secs) {
            timer = recharge_secs;   // clamp -- never overshoot the cap
            ready = true;
        }
    }
    bool is_ready() const { return ready; }
    // Consume a fire: full re-charge from 0 [RE-GATE APPLIED: superweapon.md §1
    // -- CONFIRMED firing resets to a FULL recharge (timer->0, ready->false; wait
    // the ENTIRE RechargeTime again), NOT reset-to-partial. SuperClass::Fire
    // (FUN_006cafa0) post-dispatch sets TimeLeft(+0x38)=type+0xB0, StartTime(+0x30)
    // =CurrentFrame; [NukeSpecial] is non-charge-drain so it takes this branch].
    void on_fired() {
        timer = 0.0;
        ready = false;
    }
    // 0..1 charge progress for the sidebar recharge clock (§6). Clamped.
    double charge_fraction() const {
        if (recharge_secs <= 0.0) return ready ? 1.0 : 0.0;
        double f = timer / recharge_secs;
        return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
    }
};

// The display name of a SuperweaponType (witnesses / RE-doc cross-checks).
const char* superweapon_type_name(SuperweaponType t);

// Map a [SuperWeaponTypes] roster id (1..12 in the real rulesmd) to a
// SuperweaponType [RE-GATE APPLIED: superweapon.md §4]. Unknown/absent ids -> None.
SuperweaponType superweapon_type_from_id(int id);

}  // namespace ra2engine
