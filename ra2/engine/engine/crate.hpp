#pragma once
// P5 Crates [RE: docs/re/veterancy.md §3 / C1-C8]: the pure, World/SDL-free
// crate arithmetic -- the exact ScenarioClass::Random generator (R250 seeded
// by psdes), the [Powerups] table parse, the cumulative-weight pick, the crate
// lifetime draw and the initial-count clamp. World owns placement/pickup.
#include <array>
#include <cstdint>
#include <string>

namespace ra2engine {

// [C1] RandomClass (FUN_0065C110 seed / 0x65C1C0 next / 0x65C220 ranged): a
// 250-word R250 table (taps idx1, idx2=idx1+103) seeded by 4-round psdes.
class GameRandom {
public:
    friend struct SaveAccess;  // P7 Save/Load (engine/save_fields.hpp)
    explicit GameRandom(std::uint32_t seed = 0);
    std::uint32_t next();                 // FUN_0065C1C0
    int ranged(int lo, int hi);           // FUN_0065C220 (inclusive, either order)
    std::uint32_t table_word(int i) const { return table_[i]; }  // test seam
    int idx1() const { return idx1_; }    // simdiff dump seam (read-only)
    int idx2() const { return idx2_; }
private:
    std::array<std::uint32_t, 250> table_{};
    int idx1_ = 0, idx2_ = 103;
};

// [C6] Powerup ids in the original's [Powerups] order (0x7E423C name table).
enum class Powerup : int {
    Money = 0, Unit, HealBase, Cloak, Explosion, Napalm, Squad, Darkness,
    Reveal, Armor, Speed, Firepower, ICBM, Invulnerability, Veteran, IonStorm,
    Gas, Tiberium, Pod
};
constexpr int kPowerupCount = 19;
const char* powerup_name(int p);          // "Money", "Unit", ...

// One [Powerups] entry: "weight,anim,naval[,argument]" (argument with a '%'
// is scaled by 0.01 -- FUN_006738C0).
struct PowerupDef {
    int weight = 0;
    std::string anim;
    bool naval = false;
    double arg = 0.0;
};
PowerupDef parse_powerup(const std::string& value);

// [CrateRules] + [Powerups]. Defaults are this install's values (§0).
struct CrateRules {
    int minimum = 1;              // CrateMinimum
    int maximum = 255;            // CrateMaximum
    int radius_leptons = 768;     // CrateRadius=3.0 read as leptons [C7]
    double regen = 3.0;           // CrateRegen (minutes)
    std::array<PowerupDef, kPowerupCount> powerups{};
};
// The stock [Powerups] table of this install (used by tests / as a fallback).
std::array<PowerupDef, kPowerupCount> default_powerups();

// [C5] Random(1, sum(weights)) then the first cumulative bucket >= draw.
// Returns -1 if every weight is 0 (never with stock data).
int pick_powerup(GameRandom& rng, const std::array<PowerupDef, kPowerupCount>& p);

// [C4] lifetime frames = ftol((A-B) * (Random(0,0x7FFFFFFE) * k) + B),
// A = regen*1800, B = regen*450, k = 1/0x7FFFFFFE.
int crate_lifetime_frames(GameRandom& rng, double regen);

// [C2] min(max(players, CrateMinimum), CrateMaximum) -- the two sequential
// clamps of FUN_006862D0 (Max applied last).
int initial_crate_count(int players, const CrateRules& r);

}  // namespace ra2engine
