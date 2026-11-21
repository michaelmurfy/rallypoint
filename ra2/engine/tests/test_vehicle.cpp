// tests/test_vehicle.cpp
#include "doctest.h"
#include "world.hpp"
#include "combat.hpp"
#include "voxel_raster.hpp"
#include "ra2assets/vxl.hpp"
#include "ra2assets/hva.hpp"
#include "ra2assets/vpl.hpp"
#include "ra2assets/pal.hpp"
#include <cmath>
#include <cstdint>
#include <vector>

using ra2engine::Unit;
using ra2engine::turn_toward;
using ra2engine::facing_u16_from_delta;

namespace {
// Synthetic voxel-render fixtures for the "nose leads travel" integration pin
// (copied from tests/test_voxel_raster.cpp / the reviewer's probe -- install-
// free, deterministic). Arrow model = a line of 9 voxels along model +X only,
// so its rasterized opaque centroid sits in the "nose" (+X) direction.
ra2assets::VxlModel arrow_model() {
    ra2assets::VxlModel m;
    ra2assets::VxlSection s;
    s.name = "A";
    s.size_x = 9; s.size_y = 1; s.size_z = 1;
    s.scale = 1.0f;
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) s.transform[r][c] = (r == c) ? 1.f : 0.f;
    s.min_bounds[0] = 0.f;   s.max_bounds[0] = 9.f;    // nose extends +X only
    s.min_bounds[1] = -0.5f; s.max_bounds[1] = 0.5f;
    s.min_bounds[2] = -0.5f; s.max_bounds[2] = 0.5f;
    s.normals_mode = 2;
    s.voxels.resize(9);
    for (auto& v : s.voxels) { v.color = 100; v.normal = 0; v.present = true; }
    m.sections.push_back(s);
    return m;
}
ra2assets::HvaModel vh_identity_hva() {
    ra2assets::HvaModel h;
    h.frame_count = 1;
    h.section_names = {"A"};
    ra2assets::HvaSectionXform x{};
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) x.transform[r][c] = (r == c) ? 1.f : 0.f;
    h.frames = {{x}};
    return h;
}
ra2assets::Vpl vh_identity_vpl() {
    std::vector<std::uint8_t> raw(16 + 768 + 32 * 256, 0);
    raw[8] = 32;  // section_count
    for (int i = 0; i < 256; ++i)
        for (int t = 0; t < 32; ++t) raw[16 + 768 + t * 256 + i] = static_cast<std::uint8_t>(i);
    return ra2assets::Vpl::load(raw.data(), raw.size());
}
ra2assets::Palette vh_gray_pal() {
    std::uint8_t raw[768];
    for (int i = 0; i < 256; ++i) raw[i*3] = raw[i*3+1] = raw[i*3+2] = static_cast<std::uint8_t>(i / 4);
    return ra2assets::Palette::load(raw, 768);
}
// Multi-house/Alliances [RE: multihouse.md §3/§4]: step_combat now takes a
// const World& (for World::is_enemy). These vehicle tests configure NO houses,
// so is_enemy(a,b) == (a != b) -- the pre-alliances inlined comparison -- and a
// shared default World (empty map, firewall closed) is stateless and sufficient.
const ra2engine::World& test_world() {
    static ra2assets::MapFile map;
    static ra2engine::World w(map);
    return w;
}
}  // namespace

TEST_CASE("vehicle: turn_toward takes the shortest arc with wraparound") {
    CHECK(turn_toward(0.0, 100.0, 300.0) == doctest::Approx(100.0));     // within step: clamp at target
    CHECK(turn_toward(0.0, 1000.0, 300.0) == doctest::Approx(300.0));    // step toward, positive arc
    CHECK(turn_toward(1000.0, 0.0, 300.0) == doctest::Approx(700.0));    // step back, negative arc
    // wraparound: 65000 -> 200 is a +736 arc (through 0), not -64800
    CHECK(turn_toward(65000.0, 200.0, 300.0) == doctest::Approx(65300.0));  // steps toward the wrap
    CHECK(turn_toward(65400.0, 200.0, 400.0) == doctest::Approx(200.0));    // arc 336 <= step 400: reaches target through 0
    // result always normalized to [0, 65536)
    CHECK(turn_toward(65500.0, 300.0, 100.0) == doctest::Approx(64.0));  // 65500 + 100 = 65600 -> 64
}

TEST_CASE("vehicle: facing_u16_from_delta lands in the recovered render space") {
    // The vehicle facing space is the ORIGINAL's RENDER space
    // (docs/re/voxel-render.md §2), the space facing_to_index/index_to_yaw are
    // calibrated to -- NOT infantry's 8-block facing_from_delta space. Facing
    // 0x4000 = screen East (down-right diagonal) = the unrotated +X-nose model,
    // and facing increases CLOCKWISE. So the four cardinal cell rays pin to the
    // four exact render facings (and their 32-way indices):
    CHECK(facing_u16_from_delta(1.0, 0.0) == doctest::Approx(16384.0));   // +x -> East
    CHECK(facing_u16_from_delta(0.0, 1.0) == doctest::Approx(32768.0));   // +y -> South (spawn body_facing)
    CHECK(facing_u16_from_delta(-1.0, 0.0) == doctest::Approx(49152.0));  // -x -> West
    CHECK(facing_u16_from_delta(0.0, -1.0) == doctest::Approx(0.0));      // -y -> North
    CHECK(ra2engine::facing_to_index(facing_u16_from_delta(1.0, 0.0), 32) == 8);
    CHECK(ra2engine::facing_to_index(facing_u16_from_delta(0.0, 1.0), 32) == 16);
    CHECK(ra2engine::facing_to_index(facing_u16_from_delta(-1.0, 0.0), 32) == 24);
    CHECK(ra2engine::facing_to_index(facing_u16_from_delta(0.0, -1.0), 32) == 0);
}

TEST_CASE("vehicle: rendered nose leads the travel direction") {
    // Integration pin (reviewer's probe methodology): drive a +X-heavy "arrow"
    // model through the FULL production chain -- facing_u16_from_delta ->
    // facing_to_index -> index_to_yaw -> rasterize_model -- for cell travel
    // directions, and assert the rasterized opaque centroid (the nose) points
    // within +/-30 deg of the on-screen travel direction. Proves the vehicle
    // facing space and the render facing space (voxel-render.md §2) agree.
    ra2assets::VxlModel arrow = arrow_model();
    ra2assets::HvaModel hva = vh_identity_hva();
    ra2assets::Vpl vpl = vh_identity_vpl();
    ra2assets::Palette pal = vh_gray_pal();

    const double kPi = 3.14159265358979323846;
    const double dirs[3][2] = {{1, 0}, {0, 1}, {-1, 0}};
    for (const auto& d : dirs) {
        const double f = facing_u16_from_delta(d[0], d[1]);
        const int idx = ra2engine::facing_to_index(f, 32);
        ra2engine::VoxelSprite s =
            ra2engine::rasterize_model(arrow, hva, ra2engine::index_to_yaw(idx, 32), vpl, pal);
        // Opaque centroid offset from the model anchor (the nose direction).
        double sx = 0, sy = 0; int n = 0;
        for (int y = 0; y < s.h; ++y)
            for (int x = 0; x < s.w; ++x)
                if (s.rgba[(static_cast<size_t>(y) * s.w + x) * 4 + 3] != 0) { sx += x; sy += y; ++n; }
        REQUIRE(n > 0);
        const double nx = sx / n - s.anchor_x, ny = sy / n - s.anchor_y;
        // Projected on-screen travel direction (60x30 diamond shear, y down).
        const double ta = std::atan2((d[0] + d[1]) * 15.0, (d[0] - d[1]) * 30.0);
        const double na = std::atan2(ny, nx);
        double err = std::fmod(na - ta + 3.0 * kPi, 2.0 * kPi) - kPi;  // wrap to (-pi, pi]
        CHECK(std::abs(err) <= 30.0 * kPi / 180.0);  // nose leads motion within 30 deg
    }
}

namespace {
std::vector<std::uint8_t> open_grid(int w, int h) {
    return std::vector<std::uint8_t>(static_cast<size_t>(w) * h, 1);
}
Unit tank_at(int x, int y) {
    Unit u;
    u.cell_x = x; u.cell_y = y; u.fx = x; u.fy = y;
    u.is_vehicle = true;
    u.rot_u16_per_sec = 5 * ra2engine::kRotU16PerRotPerSec;  // ROT=5
    u.speed_cps = 4.5;
    return u;
}
}  // namespace

TEST_CASE("vehicle: turns in place before rolling, then moves at its own speed") {
    std::vector<Unit> units = {tank_at(5, 5)};
    // Spawn faces S (32768); path due N-ish: goal (4,4) -> want facing 0-block.
    units[0].path = {{5, 5}, {4, 4}};
    units[0].path_idx = 1;
    ra2engine::Occupancy occ(16, 16);
    occ.set(5, 5, 0);
    auto grid = open_grid(16, 16);
    const ra2engine::AnimTable anims;

    // First small step: facing is ~180 degrees off -> must rotate, not move.
    ra2engine::step_units(units, grid, grid, 16, 16, occ, 1.0 / 60.0, 3.0, anims);
    CHECK(units[0].fx == doctest::Approx(5.0));
    CHECK(units[0].fy == doctest::Approx(5.0));
    CHECK(units[0].body_facing != doctest::Approx(32768.0));  // it turned

    // Run 2 sim-seconds: ROT=5 covers 180 degrees in ~1.7s, then it rolls.
    for (int i = 0; i < 120; ++i)
        ra2engine::step_units(units, grid, grid, 16, 16, occ, 1.0 / 60.0, 3.0, anims);
    CHECK((units[0].fx != doctest::Approx(5.0) || units[0].fy != doctest::Approx(5.0)));

    // Per-unit speed: a fresh tank on a long straight path outruns move_cps.
    std::vector<Unit> fast = {tank_at(1, 8)}, slow = {tank_at(1, 8)};
    fast[0].body_facing = facing_u16_from_delta(1.0, 0.0);   // pre-aligned
    slow[0].body_facing = facing_u16_from_delta(1.0, 0.0);
    slow[0].is_vehicle = false; slow[0].speed_cps = 0.0;     // infantry control
    for (auto* v : {&fast, &slow}) {
        (*v)[0].path.clear();
        for (int x = 1; x <= 14; ++x) (*v)[0].path.push_back({x, 8});
        (*v)[0].path_idx = 1;
    }
    ra2engine::Occupancy o2(16, 16), o3(16, 16);
    o2.set(1, 8, 0); o3.set(1, 8, 0);
    for (int i = 0; i < 60; ++i) {
        ra2engine::step_units(fast, grid, grid, 16, 16, o2, 1.0 / 60.0, 3.0, anims);
        ra2engine::step_units(slow, grid, grid, 16, 16, o3, 1.0 / 60.0, 3.0, anims);
    }
    CHECK(fast[0].fx > slow[0].fx);  // 4.5 cps vs 3.0 cps after 1s
}

TEST_CASE("vehicle: turret slews to target and returns to body when disengaged") {
    std::vector<Unit> units = {tank_at(5, 5), tank_at(9, 9)};
    units[1].is_vehicle = false;  // any target works
    units[0].target = 1;
    units[1].hp = units[1].max_hp = 100;
    units[0].hp = units[0].max_hp = 100;
    const double want = facing_u16_from_delta(units[1].fx - units[0].fx,
                                              units[1].fy - units[0].fy);
    std::vector<ra2engine::Building> no_buildings;
    for (int i = 0; i < 300; ++i) ra2engine::step_turrets(units, no_buildings, 1.0 / 60.0);
    CHECK(units[0].turret_facing == doctest::Approx(want));
    units[0].target = -1;
    for (int i = 0; i < 300; ++i) ra2engine::step_turrets(units, no_buildings, 1.0 / 60.0);
    CHECK(units[0].turret_facing == doctest::Approx(units[0].body_facing));
}

TEST_CASE("vehicle: fire waits for turret aim; infantry does not") {
    // Two hostile units in range of each other. The vehicle's turret starts
    // pointing away; step_combat alone (no step_turrets) must never let it
    // fire, then after slewing it fires on the next combat step.
    std::vector<Unit> units = {tank_at(5, 5), tank_at(7, 5)};
    for (auto& u : units) { u.hp = u.max_hp = 400; u.w_damage = 90; u.w_rof = 65;
                            u.w_range = 5.75; u.w_verses = {100}; u.alive = true; }
    units[0].house = 0; units[1].house = 1;
    units[0].target = 1; units[0].attack_order = true;
    units[1].is_vehicle = false;   // control: infantry-rules unit fires immediately
    units[1].target = 0; units[1].attack_order = true;
    units[0].turret_facing = turn_toward(facing_u16_from_delta(1.0, 0.0), 0.0, 20000.0);  // way off
    ra2engine::Occupancy occ(16, 16);
    occ.set(5, 5, 0); occ.set(7, 5, 1);

    std::vector<ra2engine::Building> bs;
    ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 15.0);
    CHECK(units[1].hp == 400);          // vehicle held fire (not aimed)
    CHECK(units[0].hp < 400);           // infantry-rules unit fired

    // Aim EXACTLY (tolerance 0): use the same delta step_combat computes,
    // (t.fx - u.fx, t.fy - u.fy) = (2, 0), so the doubles compare equal.
    units[0].turret_facing = facing_u16_from_delta(2.0, 0.0);
    ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 15.0);
    CHECK(units[1].hp < 400);           // vehicle fired once aimed
}

TEST_CASE("vehicle: fire at a building waits for turret aim, then fires after step_turrets") {
    // Mirrors "vehicle: fire waits for turret aim; infantry does not" but for
    // an attack-building order (Buildings & Construction Task 4 review fix
    // 2): step_turrets must slew a vehicle toward the building's NEAREST
    // foundation cell, and step_combat's building-fire path must gate on
    // vehicle_aimed exactly like the unit-vs-unit path.
    std::vector<Unit> units = {tank_at(5, 5)};
    units[0].hp = units[0].max_hp = 400; units[0].w_damage = 90; units[0].w_rof = 65;
    units[0].w_range = 5.75; units[0].w_verses = {100}; units[0].alive = true;
    units[0].house = 0;
    units[0].target_building = 0; units[0].attack_order = true;
    // Turret starts at its default (South, body_facing 32768) -- the
    // building sits due EAST (facing 16384) -- so it starts unaligned
    // without any manual override.

    std::vector<ra2engine::Building> bs(1);
    ra2engine::Building& b = bs[0];
    b.state = ra2engine::Building::State::Idle; b.alive = true; b.house = 1;
    b.cx = 7; b.cy = 5; b.fw = 1; b.fh = 1;  // nearest cell (7,5), dist 2 <= 5.75
    b.hp = b.max_hp = 180; b.armor_ix = 0;

    ra2engine::Occupancy occ(16, 16);
    occ.set(5, 5, 0);

    // step_combat alone (no step_turrets) must never let it fire.
    ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 15.0);
    CHECK(b.hp == 180);           // vehicle held fire (not aimed)

    // Slew the turret via step_turrets toward the building's nearest cell.
    for (int i = 0; i < 300; ++i) ra2engine::step_turrets(units, bs, 1.0 / 60.0);
    const double want = facing_u16_from_delta(7.0 - 5.0, 5.0 - 5.0);
    CHECK(units[0].turret_facing == doctest::Approx(want));

    ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 15.0);
    CHECK(b.hp < 180);            // vehicle fired once aimed
}
