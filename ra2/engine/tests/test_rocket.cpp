// W4 fix: RocketLocomotion blocks + per-rocket-type warheads (engine/rocket.cpp).
#include "doctest.h"

#include <algorithm>
#include <cmath>

#include "asset_fixture.hpp"
#include "combat.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/rules.hpp"
#include "rocket.hpp"

using namespace ra2engine;

namespace {
// The ALL01 map overlay as the clone's overlay_map_rules emits it: the stock
// [General]/[CombatDamage] keys, then the map's [General] DMislAltitude and
// its [V3WH]/[DMISLWH] Verses (special_1 = 50000% / 2%).
const char* kAll01 =
    "[General]\nV3RocketType=V3ROCKET\nDMislType=DMISL\nCMislType=CMISL\n"
    "V3RocketDamage=200\nV3RocketEliteDamage=400\nDMislDamage=300\nDMislEliteDamage=600\n"
    "DMislAltitude=768\nV3RocketAltitude=768\nV3RocketLazyCurve=yes\nDMislLazyCurve=no\n"
    "DMislPauseFrames=20\nDMislTiltFrames=60\nDMislPitchInitial=0\nDMislPitchFinal=0.5\n"
    "DMislTurnRate=0.08\nDMislAcceleration=0.8\nDMislBodyLength=128\n"
    "[CombatDamage]\nV3Warhead=V3WH       ; this is the warhead on a V3 Rocket\n"
    "DMislWarhead=DMISLWH\nV3EliteWarhead=V3EWH\nDMislEliteWarhead=DMISLEWH\n"
    "[V3WH]\nCellSpread=1\nPercentAtMax=.25\nVerses=100%,90%,80%,90%,70%,70%,100%,100%,50%,80%,0%\n"
    "[V3EWH]\nCellSpread=2\nPercentAtMax=.5\nVerses=100%,90%,80%,80%,70%,70%,100%,75%,50%,80%,0%\n"
    "[DMISLWH]\nCellSpread=1.5\nPercentAtMax=.25\nVerses=100%,90%,80%,100%,80%,80%,85%,65%,28%,80%,0%\n"
    "[DMISLEWH]\nCellSpread=3\nPercentAtMax=.5\nVerses=100%,90%,80%,100%,80%,80%,85%,65%,28%,80%,0%\n"
    // --- the all01t.map overlay ---
    "[General]\nDMislAltitude=1550\nV3RocketAltitude=1550\n"
    "[V3WH]\nVerses=100%,90%,80%,90%,70%,70%,100%,100%,50%,50000%,0%\n"
    "[DMISLWH]\nCellSpread=1.5\nPercentAtMax=.25\nVerses=100%,90%,80%,100%,80%,80%,85%,65%,28%,2%,0%\n";
constexpr int kSpecial1 = 9;   // none flak plate light medium heavy wood steel concrete special_1 special_2
}  // namespace

TEST_CASE("rocket: each rocket type explodes with its OWN warhead (FUN_00662A70), ALL01 overlay") {
    const ra2assets::Ini ini = ra2assets::Ini::parse(kAll01);   // Rules keeps a pointer to it
    const ra2assets::Rules rules = ra2assets::Rules::load(ini);
    const RocketParams p = resolve_rocket_params(rules);
    CHECK(rocket_kind_for(p, "V3ROCKET") == kRocketV3);
    CHECK(rocket_kind_for(p, "dmisl") == kRocketDMisl);
    CHECK(rocket_kind_for(p, "CMISL") == kRocketCMisl);
    CHECK(rocket_kind_for(p, "SOMETHING") == kRocketDMisl);   // "anything else" -> DMisl

    const RocketBlock& dm = p.block[kRocketDMisl];
    const RocketBlock& v3 = p.block[kRocketV3];
    CHECK(dm.warhead.id == "DMISLWH");
    CHECK(dm.elite_warhead.id == "DMISLEWH");
    CHECK(v3.warhead.id == "V3WH");          // inline comment cut
    CHECK(v3.elite_warhead.id == "V3EWH");
    CHECK(dm.altitude == 1550);              // the map's [General] DMislAltitude
    CHECK(v3.altitude == 1550);
    CHECK(dm.pause_frames == 20);
    CHECK(dm.tilt_frames == 60);
    CHECK(dm.body_length == 128);
    CHECK_FALSE(dm.lazy_curve);
    CHECK(v3.lazy_curve);
    CHECK(dm.warhead.spread == doctest::Approx(1.5));
    CHECK(dm.elite_warhead.spread == doctest::Approx(3.0));

    // The Statue (Armor=special_1): a Dreadnought missile does 300 x 2% = 6,
    // the scripted V3 200 x 50000% = 100000 (one-shots the 10000-HP Statue).
    const std::vector<int> dmv(dm.warhead.verses.begin(), dm.warhead.verses.end());
    const std::vector<int> v3v(v3.warhead.verses.begin(), v3.warhead.verses.end());
    CHECK(damage_vs(rocket_damage(dm, false), dmv, kSpecial1) == 6);
    CHECK(damage_vs(rocket_damage(v3, false), v3v, kSpecial1) == 100000);
    // Elite launchers: EliteDamage + the elite warhead (DMISLEWH keeps 28%/80%).
    CHECK(rocket_damage(dm, true) == 600);
    CHECK(&rocket_warhead(dm, true) == &dm.elite_warhead);
}

TEST_CASE("rocket: stock rulesmd blocks (V3 / DMisl / CMisl)") {
    if (!ra2fixture::assets_available() || ra2fixture::rulesmd_bytes().empty()) {
        MESSAGE("skipped: rulesmd.ini not found");
        return;
    }
    const RocketParams p = resolve_rocket_params(ra2fixture::rulesmd());
    const RocketBlock& v3 = p.block[kRocketV3];
    CHECK(v3.pause_frames == 0);
    CHECK(v3.tilt_frames == 60);
    CHECK(v3.pitch_initial == doctest::Approx(0.21));
    CHECK(v3.pitch_final == doctest::Approx(0.5));
    CHECK(v3.turn_rate == doctest::Approx(0.05));
    CHECK(v3.acceleration == doctest::Approx(0.4));
    CHECK(v3.altitude == 768);
    CHECK(v3.damage == 200);
    CHECK(v3.elite_damage == 400);
    CHECK(v3.body_length == 256);
    CHECK(v3.lazy_curve);
    CHECK(v3.warhead.id == "V3WH");
    CHECK(v3.elite_warhead.id == "V3EWH");
    const RocketBlock& dm = p.block[kRocketDMisl];
    CHECK(dm.pause_frames == 20);
    CHECK(dm.turn_rate == doctest::Approx(0.08));
    CHECK(dm.acceleration == doctest::Approx(0.8));
    CHECK(dm.damage == 300);
    CHECK_FALSE(dm.lazy_curve);
    CHECK(dm.warhead.id == "DMISLWH");
    CHECK(dm.warhead.spread == doctest::Approx(1.5));
    CHECK(dm.elite_warhead.id == "DMISLEWH");
    CHECK(p.block[kRocketCMisl].warhead.id == "CMISLWH");
    CHECK(p.type_id[kRocketCMisl] == "CMISL");
}

// ---- the flight (FUN_00661D00) ----------------------------------------------
namespace {
struct FlightRun {
    int left_at = -1;       // frame the tilt ended (left the launcher)
    int det_at = -1;        // frame FUN_00661B30 fired
    double max_z = 0.0;
    double nose_x = 0, nose_y = 0, nose_z = 0;
    double pitch_at_left = 0.0;
    int phase5_at = -1;
};
FlightRun fly(const RocketBlock& b, int cap, double dist) {
    RocketFlight f;
    rocket_launch(b, f, 0.0, 0.0, 40.0, dist, 0.0);
    FlightRun r;
    for (int fr = 1; fr <= 3000; ++fr) {
        const int res = rocket_step(b, cap, f, dist, 0.0, 0.0);
        r.max_z = std::max(r.max_z, f.z);
        if (res == kRocketLeftLauncher) { r.left_at = fr; r.pitch_at_left = f.pitch; }
        if (f.phase == kRocketDive && r.phase5_at < 0) r.phase5_at = fr;
        if (res == kRocketDetonate) {
            r.det_at = fr;
            rocket_nose(b, f, r.nose_x, r.nose_y, r.nose_z);
            break;
        }
    }
    return r;
}
}  // namespace

TEST_CASE("rocket flight: DMisl (ALL01 1550) -- 20 pause + 60 tilt on deck, climb, level-off, dive, nose contact") {
    const ra2assets::Ini ini = ra2assets::Ini::parse(kAll01);
    const ra2assets::Rules rules = ra2assets::Rules::load(ini);
    const RocketParams p = resolve_rocket_params(rules);
    const RocketBlock& dm = p.block[kRocketDMisl];
    const int cap = 46;   // DMISL Speed=18 -> min(255, 18*256/100)
    const FlightRun r = fly(dm, cap, 22 * 256.0);   // the fleet -> the Statue
    CHECK(r.left_at == 80);                          // DMislPauseFrames 20 + DMislTiltFrames 60
    CHECK(r.pitch_at_left == doctest::Approx(0.5 * 1.5707963267948966));   // PitchFinal 45 degrees
    CHECK(r.max_z >= 1550.0);                         // climbs to the map's DMislAltitude
    CHECK(r.max_z < 2200.0);                          // then levels off (LazyCurve=no)
    REQUIRE(r.det_at > 0);
    CHECK(r.phase5_at > 0);                           // dived
    CHECK(std::fabs(r.nose_x - 22 * 256.0) < 256.0);  // explodes on the target
    CHECK(std::fabs(r.nose_y) < 1.0);
    CHECK(r.nose_z <= 0.0);
}

TEST_CASE("rocket flight: V3 lazy curve keeps climbing past Altitude and comes down on the target") {
    if (!ra2fixture::assets_available() || ra2fixture::rulesmd_bytes().empty()) {
        MESSAGE("skipped: rulesmd.ini not found");
        return;
    }
    const RocketParams p = resolve_rocket_params(ra2fixture::rulesmd());
    const RocketBlock& v3 = p.block[kRocketV3];
    const FlightRun r = fly(v3, 38, 20 * 256.0);   // V3ROCKET Speed=15 -> 38 leptons/frame
    CHECK(r.left_at == 61);   // PauseFrames 0 still spends its state-1 frame, + TiltFrames 60
    CHECK(r.max_z > 768.0);                         // the arc rises above the 768 lever-off height
    REQUIRE(r.det_at > 0);
    CHECK(std::fabs(r.nose_x - 20 * 256.0) < 256.0);
    CHECK(r.phase5_at < 0);                         // LazyCurve never enters the dive state
}
