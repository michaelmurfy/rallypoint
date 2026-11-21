#include "inf_anim.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

#include <algorithm>
#include <cmath>

namespace ra2engine {

const char* inf_seq_name(int seq) {
    static const char* kNames[kSqCount] = {
        "Ready", "Guard", "Prone", "Walk", "FireUp", "Down", "Crawl", "Up", "FireProne", "Idle1", "Idle2",
        "Die1", "Die2", "Die3", "Die4", "Die5", "Tread", "Swim", "WetIdle1", "WetIdle2", "WetDie1",
        "WetDie2", "WetAttack", "Hover", "Fly", "Tumble", "FireFly", "Deploy", "Deployed", "DeployedFire",
        "DeployedIdle", "Undeploy", "Cheer", "Paradrop", "AirDeathStart", "AirDeathFalling",
        "AirDeathFinish", "Panic", "Shovel", "Carry", "SecondaryFire", "SecondaryProne"};
    return seq >= 0 && seq < kSqCount ? kNames[seq] : "Nothing";
}

const InfDoControl& inf_do_control(int seq) {
    // Dumped from gamemd.exe 0x7E9F7C (42 x {Interrupt, IsMobile, RandomStart, Rate}).
    static const InfDoControl kTable[kSqCount] = {
        {1, 0, 0, 0}, {1, 0, 0, 0}, {1, 0, 0, 6}, {1, 1, 1, 3}, {1, 0, 0, 1}, {0, 1, 0, 1}, {1, 1, 1, 1},
        {0, 0, 0, 1}, {1, 0, 0, 1}, {1, 0, 0, 3}, {1, 0, 0, 3}, {0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 1},
        {0, 0, 0, 1}, {0, 0, 0, 1}, {1, 0, 0, 3}, {1, 1, 1, 1}, {1, 0, 0, 3}, {1, 0, 0, 3}, {0, 0, 0, 1},
        {0, 0, 0, 1}, {1, 0, 0, 1}, {1, 1, 0, 2}, {1, 1, 0, 1}, {1, 1, 0, 1}, {1, 1, 0, 1}, {0, 0, 0, 1},
        {1, 0, 0, 1}, {1, 0, 0, 1}, {1, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 3}, {1, 0, 0, 1}, {0, 0, 0, 3},
        {0, 0, 0, 1}, {0, 0, 0, 3}, {1, 1, 1, 4}, {1, 0, 0, 6}, {1, 1, 1, 3}, {1, 0, 0, 1}, {1, 0, 0, 1}};
    static const InfDoControl kNone{1, 0, 0, 0};
    return seq >= 0 && seq < kSqCount ? kTable[seq] : kNone;
}

namespace {

std::uint32_t next_rng(InfAnimState& s) {
    s.rng = s.rng * 1664525u + 1013904223u;
    return s.rng >> 8;
}
// Random(lo, hi) inclusive on the unit's own render RNG.
int rnd(InfAnimState& s, int lo, int hi) {
    return lo + static_cast<int>(next_rng(s) % static_cast<std::uint32_t>(hi - lo + 1));
}

bool is_die(int q) {
    return (q >= kSqDie1 && q <= kSqDie5) || q == kSqWetDie1 || q == kSqWetDie2 || q == kSqAirDeathStart ||
           q == kSqAirDeathFalling || q == kSqAirDeathFinish;
}
bool is_deploy_group(int q) {   // 27..30 [FUN_00520810: Deploy/Deployed/DeployedFire/DeployedIdle]
    return q >= kSqDeploy && q <= kSqDeployedIdle;
}

// The PlayAnim substitutions [RE: FUN_0051d4c0]: in water an amphibious
// soldier swaps to the Wet/Swim set; Walk becomes Panic above fear 199; and
// (clone) a missing entry falls back to its nearest dry/standing kin.
int resolve(const InfAnimState& s, const InfTable& t, int q, bool in_water) {
    // W7-01-21 / W7-12-40 [RE: PlayAnim 0x51D4C0 -- Ready becomes Hover when
    // airborne and Hover is defined; 0x520C70 -- moving with the jumpjet
    // locomotor: Fly (Hover at <= 80 % speed); 0x5203E0 -- firing: FireFly;
    // ReceiveDamage in the air: AirDeathStart]:
    if (s.air) {
        auto has = [&](int k) { return t.e[k].has && t.e[k].count > 0; };
        switch (q) {
            case kSqReady: case kSqGuard: case kSqProne: case kSqIdle1: case kSqIdle2:
                if (has(kSqHover)) return kSqHover;
                break;
            case kSqWalk: case kSqCrawl: case kSqPanic:
                if (has(kSqFly)) return kSqFly;
                if (has(kSqHover)) return kSqHover;
                break;
            case kSqFireUp: case kSqFireProne: case kSqSecondaryFire: case kSqSecondaryProne:
                if (has(kSqFireFly)) return kSqFireFly;
                break;
            case kSqDie1: case kSqDie2:
                if (has(kSqAirDeathStart)) return kSqAirDeathStart;
                break;
            default: break;
        }
    }
    if (in_water && t.e[kSqSwim].has) {
        switch (q) {
            case kSqWalk: case kSqCrawl: q = kSqSwim; break;
            case kSqReady: case kSqProne: case kSqGuard: q = kSqTread; break;
            case kSqIdle1: q = kSqWetIdle1; break;
            case kSqIdle2: q = kSqWetIdle2; break;
            case kSqDie1: q = kSqWetDie1; break;
            case kSqDie2: q = kSqWetDie2; break;
            case kSqFireUp: case kSqFireProne: q = kSqWetAttack; break;
            default: break;
        }
    }
    if (q == kSqWalk && s.fear > 199 && t.e[kSqPanic].has) q = kSqPanic;
    // Fallbacks for entries the art does not define (clone safety net).
    for (int guard = 0; guard < 4 && !t.e[q].has; ++guard) {
        switch (q) {
            case kSqPanic: case kSqCrawl: case kSqSwim: q = kSqWalk; break;
            case kSqProne: case kSqTread: case kSqGuard: case kSqDeployed: case kSqDeployedIdle: q = kSqReady; break;
            case kSqFireProne: case kSqWetAttack: q = kSqFireUp; break;
            case kSqWetIdle1: q = kSqIdle1; break;
            case kSqWetIdle2: q = kSqIdle2; break;
            case kSqWetDie1: case kSqDie2: case kSqDie3: case kSqDie4: case kSqDie5: q = kSqDie1; break;
            case kSqWetDie2: q = kSqDie2; break;
            default: return q;   // keep (drawn from the default entry)
        }
    }
    return q;
}

void play(InfAnimState& s, const InfTable& t, int q, bool in_water, bool force) {
    q = resolve(s, t, q, in_water);
    if (q == s.seq) return;
    // W7-12-36 [RE: FUN_0051d4c0 head -- `if (Sequence[q].CountFrames == 0)
    // return 0`]: a sequence the art defines with no frames (GI/GGI
    // DeployedIdle=0,0,0) or does not define at all (a deployed Yuri has no
    // DeployedFire) is refused and the current one keeps playing. Deaths keep
    // the clone's Die1 safety net (every stock soldier defines it).
    if (!is_die(q) && s.seq >= 0 && (!t.e[q].has || t.e[q].count <= 0)) return;
    // [FUN_0051d4c0] a running non-interruptible sequence is kept unless forced.
    if (!force && s.seq >= 0 && !inf_do_control(s.seq).interrupt) return;
    if (q == kSqDown) {
        if (!t.crawls) return;   // Down only for Crawls=yes types
        s.prone = true;
    } else if (q == kSqUp || q == kSqDeploy) {
        s.prone = false;
    }
    s.seq = q;
    s.stage = 0;
    s.stage_timer = 0;
}

// [FUN_00520810 default branch] what follows a finished sequence.
void finished(InfAnimState& s, const InfTable& t, int& facing, const InfTickCtx& c) {
    const int q = s.seq;
    // W7-12-40 [RE: 0x520810]: AirDeathStart -> AirDeathFalling (loops while
    // falling) -> AirDeathFinish once on the ground.
    if (q == kSqAirDeathStart || q == kSqAirDeathFalling) {
        const int nq = (!s.air && t.e[kSqAirDeathFinish].has) ? kSqAirDeathFinish
                       : t.e[kSqAirDeathFalling].has ? kSqAirDeathFalling : -1;
        if (nq >= 0) {
            s.seq = nq;
            s.stage = 0;
            s.stage_timer = 0;
            return;
        }
    }
    if (is_die(q)) { s.stage = std::max(0, t.e[q].count - 1); return; }   // the body holds (tombstone soon)
    if (t.e[q].has && t.e[q].facing >= 0) {
        // SequenceFacing is clockwise from N; our blocks run counter-clockwise.
        // W7-12-39 [RE: the label parser 0x523A30 compares "N" 0x8248D8 = 0,
        // "NE" = 1 .. "NW" 0x8248BC = 7; 0x520810 sets DirStruct(label << 13),
        // a WORLD direction, and world dir8 d is screen block d+1 (the muzzle
        // rule (d+1)&7 of 0x6FD210 -- world NW is screen up)].
        facing = (8 - ((t.e[q].facing + 1) & 7)) & 7;
    }
    // W7-12-10 [RE: FUN_00520810 default branch]: while the slot-1 target
    // lives, a finished SecondaryFire / SecondaryProne replays (forced) --
    // Boris holding his flare on the building.
    if ((q == kSqSecondaryFire || q == kSqSecondaryProne) && c.has_target && c.fire_slot == 1) {
        s.stage = 0;
        return;
    }
    int next;
    if (q == kSqDeploy || q == kSqDeployedFire) next = kSqDeployed;
    else if (q == kSqUndeploy || q == kSqCheer) next = kSqReady;
    else if (c.moving && !is_deploy_group(q)) next = s.prone ? kSqCrawl : kSqWalk;
    else if (is_deploy_group(q)) next = kSqDeployed;
    else next = s.prone ? kSqProne : kSqReady;
    if (next == q) {   // a looping sequence (Walk/Crawl/Panic/Swim/Deployed...)
        s.stage = 0;
        return;
    }
    s.seq = -1;        // the finished one never blocks the next
    play(s, t, next, c.in_water, true);
}

void arm_idle(InfAnimState& s, const InfTickCtx& c) {
    // [RE: FUN_0051cb80] Timer = Random over [freq*450, freq*1800] frames.
    const double lo = c.idle_frequency * 450.0, hi = c.idle_frequency * 1800.0;
    const double u = (next_rng(s) % 1000000u) / 1000000.0;
    s.idle_timer = std::max(1, om::ftol(lo + (hi - lo) * u));
}

// [RE: FUN_0051cb80] the idle action roll.
void idle_action(InfAnimState& s, const InfTable& t, int& facing, const InfTickCtx& c) {
    arm_idle(s, c);
    if (t.fraidycat && !c.human && s.fear > 50) {
        if (c.scatter) c.scatter();
        return;
    }
    switch (rnd(s, 0, 10)) {
        case 1: case 2: case 7: play(s, t, kSqIdle2, c.in_water, false); break;
        case 3: case 4: case 5: play(s, t, kSqIdle1, c.in_water, false); break;
        case 8:
            facing = rnd(s, 0, 7);
            if (t.fraidycat && !c.human && c.scatter) c.scatter();
            break;
        case 6: case 9: case 10: facing = rnd(s, 0, 7); break;
        default: break;
    }
}

}  // namespace

int inf_anim_frame(const InfAnimState& s, const InfTable& t, int facing) {
    const int q = s.seq < 0 ? kSqReady : s.seq;
    const InfSeqEntry& e = t.e[q];
    const int stage = std::clamp(s.stage, 0, std::max(0, e.count - 1));
    return e.first + (e.fmult ? ((facing % 8 + 8) % 8) * e.fmult : 0) + stage;
}

int inf_anim_tick(InfAnimState& s, const InfTable& t, int& facing, const InfTickCtx& c) {
    ++s.ticks;
    // W7-01-21: a change of the airborne state re-resolves the running pose
    // (take-off: Ready -> Hover; landing: Hover -> Ready).
    if (c.airborne != s.air) {
        s.air = c.airborne;
        if (!c.dying && s.seq >= 0 && !is_die(s.seq)) {
            const int base = (s.seq == kSqHover) ? kSqReady : (s.seq == kSqFly) ? kSqWalk : s.seq;
            const int was = s.seq;
            s.seq = -1;
            play(s, t, base, c.in_water, true);
            if (s.seq < 0) s.seq = was;
        }
    }
    const int seq0 = s.seq;
    if (s.seq < 0) {
        play(s, t, c.deployed ? kSqDeployed : kSqReady, c.in_water, true);
        s.was_deployed = c.deployed;
        arm_idle(s, c);
    }
    if (c.dying) {
        if (!is_die(s.seq) && !s.vanish) {
            const int d = s.death < 0 ? 1 : s.death;
            if (t.not_human || d == 1) play(s, t, kSqDie1, c.in_water, true);
            else if (d == 2 || d == 9) play(s, t, kSqDie2, c.in_water, true);
            else s.vanish = true;   // the death anim (or nothing for 0) replaces the body
        }
    } else {
        // ---- fear [RE: FUN_0051fde0], only while armed (the live game) ----
        if (s.armed) {
            if (s.fear > 0 && !t.fearless) --s.fear;
            if (!is_deploy_group(s.seq) && s.seq != kSqCheer) {
                if (!s.prone) {
                    if (s.fear > 49 && !(c.human && (c.has_destination || c.moving)) && !t.fraidycat)
                        play(s, t, kSqDown, c.in_water, false);
                } else if (s.fear < 50) {
                    play(s, t, kSqUp, c.in_water, false);
                }
            }
            // W7-07-27 / W7-09-12 [RE: FUN_0051FDE0 tail 0x51FF0C]: the panic
            // scatter has no human gate (only the idle-roll scatter does).
            if (t.fraidycat && s.fear > 50 && !c.moving && !c.has_destination && !is_deploy_group(s.seq) &&
                c.scatter)
                c.scatter();
        }
        // ---- deploy edges (GI / Guardian GI) ----
        if (c.deployed && !s.was_deployed) play(s, t, kSqDeploy, c.in_water, true);
        else if (!c.deployed && s.was_deployed) play(s, t, kSqUndeploy, c.in_water, true);
        s.was_deployed = c.deployed;
        // ---- a shot ----
        // W7-12-10 [RE: 0x5203E0]: the sequence follows the weapon index
        // SelectWeapon picked -- the deploy group fires DeployedFire; slot 0
        // FireProne / FireUp; another slot SecondaryProne (prone, count != 0),
        // else SecondaryFire (count != 0), else the FireUp/FireProne pair.
        // W7-12-37: requested NOT forced (vt+0x558(q, 0)), so Deploy / Down /
        // Up / Cheer / Undeploy finish first. W7-02-05: a Desolator eruption
        // (InfAnimState::erupt) is a DeployedFire shot too.
        if (c.fire_flash > s.prev_fire + 1e-9 || s.erupt) {
            s.erupt = false;
            int q = c.deployed ? kSqDeployedFire : s.prone ? kSqFireProne : kSqFireUp;
            if (!c.deployed && c.fire_slot != 0) {
                if (s.prone && t.e[kSqSecondaryProne].has && t.e[kSqSecondaryProne].count > 0) q = kSqSecondaryProne;
                else if (t.e[kSqSecondaryFire].has && t.e[kSqSecondaryFire].count > 0) q = kSqSecondaryFire;
            }
            play(s, t, q, c.in_water, false);
        }
        // ---- cheer (MakeHouseCheer / victory) ----
        if (s.cheer) {
            s.cheer = false;
            if (!c.moving && !c.deployed && t.e[kSqCheer].has) play(s, t, kSqCheer, c.in_water, true);
        }
        // ---- walking vs standing ----
        const InfDoControl& dc = inf_do_control(s.seq);
        if (c.moving && !dc.mobile && s.seq != kSqDown && s.seq != kSqUp && !is_deploy_group(s.seq))
            play(s, t, s.prone ? kSqCrawl : kSqWalk, c.in_water, false);
        else if (!c.moving && dc.mobile && s.seq != kSqDown && s.seq != kSqUp && !(s.air && s.seq == kSqHover))
            play(s, t, s.prone ? kSqProne : kSqReady, c.in_water, true);
        else if (c.moving && (s.seq == kSqWalk || s.seq == kSqPanic))   // fear crossing 199 mid-walk
            play(s, t, kSqWalk, c.in_water, false);
        // ---- idle fidgets [RE: FUN_0051cb80 gated by FUN_00521400] ----
        const bool can_idle = !c.moving && !s.prone && !c.has_target &&
                              (s.seq == kSqReady || s.seq == kSqGuard || s.seq == kSqTread);
        if (can_idle && s.idle_timer > 0 && --s.idle_timer == 0) idle_action(s, t, facing, c);
        else if (s.idle_timer <= 0) arm_idle(s, c);
    }
    s.prev_fire = c.fire_flash;
    // ---- stage clock: one stage per DoControls Rate frames (a sequence
    // started this tick shows its first stage for the whole tick) ----
    const int rate = inf_do_control(s.seq).rate;
    if (s.seq == seq0 && rate > 0 && ++s.stage_timer >= rate) {
        s.stage_timer = 0;
        ++s.stage;
        const int q = s.seq < 0 ? kSqReady : s.seq;
        if (s.stage >= std::max(1, t.e[q].count)) finished(s, t, facing, c);
    }
    return inf_anim_frame(s, t, facing);
}

int inf_prone_damage(const InfAnimState& s, int damage, double prone_damage) {
    // [RE: FUN_00517db5 head] prone and damage > 0 -> ftol(damage * ProneDamage), >= 1.
    if (!s.armed || !s.prone || damage <= 0) return damage;
    return std::max(1, om::ftol(damage * prone_damage));
}

void inf_on_hit(InfAnimState& s, bool has_attacker, double ratio) {
    if (!s.armed) return;
    // [RE: FUN_00517db5 after the hit] attacked by someone while calm ->
    // fear 100 (Fraidycat 300); otherwise (or already afraid) +50, +25 above
    // ConditionRed, +12 above ConditionYellow, capped 300. Fearless never.
    if (has_attacker && s.fear <= 99) {
        if (s.fraidycat) s.fear = 300;
        else if (!s.fearless) s.fear = 100;
        return;
    }
    if (s.fearless) return;
    int inc = 50;
    if (ratio > 0.25) inc = 25;
    if (ratio > 0.5) inc /= 2;
    s.fear = std::min(300, s.fear + inc);
}

}  // namespace ra2engine
