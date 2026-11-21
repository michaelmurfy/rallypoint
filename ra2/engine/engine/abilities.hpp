#pragma once
// P4 Unit special abilities (batch 1) -- the PURE arithmetic + the radiation
// field [RE: docs/re/abilities.md]. World-free (no Unit, no Building): every
// function here is a disassembled formula, host-doctested in
// tests/test_abilities.cpp. The World-side pass that drives these lives in
// engine/world_abilities.cpp.
#include <cstdint>
#include "orig_math.hpp"
#include <vector>

#include "ability_types.hpp"

namespace ra2engine {

constexpr double kAbilityLeptonsPerCell = 256.0;

// x87 __ftol (FUN_007C4D00): truncation toward zero.
inline int ability_ftol(double v) { return om::ftol(v); }   // Net L0: wraps like the original, never UB

// --- §A Radiation -----------------------------------------------------------
// SetSpread (0x65AF10): SpreadInLeptons = n*256 + 128.
int rad_spread_leptons(int spread_cells);
// Radiate / DecreaseRadiation (0x65B400 / 0x65B740): a cell d leptons from the
// base gets (SL - d)/SL * RadLevel, or 0.0 outside SL.
double rad_cell_amount(int d_leptons, int spread_leptons, int rad_level);
// Activate (0x65AFC0): LevelSteps = RadDuration / RadLevelDelay (int div).
int rad_level_steps(int duration, int level_delay);
// Add (0x65AF70): the effective current level = ability_ftol(L * left / duration).
int rad_add_current(int rad_level, int time_left, int duration);
// Deactivate (0x65B590): per-cell removal = amt/steps * (left/delay + 1).
double rad_deactivate_amount(double amt, int steps, int time_left, int level_delay);
// FootClass::AI (0x4DA4E0): ability_ftol(ability_ftol(min(cell, RadLevelMax)) * RadLevelFactor);
// 0 when the clamped level is not positive (no ReceiveDamage call at all).
int rad_damage(double cell_level, const AbilityRules& r);
// The cadence (Frame % RadApplicationDelay == 0, absolute phase).
bool rad_apply_due(std::uint64_t frame, const AbilityRules& r);

struct RadSite {
    int cx = 0, cy = 0;          // BaseCell
    int spread = 0;              // cells
    int spread_leptons = 128;    // spread*256+128
    int rad_level = 0;           // RadLevel
    int level_steps = 0;         // RadDuration / RadLevelDelay
    int duration = 0;            // RadDuration
    int time_left = 0;           // RadTimeLeft
    std::uint64_t timer_start = 0;  // LevelTimer start frame
    int timer_left = 0;             // LevelTimer length
    bool alive = true;
};

// The per-cell RadLevel doubles + the RadSite array (CellClass +0xF0 and
// RadSiteClass::Array). Distances are 2-D cell-center leptons (documented
// divergence, abilities.md §A4 / combat.md §6).
class RadField {
public:
    friend struct SaveAccess;  // P7 Save/Load (engine/save_fields.hpp)
    void init(int w, int h);
    bool empty() const { return w_ == 0; }
    int w() const { return w_; }
    int h() const { return h_; }
    double level_at(int x, int y) const;
    // BulletClass::Detonate's RadSite block (§A1): create at (cx,cy) with
    // Spread=spread / SetRadLevel(level) / Activate, or Add(level) to the
    // site already based at that cell. frame = the current logic frame.
    void detonate(int cx, int cy, int spread, int level, std::uint64_t frame,
                  const AbilityRules& r);
    // RadSite::AI for every live site (§A3): TimeLeft--, the 90-frame decay,
    // deletion at TimeLeft <= 0.
    void step(std::uint64_t frame, const AbilityRules& r);
    const std::vector<RadSite>& sites() const { return sites_; }
    int live_sites() const;
    double total() const;  // sum over all cells (the gate witness)

private:
    void radiate(const RadSite& s, double sign_scale, bool add);
    std::vector<double> cell_;
    std::vector<RadSite> sites_;
    int w_ = 0, h_ = 0;
};

// --- §B Terror Drone ----------------------------------------------------------
// FootClass::ReceiveDamage (0x4D7250): SuppressionTimer = 2*damage - threshold,
// armed only when damage > threshold. Returns 0 when not armed.
int suppression_frames(int damage, int threshold);

// --- §C Chrono Legionnaire ------------------------------------------------------
// TeleportLocomotionClass::Process (0x7187B7..0x718883).
int teleport_delay_frames(int dist_leptons, const AbilityRules& r);
// TemporalClass::Fire (0x71A1A0): WarpRemaining = Type.Strength * 10.
int warp_remaining_init(int type_strength);

// --- §D C4 ----------------------------------------------------------------------
// InfantryClass C4 plant (0x51A39E): ability_ftol(C4Delay * 900.0) frames.
int c4_delay_frames(double c4_delay_minutes);

// --- §E Ivan bombs --------------------------------------------------------------
// BombClass::TimeToExplode (0x4389F0): DetonationFrame < Frame (strict).
bool bomb_due(std::int64_t detonation_frame, std::uint64_t frame);

// --- §G IFV ---------------------------------------------------------------------
// SetCurrentWeapon (0x70D0A0): 0 <= idx < 18 else 0; also 0 when the type has
// fewer weapons than idx+1 (an absent WeaponN resolves invalid).
int ifv_weapon_index(int ifv_mode, int weapon_count);

// --- Area damage falloff (combat.md §7 adjudication, reused) ---------------------
// 1.0 at the center -> pmax at the edge, linear; < 0 = outside (no hit).
double area_scale(double d_leptons, double radius_leptons, double pmax);

}  // namespace ra2engine
