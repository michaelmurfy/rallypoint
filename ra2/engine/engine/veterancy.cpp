#include "veterancy.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

#include <cctype>

namespace ra2engine {

namespace {
// ftol: the binary's _ftol (0x7C4D00) truncates toward zero.
inline int ftol(double x) { return om::ftol(x); }

const char* const kAbilityNames[18] = {
    "FASTER", "STRONGER", "FIREPOWER", "SCATTER", "ROF", "SIGHT", "CLOAK",
    "TIBERIUM_PROOF", "VEIN_PROOF", "SELF_HEAL", "EXPLODES", "RADAR_INVISIBLE",
    "SENSORS", "FEARLESS", "C4", "TIBERIUM_HEAL", "GUARD_AREA", "CRUSHER"};
}  // namespace

AbilityMask parse_abilities(const std::string& csv) {
    AbilityMask m = 0;
    std::string tok;
    auto flush = [&]() {
        std::size_t a = 0, b = tok.size();
        while (a < b && std::isspace(static_cast<unsigned char>(tok[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(tok[b - 1]))) --b;
        std::string t = tok.substr(a, b - a);
        for (char& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        for (int i = 0; i < 18; ++i)
            if (t == kAbilityNames[i]) m |= (1u << i);
        tok.clear();
    };
    for (char c : csv) {
        if (c == ';') break;            // trailing ini comment
        if (c == ',') { flush(); continue; }
        tok.push_back(c);
    }
    flush();
    return m;
}

bool is_veteran(float v) { return v >= 1.0f && v < 2.0f; }
bool is_elite(float v) { return v >= 2.0f; }
int vet_rank(float v) { return is_elite(v) ? 2 : (is_veteran(v) ? 1 : 0); }

float veterancy_add(float v, int owner_cost, int victim_cost, const VeterancyRules& r) {
    // [V2] FILD victim; FILD owner; FMUL Ratio; FDIVP; FADD (float)v.
    const double owner = static_cast<double>(owner_cost) * r.ratio;
    const double gain = static_cast<double>(victim_cost) / owner;   // owner 0 -> inf
    const double sum = gain + static_cast<double>(v);
    // cap <= sum -> store (float)cap, else (float)sum (FCOMPP on the double).
    if (r.cap <= sum) return static_cast<float>(r.cap);
    return static_cast<float>(sum);
}

int kill_value(int victim_cost, float victim_vet, bool allied) {
    if (allied) return 0;
    if (is_veteran(victim_vet)) return victim_cost * 2;
    if (is_elite(victim_vet)) return victim_cost * 3;
    return victim_cost;
}

bool has_ability(float v, AbilityMask vet, AbilityMask elite, Ability a) {
    if (is_veteran(v)) return ability_bit(vet, a);
    if (is_elite(v)) return ability_bit(vet, a) || ability_bit(elite, a);
    return false;
}

int fire_damage(int damage, double fp_mult, bool firepower, double vet_combat,
                double house_fp) {
    // [V5] FLD houseFP (house+0x188); FMUL FirepowerMultiplier; FIMUL Damage; ftol.
    double t = house_fp * fp_mult;
    t = t * static_cast<double>(damage);
    int d = ftol(t);
    if (firepower) {
        const double u = static_cast<double>(d) * vet_combat;
        d = ftol(u);
    }
    return d;
}

int receive_damage(int d, double armor_mult, bool stronger, double vet_armor) {
    // [V6] FILD d; ( FLD houseArmor(1.0); FMUL ArmorMultiplier ); FDIVR; ftol.
    const double div = 1.0 * armor_mult;
    const double q = static_cast<double>(d) / div;
    int out = ftol(q);
    if (stronger) {
        const double q2 = static_cast<double>(out) / vet_armor;
        out = ftol(q2);
    }
    if (out < 1) out = 1;
    return out;
}

int rof_frames(int rof, bool rof_ability, double vet_rof, double house_rof) {
    // [V7] ftol(ROF * houseROF (house+0x1a8) [+ Random(0,2): NOT reproduced]).
    const double base = static_cast<double>(rof) * house_rof;
    int r = ftol(base);
    if (rof_ability) {
        const double t = static_cast<double>(r) * vet_rof;
        r = ftol(t);
    }
    return r;
}

int speed_value(int speed, double speed_mult, bool faster, double vet_speed) {
    // [V8] FILD speed; FMUL houseSpeed(1.0); FMUL SpeedMultiplier; ftol.
    double t = static_cast<double>(speed) * 1.0;
    t = t * speed_mult;
    int s = ftol(t);
    if (faster) {
        const double u = static_cast<double>(s) * vet_speed;
        s = ftol(u);
    }
    // * SpeedPercentage (1.0 -- no system lowers it in this engine)
    const double p = static_cast<double>(s) * 1.0;
    return ftol(p);
}

int sight_value(int sight, bool sight_ability, double vet_sight) {
    // [V9] guarded by VeteranSight != 0.0 (dead on this install).
    if (!sight_ability || vet_sight == 0.0) return sight;
    const double t = static_cast<double>(sight) * vet_sight;
    return ftol(t);
}

int self_heal_interval(double repair_rate) {
    const double t = repair_rate * 900.0;   // [V11] FLD RepairRate; FMUL 900.0; ftol
    return ftol(t);
}

bool should_self_heal(std::uint64_t frame, int interval, bool eligible, int hp, int max_hp) {
    if (!eligible || interval <= 0) return false;
    if (frame % static_cast<std::uint64_t>(interval) != 0) return false;
    if (hp == max_hp) return false;
    return hp != 0;
}

float promote_levels(float v, double levels) {
    for (int i = 0; static_cast<double>(i) < levels; ++i) {
        if (is_veteran(v)) v = 2.0f;                     // SetElite
        if (v >= 0.0f && v < 1.0f) v = 1.0f;             // IsRookie -> SetVeteran
        if (v < 0.0f) v = 0.0f;                          // IsNegative -> SetRookie(false)
    }
    return v;
}

}  // namespace ra2engine
