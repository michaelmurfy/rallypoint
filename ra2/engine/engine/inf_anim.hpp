#pragma once
// inf_anim.hpp -- W3-B infantry sequence state machine (DoType) [RE:
// docs/re/infantry-anim.md §8-§12; InfantryClass::PlayAnim FUN_0051d4c0,
// sequence-finished AI FUN_00520810, ReceiveDamage FUN_00517db5, fear /
// prone AI FUN_0051fde0, idle actions FUN_0051cb80 + gate FUN_00521400,
// the DoControls table @0x7E9F7C].
//
// Pure C++17 (no SDL). The per-unit state lives in Unit::ia; World::step runs
// inf_anim_tick once per 15 Hz logic tick for every infantry when
// World::enable_infantry_anims was called (the live game). Nothing here is
// read by world_hash.
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace ra2engine {

// YRpp Sequence enum (GeneralDefinitions.h), the art Sequence= key names.
enum InfSeq : int {
    kSqReady = 0, kSqGuard, kSqProne, kSqWalk, kSqFireUp, kSqDown, kSqCrawl, kSqUp, kSqFireProne,
    kSqIdle1, kSqIdle2, kSqDie1, kSqDie2, kSqDie3, kSqDie4, kSqDie5, kSqTread, kSqSwim, kSqWetIdle1,
    kSqWetIdle2, kSqWetDie1, kSqWetDie2, kSqWetAttack, kSqHover, kSqFly, kSqTumble, kSqFireFly,
    kSqDeploy, kSqDeployed, kSqDeployedFire, kSqDeployedIdle, kSqUndeploy, kSqCheer, kSqParadrop,
    kSqAirDeathStart, kSqAirDeathFalling, kSqAirDeathFinish, kSqPanic, kSqShovel, kSqCarry,
    kSqSecondaryFire, kSqSecondaryProne, kSqCount
};
const char* inf_seq_name(int seq);

// [RE: DoControls @0x7E9F7C, 4 bytes per sequence] Interrupt / IsMobile /
// RandomStart / Rate (game frames per stage; 0 = the stage never advances).
struct InfDoControl { bool interrupt = false, mobile = false, random_start = false; int rate = 0; };   // W9-05: initialised
const InfDoControl& inf_do_control(int seq);

// One art Sequence= entry: StartFrame,FrameCount,FacingMultiplier[,Facing].
// `facing` is the optional SequenceFacing label (N=0, NE=1 .. NW=7,
// clockwise), -1 if none; `has` = the key exists in the art section.
struct InfSeqEntry {
    int first = 0, count = 1, fmult = 0, facing = -1;
    bool has = false;
};
// Per image: the full table + the type switches the machine reads.
struct InfTable {
    std::array<InfSeqEntry, kSqCount> e{};
    bool crawls = true;      // art Crawls= [RE-FALLBACK: ctor default not traced; every stock soldier sets it]
    bool fearless = false;   // rules Fearless=
    bool fraidycat = false;  // rules Fraidycat=
    bool not_human = false;  // rules NotHuman= (always Die1, no DeadBodies)
};

// Per-unit state (Unit::ia). `death` = the killing warhead's InfDeath (-1
// unknown -> Die1); `vanish` = the body is replaced by a death anim (the
// presentation spawns it) or removed (InfDeath 0).
struct InfAnimState {
    int seq = -1;
    int stage = 0;
    int stage_timer = 0;
    double acc = 0.0;        // logic-tick accumulator (dt * 15)
    long ticks = 0;          // logic ticks this unit has run
    int idle_timer = -1;     // ticks to the next idle action, -1 = unarmed
    std::uint32_t rng = 0;   // render-side RNG (never the sim RNG)
    int fear = 0;            // InfantryClass +0x6D4 (0..300)
    bool prone = false;      // InfantryClass +0x6DB
    bool armed = false;      // fear / ProneDamage active (World::enable_infantry_anims)
    bool fearless = false;   // copied from the InfTable when armed (combat reads them)
    bool fraidycat = false;
    int death = -1;          // InfDeath of the kill
    bool vanish = false;
    bool was_deployed = false;
    double prev_fire = 0.0;
    bool cheer = false;      // MakeHouseCheer / victory request
    bool erupt = false;      // W7-02-05: a Desolator eruption fired this tick (DeployedFire)
    bool air = false;        // W7-01-21: airborne this tick (jumpjet infantry)
};

// What the machine needs from the World for one unit and one tick.
struct InfTickCtx {
    bool moving = false;
    bool has_destination = false;
    bool human = false;          // the house is player-controlled (FUN_0050b550)
    bool in_water = false;
    bool has_target = false;
    bool dying = false;
    bool deployed = false;
    double fire_flash = 0.0;
    int fire_slot = 0;              // W7-12-10: the active weapon slot (Unit::w_active)
    bool airborne = false;          // W7-01-21: a jumpjet soldier in the air (Rocketeer)
    double idle_frequency = 0.15;   // rules [AudioVisual] IdleActionFrequency (minutes)
    std::function<void()> scatter;  // Fraidycat flight (World::order_scatter)
};

// One logic tick; returns the frame to draw. `facing` is the unit's 8-way
// block index (0 = N, counter-clockwise; infantry-anim.md §4) and may be
// turned by an idle action or a finished sequence's facing label.
int inf_anim_tick(InfAnimState& s, const InfTable& t, int& facing, const InfTickCtx& c);
// The frame for the current state without stepping.
int inf_anim_frame(const InfAnimState& s, const InfTable& t, int facing);

// [RE: FUN_00517db5] A hit on an armed infantry: the ProneDamage multiplier
// (applied by the caller BEFORE subtracting) and the fear bump after it.
int inf_prone_damage(const InfAnimState& s, int damage, double prone_damage);
void inf_on_hit(InfAnimState& s, bool has_attacker, double health_ratio);


}  // namespace ra2engine
