#include "doctest.h"
#include "production.hpp"
#include "roster2.hpp"
#include "ra2assets/art.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/rules.hpp"
#include <set>
#include <utility>
using namespace ra2engine;

// Every literal below is recovered in docs/re/roster2.md (§ refs inline).

namespace ra2engine {
// Local helper (pathfind.hpp's Cellxy has no operator==).
static bool operator==(const Cellxy& a, const Cellxy& b) { return a.x == b.x && a.y == b.y; }
}  // namespace ra2engine

namespace {
using CellSet = std::set<std::pair<int, int>>;
std::function<bool(int, int)> in_set(const CellSet& s) {
    return [&s](int x, int y) { return s.count({x, y}) != 0; };
}
}  // namespace

TEST_CASE("roster2 wall: mask bits N=1 E=2 S=4 W=8 from AdjacentCell [RE: TL;DR 2/3]") {
    CellSet walls{{10, 10}, {10, 9}, {11, 10}, {10, 11}, {9, 10}};
    CHECK(wall_mask(10, 10, in_set(walls)) == (kWallN | kWallE | kWallS | kWallW));
    CHECK(kWallN == 1);
    CHECK(kWallE == 2);
    CHECK(kWallS == 4);
    CHECK(kWallW == 8);
    CellSet north_only{{5, 4}};
    CHECK(wall_mask(5, 5, in_set(north_only)) == kWallN);      // (0,-1) is N
    CellSet east_only{{6, 5}};
    CHECK(wall_mask(5, 5, in_set(east_only)) == kWallE);       // (+1,0) is E
    CellSet diag{{6, 6}, {4, 4}};
    CHECK(wall_mask(5, 5, in_set(diag)) == 0);                 // diagonals never connect
}

TEST_CASE("roster2 wall: frame = stage*16 + mask, stage from HP [RE: TL;DR 2, §D]") {
    CHECK(wall_frame(0, 0) == 0);
    CHECK(wall_frame(kWallE | kWallW, 0) == 10);
    CHECK(wall_frame(kWallE | kWallW, 1) == 26);
    CHECK(wall_frame(15, 2) == 47);
    CHECK(wall_frame(31, 0) == 15);            // mask clamped to 4 bits
    CHECK(wall_stage(300, 300, 0.5) == 0);
    CHECK(wall_stage(151, 300, 0.5) == 0);
    CHECK(wall_stage(150, 300, 0.5) == 1);     // <= ConditionYellow -> damaged
    CHECK(wall_stage(1, 0, 0.5) == 0);         // degenerate max guarded
}

TEST_CASE("roster2 wall: GuardRange=5 -> fill reach 5 cells [RE: TL;DR 4]") {
    CHECK(wall_fill_range(5.0) == 5);
    CHECK(wall_fill_range(5.5) == 5);
    CHECK(wall_fill_range(0.0) == 0);
}

TEST_CASE("roster2 wall: line fill closes gaps to an own wall within range [RE: TL;DR 4]") {
    auto all_free = [](int, int) { return true; };
    {   // own wall 4 east -> fill the 3 between, nothing else
        CellSet own{{14, 10}};
        const auto f = wall_line_fill(10, 10, 5, in_set(own), all_free);
        REQUIRE(f.size() == 3);
        CHECK(f[0] == Cellxy{11, 10});
        CHECK(f[1] == Cellxy{12, 10});
        CHECK(f[2] == Cellxy{13, 10});
    }
    {   // own wall exactly at the reach (k=5) -> 4 fills
        CellSet own{{15, 10}};
        CHECK(wall_line_fill(10, 10, 5, in_set(own), all_free).size() == 4);
    }
    {   // own wall beyond the reach (k=6) -> nothing
        CellSet own{{16, 10}};
        CHECK(wall_line_fill(10, 10, 5, in_set(own), all_free).empty());
    }
    {   // an unplaceable cell in between stops that direction
        CellSet own{{14, 10}};
        auto blocked = [](int x, int y) { return !(x == 12 && y == 10); };
        CHECK(wall_line_fill(10, 10, 5, in_set(own), blocked).empty());
    }
    {   // adjacent own wall -> nothing to fill (k=1)
        CellSet own{{11, 10}};
        CHECK(wall_line_fill(10, 10, 5, in_set(own), all_free).empty());
    }
    {   // facing order N, E, S, W; vertical fills advance y
        CellSet own{{10, 7}, {7, 10}};
        const auto f = wall_line_fill(10, 10, 5, in_set(own), all_free);
        REQUIRE(f.size() == 4);
        CHECK(f[0] == Cellxy{10, 9});    // N first
        CHECK(f[1] == Cellxy{10, 8});
        CHECK(f[2] == Cellxy{9, 10});    // then W
        CHECK(f[3] == Cellxy{8, 10});
    }
}

TEST_CASE("roster2 wall: build time x WallBuildSpeedCoefficient after the integer time [RE: TL;DR 5]") {
    CHECK(kWallBuildSpeedCoefficient == 3.0);
    CHECK(build_frames(100, 0.7) == 63);
    CHECK(wall_build_frames(100, 0.7) == 189);
    CHECK(wall_build_frames(100, 0.7, 1.0) == 63);
}

TEST_CASE("roster2 naval: WaterBound placement = every foundation cell water [RE: TL;DR 6]") {
    const int W = 10, H = 10;
    std::vector<std::uint8_t> water(W * H, 0);
    for (int y = 2; y < 8; ++y)
        for (int x = 2; x < 8; ++x) water[y * W + x] = 1;
    CHECK(water_bound_ok(2, 2, 4, 4, water, W, H));
    CHECK(water_bound_ok(4, 4, 4, 4, water, W, H));
    CHECK_FALSE(water_bound_ok(5, 5, 4, 4, water, W, H));   // spills onto land
    CHECK_FALSE(water_bound_ok(8, 8, 4, 4, water, W, H));   // off-grid + land
    CHECK_FALSE(water_bound_ok(-1, 2, 4, 4, water, W, H));
}

TEST_CASE("roster2 naval: exit = first free WATER ring cell outside the yard [RE: TL;DR 8]") {
    const int W = 20, H = 20;
    std::vector<std::uint8_t> water(W * H, 1);
    Building yard;
    yard.cx = 5; yard.cy = 5; yard.fw = 4; yard.fh = 4;
    BuildingTypeDef def;
    def.fw = 4; def.fh = 4;
    auto free_all = [](int, int) { return true; };
    const Cellxy e = naval_exit_cell(yard, def, water, W, H, free_all);
    CHECK(e == Cellxy{7, 9});   // south door cell of the even-height foundation
    // land south of the yard -> the walk keeps going to a water cell
    for (int x = 0; x < W; ++x) water[9 * W + x] = 0;
    const Cellxy e2 = naval_exit_cell(yard, def, water, W, H, free_all);
    REQUIRE(e2.x >= 0);
    CHECK(water[e2.y * W + e2.x] == 1);
    CHECK_FALSE((e2.x >= 5 && e2.x < 9 && e2.y >= 5 && e2.y < 9));
    // boxed in (no water anywhere outside) -> {-1,-1}
    std::vector<std::uint8_t> dry(W * H, 0);
    CHECK(naval_exit_cell(yard, def, dry, W, H, free_all).x == -1);
}

TEST_CASE("roster2 naval: Factory=UnitType split by Naval [RE: TL;DR 7]") {
    //                 GAWEAP  NAYARD  GAPILE  GAYARD
    std::vector<bool> uf{true, true, false, true};
    std::vector<bool> nv{false, true, false, true};
    CHECK(unit_factory_types(uf, nv, true) == std::vector<int>{1, 3});
    CHECK(unit_factory_types(uf, nv, false) == std::vector<int>{0});
}

TEST_CASE("roster2 airfield: GAAIRC pads = centre + DockingOffset[k] [RE: TL;DR 9]") {
    Building b;
    b.cx = 20; b.cy = 30; b.fw = 3; b.fh = 2;
    const LeptonXY c = building_center_leptons(b);
    CHECK(c.x == 20 * 256 + 384);
    CHECK(c.y == 30 * 256 + 256);
    const std::vector<LeptonXY> off{{0, -128}, {0, 128}, {256, -128}, {256, 128}};
    CHECK(lepton_cell(dock_pad_leptons(b, 0, off)) == Cellxy{21, 30});
    CHECK(lepton_cell(dock_pad_leptons(b, 1, off)) == Cellxy{21, 31});
    CHECK(lepton_cell(dock_pad_leptons(b, 2, off)) == Cellxy{22, 30});
    CHECK(lepton_cell(dock_pad_leptons(b, 3, off)) == Cellxy{22, 31});
    CHECK(lepton_cell(dock_pad_leptons(b, 7, off)) == Cellxy{21, 31});   // out of range -> centre
}

TEST_CASE("roster2 airfield: reload cadence ftol(ReloadRate*900) and pad pick [RE: TL;DR 10]") {
    CHECK(reload_cycle_frames(0.3) == 270);
    CHECK(reload_cycle_frames(0.0) == 1);
    CHECK(first_free_pad({-1, -1, -1, -1}) == 0);
    CHECK(first_free_pad({3, -1, 5, -1}) == 1);
    CHECK(first_free_pad({3, 4, 5, 6}) == -1);
}

TEST_CASE("roster2 airfield: sortie dock -> strike -> RTB -> rearm [RE: TL;DR 9-11]") {
    Sortie s;
    s.home = {0, 0};
    s.x = 0; s.y = 0;
    s.ammo = s.max_ammo = 1;
    s.pad = 0;
    s.speed_lpf = 179.2;           // Speed 14 x 0.75 cps / 15 Hz x 256
    s.range_leptons = 6 * 256;     // Maverick Range=6
    s.has_target = true;
    s.target = {10 * 256, 0};
    std::vector<SortieEvent> ev;
    int frames = 0, fired_frame = -1, landed_frame = -1, reloaded_frame = -1;
    while (frames < 2000) {
        const SortieEvent e = step_sortie(s, 270);
        ++frames;
        if (e != SortieEvent::None) ev.push_back(e);
        if (e == SortieEvent::Fired) fired_frame = frames;
        if (e == SortieEvent::Landed) landed_frame = frames;
        if (e == SortieEvent::Reloaded) { reloaded_frame = frames; break; }
    }
    REQUIRE(ev.size() == 6);
    CHECK(ev[0] == SortieEvent::TookOff);
    CHECK(ev[1] == SortieEvent::ReachedCruise);
    CHECK(ev[2] == SortieEvent::Fired);
    CHECK(ev[3] == SortieEvent::StartedReturn);
    CHECK(ev[4] == SortieEvent::Landed);
    CHECK(ev[5] == SortieEvent::Reloaded);
    CHECK(s.shots == 1);
    CHECK(s.ammo == 1);
    CHECK(s.state == Sortie::State::Docked);
    CHECK(s.x == 0.0);
    CHECK(s.y == 0.0);
    CHECK(s.z == 0);
    CHECK(fired_frame > 15);                         // after the 15-frame climb
    CHECK(reloaded_frame - landed_frame == 270);     // one ReloadRate cycle refills Ammo=1
    CHECK_FALSE(s.has_target);
}

TEST_CASE("roster2 airfield: an empty docked aircraft does not launch [RE: TL;DR 11]") {
    Sortie s;
    s.ammo = 0; s.max_ammo = 1;
    s.has_target = true;
    s.target = {2560, 0};
    s.speed_lpf = 100.0;
    for (int i = 0; i < 269; ++i) CHECK(step_sortie(s, 270) == SortieEvent::None);
    CHECK(s.state == Sortie::State::Docked);
    CHECK(step_sortie(s, 270) == SortieEvent::Reloaded);
    CHECK(s.ammo == 1);
    CHECK(step_sortie(s, 270) == SortieEvent::TookOff);
}

TEST_CASE("roster2 art: DockingOffsetN parses artmd [GAAIRC] incl. inline comments [RE: TL;DR 9]") {
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[GAAIRC]\n"
        "DockingOffset0=0,-128,0      ;-80,80,0 \n"
        "DockingOffset1=0,128,0       ;-80,-208,0\n"
        "DockingOffset2=256,-128,0\n"
        "DockingOffset3= 256 , 128 , 0\n"
        "DockingOffset4=1,2\n");
    ra2assets::Art art = ra2assets::Art::load(ini);
    int v[3] = {9, 9, 9};
    REQUIRE(art.docking_offset("GAAIRC", 0, v));
    CHECK(v[0] == 0); CHECK(v[1] == -128); CHECK(v[2] == 0);
    REQUIRE(art.docking_offset("GAAIRC", 1, v));
    CHECK(v[1] == 128);
    REQUIRE(art.docking_offset("GAAIRC", 3, v));
    CHECK(v[0] == 256); CHECK(v[1] == 128);
    int w[3] = {7, 7, 7};
    CHECK_FALSE(art.docking_offset("GAAIRC", 4, w));   // only 2 fields
    CHECK_FALSE(art.docking_offset("GAAIRC", 5, w));   // absent
    CHECK(w[0] == 7);
}

TEST_CASE("roster2 rules: section_string strips the inline comment [RE: roster2.md §B]") {
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[GAYARD]\nNaval=yes ;GS\nFactory=UnitType\nWaterBound=yes \n");
    ra2assets::Rules rules = ra2assets::Rules::load(ini);
    CHECK(rules.section_string("GAYARD", "Naval", "") == "yes");
    CHECK(rules.section_string("GAYARD", "Factory", "") == "UnitType");
    CHECK(rules.section_string("GAYARD", "WaterBound", "") == "yes");
    CHECK(rules.section_string("GAYARD", "Wall", "no") == "no");
    CHECK(rules.section_flag("GAYARD", "Naval", false));   // RC-5: Ini now cuts the comment like the game
}
