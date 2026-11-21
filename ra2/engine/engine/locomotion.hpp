#pragma once
// W7 locomotion model (docs/gap/wave7/11-locomotion.md) -- the pure parts:
// the per-unit locomotor state (UnitLoco), the Drive/Ship track tables, the
// integer speed formulas and the Hover / Walk speed rules. No SDL, no World:
// compiled into both ra2assets_tests and ra2engine (pathfind.cpp pattern).
//
// [RE: FootClass::GetCurrentSpeed FUN_004DB0B0, InfantryClass override
// FUN_00521AB0, DriveLocomotionClass While_Moving FUN_004B0E50 / Start_Of_Move
// FUN_004B2550, ShipLocomotionClass FUN_0069FC80 / FUN_006A1310,
// HoverLocomotionClass Process FUN_00514130 / speed FUN_00515CF0,
// WalkLocomotionClass FUN_00759CE0, Basic_Path FUN_004D3840.]

#include <cstdint>

namespace ra2engine {

// The locomotor CLSID family a unit moves with (TechnoType Locomotor=).
enum class LocoKind : std::uint8_t {
    Drive = 0,     // {4A582741}: track tables (vehicles; the default)
    Walk = 1,      // {4A582744}: raw leptons (infantry)
    Hover = 2,     // {4A582742}: raw leptons, accel/brake ramp, drift
    Ship = 3,      // {2BEA74E1}: track tables (identical data, 0x7F1960/0x7F1A40)
    Teleport = 4,  // {4A582747}
    Jumpjet = 5,   // {92612C46}
    Fly = 6,       // {4A582746}
    Other = 7,     // Rocket / Mech / Tunnel / DropPod...
};

// Per-type locomotor data (UnitCombat::loco), resolved from rules by
// weapon_resolve. Defaults are INERT for a hand-built UnitCombat: kind -1
// (derived from is_vehicle / is_naval / infantry at step time) and no
// acceleration (the rules resolver applies the TechnoType default yes).
// [RE: TechnoType ctor FUN_0070FE90 / INI FUN_00711510: SlowdownDistance
// +0x2F8 (500), DeaccelerationFactor +0x300 (.002), AccelerationFactor +0x308
// (.03), Accelerates +0xDBD (yes)].
struct LocoType {
    int kind = -1;                    // LocoKind, -1 = derive
    bool accelerates = false;
    double accel_factor = 0.03;
    double decel_factor = 0.002;
    int slowdown_distance = 500;      // leptons
};

// Per-unit locomotor state (Unit::loco). Frame counts are logic frames.
struct UnitLoco {
    LocoType t;
    // [RE: Foot+0x578 SpeedPercentage, Drive+0x50 desired, Drive+0x4C
    // SpeedAccum (speed units carried between frames)].
    double pct = 0.0;
    double pct_desired = 1.0;
    double accum = 0.0;
    // Drive/Ship track in progress [RE: Drive+0x58 TrackNumber (TurnTrack
    // index), +0x5C TrackIndex]: RawTrack id (-1 none), its flags, the next
    // point index, the head-to cell, the middle cell of a two-step curve and
    // whether the unit has passed into it (the RawTrack CellIndex).
    int trk = -1;
    int trk_flags = 0;
    int trk_idx = 0;
    int trk_face = 0;                 // end facing (u16)
    int trk_hx = 0, trk_hy = 0;
    int trk_mx = 0, trk_my = 0;
    bool trk_two = false;
    bool trk_in_mid = false;
    std::int8_t trk_layer_mid = 0, trk_layer_head = 0;
    // Hover current speed fraction [RE: Hover loco+0x50].
    double hover = 0.0;
    // Blocked-move state [RE: Foot+0x640 PathDelay timer, +0x668 BlockageTimer,
    // +0x6B7 blockage seen, +0x64C retry counter; Basic_Path FUN_004D3840].
    double path_delay = 0.0;          // frames until the next re-path try
    double blockage = -1.0;           // frames left before mode-2 re-pathing; -1 not started
    int temp_tries = 0;               // Temp (parked friendly) blocks met this hold-up
};

// Locomotor kind from a Locomotor= CLSID string (case-insensitive; empty ->
// Drive, the TechnoType default for a vehicle; infantry pass is_infantry).
LocoKind loco_kind_from_clsid(const char* clsid, bool is_infantry);

// One track point: a lepton offset from the track's head-to cell centre and
// the body facing (0..255) there [RE: RawTrack points, DAT_007E6A28].
struct TrackPoint {
    std::int16_t x, y;
    std::uint8_t facing;
};

// [RE: RawTrack DAT_007E6A28 (Drive) == DAT_007F1960 (Ship), 16 bytes:
// points*, JumpIndex, EntryIndex, CellIndex]. `n` excludes the {0,0}
// terminator.
struct RawTrack {
    const TrackPoint* pts;
    int n;
    int jump, entry, cell;
};

// [RE: TurnTrack DAT_007E6B28 (Drive) == DAT_007F1A40 (Ship), 12 bytes,
// index cur_dir*8 + next_dir]: RawTrack id, short-track id, end facing
// (dir*32), flags (1 swap x/y, 2 negate x, 4 negate y -- applied in that
// order by FUN_004B46A0; 8 = the track spans TWO path steps).
struct TurnTrack {
    std::int8_t track;
    std::int8_t short_track;
    std::uint8_t face;
    std::uint8_t flags;
};

const RawTrack& raw_track(int id);           // ids 0..13 (0 = none)
const TurnTrack& turn_track(int cur_dir, int next_dir);

// Point `idx` of track `id` under `flags`, in leptons relative to the head-to
// cell centre; *facing_u16 = the transformed facing x 256 (0..65535).
void track_point(int id, int idx, int flags, int& x, int& y, int& facing_u16);

// 8-way direction of a cell step: 0 N (0,-1), 1 NE, 2 E (+1,0), 3 SE, 4 S,
// 5 SW, 6 W, 7 NW (the original's FacingType); -1 if not a unit step.
int dir8_of_step(int dx, int dy);

// Integer leptons per frame of a unit moving `speed_cps` cells/sec at the
// engine's 15 logic frames/sec (the type's Speed= leptons, ftol'd).
int leptons_per_frame(double speed_cps);

// [RE: FUN_004DB0B0 tail] ftol(speed * SpeedPercentage).
int apply_speed_pct(int speed, double pct);

// [RE: InfantryClass::GetCurrentSpeed FUN_00521AB0] prone (Infantry+0x6DB):
// Crawls=yes -> s - trunc(s/3); Crawls=no -> s + s/2.
int infantry_speed(int s, bool prone, bool crawls);

// Rules the locomotors read (rulesmd defaults; World::arm_loco_rules
// replaces them with the install's values).
struct LocoRules {
    int path_delay_frames = 9;        // ftol([General] PathDelay .01 * 900) [RE: disasm 0x4D3F42]
    int blockage_path_delay = 60;     // [General] BlockagePathDelay (frames, Rules+0x1768)
    int close_enough = 576;           // [General] CloseEnough 2.25 cells, leptons (Rules+0x1718)
    double condition_yellow = 0.5;    // [AudioVisual] ConditionYellow (Rules+0x1700)
    double hover_accel = 0.02;        // [General] HoverAcceleration (Rules+0x5E0)
    double hover_brake = 0.03;        // [General] HoverBrake (Rules+0x5E8)
    double hover_boost = 1.5;         // [General] HoverBoost (Rules+0x5D8)
};

// [RE: HoverLocomotionClass FUN_00515CF0] the target speed fraction:
// 0 when the facing error is over 0x2000 (45 deg: stop and turn), 0.5 within
// one cell (0x100 leptons) of the destination, else 1.0; times HoverBoost on a
// straightaway, clamped to 1.0.
double hover_target_fraction(int facing_err_u16, int dist_to_goal_leptons, bool straightaway,
                             double boost);
// One frame of the hover ramp: cur += 1/(accel*900) toward a higher target,
// cur -= 1/(brake*900) toward a lower one (never past the target).
double hover_ramp(double cur, double target, double accel, double brake);

}  // namespace ra2engine
