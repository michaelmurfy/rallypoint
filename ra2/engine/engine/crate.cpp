#include "crate.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

#include <cctype>
#include <cstdlib>
#include <vector>

namespace ra2engine {

namespace {
// psdes round constants (gamemd.exe 0x83863C / 0x83868C) [RE: veterancy.md C1].
constexpr std::uint32_t kC1[4] = {0xbaa96887u, 0x1e17d32cu, 0x03bcdc3cu, 0x0f33d1b2u};
constexpr std::uint32_t kC2[4] = {0x4b0f3b58u, 0xe874f0c3u, 0x6955c5a6u, 0x55a7ca46u};

// Arithmetic shift right on a 32-bit pattern (x86 SAR).
inline std::uint32_t sar16(std::uint32_t x) {
    const std::int32_t s = static_cast<std::int32_t>(x);
    return static_cast<std::uint32_t>(s >> 16);   // implementation-defined pre-C++20,
                                                   // arithmetic on every target we build
}
inline int ftol(double x) { return om::ftol(x); }

const char* const kNames[kPowerupCount] = {
    "Money", "Unit", "HealBase", "Cloak", "Explosion", "Napalm", "Squad", "Darkness",
    "Reveal", "Armor", "Speed", "Firepower", "ICBM", "Invulnerability", "Veteran",
    "IonStorm", "Gas", "Tiberium", "Pod"};

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
}  // namespace

GameRandom::GameRandom(std::uint32_t seed) {
    // [C1] FUN_0065C110: idx1=0, idx2=103; table[i] = psdes(lword=i, irword=seed).
    idx1_ = 0;
    idx2_ = 103;
    for (int i = 0; i < 250; ++i) {
        std::uint32_t lw = static_cast<std::uint32_t>(i);
        std::uint32_t rw = seed;
        for (int k = 0; k < 4; ++k) {
            const std::uint32_t u = kC1[k] ^ lw;
            const std::uint32_t hi = sar16(u);
            const std::uint32_t lo = u & 0xffffu;
            const std::uint32_t t = ~(hi * hi) + lo * lo;
            std::uint32_t n = sar16(t) | (t << 16);
            n ^= kC2[k];
            n += hi * lo;
            n ^= rw;
            rw = lw;
            lw = n;
        }
        table_[i] = lw;
    }
}

std::uint32_t GameRandom::next() {
    // [C1] FUN_0065C1C0: R250 with taps (idx1, idx2), both wrap at 250.
    table_[idx1_] ^= table_[idx2_];
    const std::uint32_t r = table_[idx1_];
    if (++idx1_ > 249) idx1_ = 0;
    if (++idx2_ > 249) idx2_ = 0;
    return r;
}

int GameRandom::ranged(int lo, int hi) {
    // [C1] FUN_0065C220: rejection-sample next() & mask(bitlen(range)).
    if (lo == hi) return lo;
    if (hi < lo) { const int t = lo; lo = hi; hi = t; }
    const std::uint32_t range = static_cast<std::uint32_t>(hi) - static_cast<std::uint32_t>(lo);
    int bit = 31;
    std::uint32_t probe = range & 0x80000000u;
    while (probe == 0 && bit > 0) {
        --bit;
        probe = range & (1u << bit);
    }
    const unsigned sh = static_cast<unsigned>(bit + 1) & 31u;
    const std::uint32_t mask = ~(0xFFFFFFFFu << sh);
    std::uint32_t r = range + 1u;
    while (static_cast<std::int32_t>(range) < static_cast<std::int32_t>(r)) {
        r = next();
        r &= mask;
    }
    return static_cast<int>(r + static_cast<std::uint32_t>(lo));
}

const char* powerup_name(int p) {
    return (p >= 0 && p < kPowerupCount) ? kNames[p] : "?";
}

PowerupDef parse_powerup(const std::string& value) {
    PowerupDef d;
    std::string v = value;
    const std::size_t sc = v.find(';');
    if (sc != std::string::npos) v = v.substr(0, sc);
    std::vector<std::string> f;
    std::string cur;
    for (char c : v) {
        if (c == ',') { f.push_back(trim(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    f.push_back(trim(cur));
    if (f.size() > 0 && !f[0].empty()) d.weight = std::atoi(f[0].c_str());
    if (f.size() > 1) d.anim = f[1];
    if (f.size() > 2 && !f[2].empty()) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(f[2][0])));
        d.naval = (c == 'y' || c == 't' || c == '1');
    }
    if (f.size() > 3 && !f[3].empty()) {
        double a = std::strtod(f[3].c_str(), nullptr);
        if (f[3].find('%') != std::string::npos) a = a * 0.01;   // FUN_006738C0 [0x7E2808]
        d.arg = a;
    }
    return d;
}

std::array<PowerupDef, kPowerupCount> default_powerups() {
    std::array<PowerupDef, kPowerupCount> p{};
    p[(int)Powerup::Money] = parse_powerup("20,MONEY,yes,2000");
    p[(int)Powerup::Unit] = parse_powerup("20,<none>,no");
    p[(int)Powerup::HealBase] = parse_powerup("10,HEALALL,yes");
    p[(int)Powerup::Cloak] = parse_powerup("0,CLOAK,yes");
    p[(int)Powerup::Explosion] = parse_powerup("0,<none>,yes,500");
    p[(int)Powerup::Napalm] = parse_powerup("0,<none>,no,600");
    p[(int)Powerup::Squad] = parse_powerup("0,<none>,no");
    p[(int)Powerup::Darkness] = parse_powerup("0,SHROUDX,yes");
    p[(int)Powerup::Reveal] = parse_powerup("10,REVEAL,yes");
    p[(int)Powerup::Armor] = parse_powerup("10,ARMOR,yes,1.5");
    p[(int)Powerup::Speed] = parse_powerup("10,SPEED,yes,1.2");
    p[(int)Powerup::Firepower] = parse_powerup("10,FIREPOWR,yes,2.0");
    p[(int)Powerup::ICBM] = parse_powerup("0,CHEMISLE,yes");
    p[(int)Powerup::Invulnerability] = parse_powerup("0,ARMOR,yes,1.0");
    p[(int)Powerup::Veteran] = parse_powerup("20,VETERAN,yes,1");
    p[(int)Powerup::IonStorm] = parse_powerup("0,<none>,yes");
    p[(int)Powerup::Gas] = parse_powerup("0,<none>,yes,100");
    p[(int)Powerup::Tiberium] = parse_powerup("0,<none>,no");
    p[(int)Powerup::Pod] = parse_powerup("0,<none>,no");
    return p;
}

int pick_powerup(GameRandom& rng, const std::array<PowerupDef, kPowerupCount>& p) {
    int total = 0;
    for (const PowerupDef& d : p) total += d.weight;
    if (total <= 0) return -1;
    const int draw = rng.ranged(1, total);
    int acc = 0;
    for (int i = 0; i < kPowerupCount; ++i) {
        acc += p[i].weight;
        if (draw <= acc) return i;
    }
    return kPowerupCount - 1;   // unreachable: draw <= total
}

int crate_lifetime_frames(GameRandom& rng, double regen) {
    // [C4] 0x4A1760..0x4A17C5, one x87 op per statement.
    const double a = regen * 1800.0;
    const double b = regen * 450.0;
    const int draw = rng.ranged(0, 0x7ffffffe);
    const double span = a - b;
    double r = static_cast<double>(draw);
    r = r * 4.656612877414201e-10;          // [0x7E2570] == 1/0x7FFFFFFE
    double t = span * r;
    t = t + b;
    return ftol(t);
}

int initial_crate_count(int players, const CrateRules& r) {
    int n = r.minimum;
    if (r.minimum <= players) n = players;   // FUN_006862D0 L52-55
    if (r.maximum <= n) n = r.maximum;
    return n;
}

}  // namespace ra2engine
