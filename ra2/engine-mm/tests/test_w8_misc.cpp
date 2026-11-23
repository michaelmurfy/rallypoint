// Wave-8 fix (w8fix-misc): docs/gap/wave8 root causes 9 and 10.
//   W8-06-05 Chronosphere destination layer on a high-bridge cell (0x717600).
//   W8-06-06 the Chronosphere ejects a riding parasite (0x7188D9).
//   W8-04-05 a Robot Tank switched off over water spins, then drops and dies
//            (0x516A10 / 0x514969..0x514A2C / 0x5F3970 / 0x5F3680).
#include "doctest.h"

#include "asset_fixture.hpp"
#include "harvest.hpp"   // OreValues
#include "locomotion.hpp"
#include "world.hpp"

using namespace ra2engine;

namespace {

UnitCombat w8_unit(const char* image, int strength, bool vehicle) {
    UnitCombat c;
    c.image = image;
    c.strength = strength;
    c.armor = "heavy";
    c.is_vehicle = vehicle;
    if (vehicle) c.rot = 5;
    return c;
}

bool w8_open_rect(const World& w, int W, int H, int& x0, int& y0) {
    for (int y = 4; y + H < w.grid_h() - 4; ++y)
        for (int x = 4; x + W < w.grid_w() - 4; ++x) {
            bool ok = true;
            for (int dy = 0; dy < H && ok; ++dy)
                for (int dx = 0; dx < W && ok; ++dx)
                    if (!w.walkable(x + dx, y + dy) || w.unit_at_cell(x + dx, y + dy) >= 0) ok = false;
            if (ok) { x0 = x; y0 = y; return true; }
        }
    return false;
}

// A world with a charged Chronosphere for house 0 and a 16x8 open rectangle.
#define W8_CHRONO_WORLD(world, x0, y0)                                       \
    if (!ra2fixture::mapsmd03_available()) {                                 \
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR"); \
        return;                                                              \
    }                                                                        \
    World world(ra2fixture::mapsmd03_map());                                 \
    int x0 = 0, y0 = 0;                                                      \
    REQUIRE(w8_open_rect(world, 16, 8, x0, y0));                             \
    world.grant_superweapon_type(0, SuperweaponType::ChronoSphere, 0.2, false); \
    for (int f = 0; f < 60 && !world.sw_ready(0, SuperweaponType::ChronoSphere); ++f) world.step(1.0 / 15.0); \
    REQUIRE(world.sw_ready(0, SuperweaponType::ChronoSphere));

}  // namespace

TEST_CASE("w8misc W8-06-05: a tank chronoshifted onto a water deck lands ON it; the ship under it lives") {
    W8_CHRONO_WORLD(world, x0, y0);
    const int W = world.grid_w();
    const int sx = x0 + 2, sy = y0 + 3, tx = x0 + 11, ty = y0 + 3;
    std::vector<std::uint8_t> hb(static_cast<std::size_t>(W) * world.grid_h(), 0);
    hb[static_cast<std::size_t>(ty) * W + tx] = 1;   // a deck over water
    world.set_high_bridges(hb);
    const int tank = static_cast<int>(world.units().size());
    world.add_combat_unit(sx, sy, 0, w8_unit("MTNK", 400, true));
    const int ship = static_cast<int>(world.units().size());
    UnitCombat dest = w8_unit("DEST", 600, true);
    dest.is_naval = true;
    dest.movement_domain = MovementDomain::Water;
    world.add_combat_unit(tx, ty, 1, dest);
    world.units()[static_cast<std::size_t>(ship)].on_deck = 0;   // under the span
    REQUIRE(world.fire_superweapon_at(0, SuperweaponType::ChronoSphere, sx, sy, tx, ty));
    const Unit& t = world.units()[static_cast<std::size_t>(tank)];
    CHECK(t.cell_x == tx);
    CHECK(t.cell_y == ty);
    CHECK(t.on_deck == 1);
    const Unit& s = world.units()[static_cast<std::size_t>(ship)];
    CHECK(s.alive);
    CHECK(s.die_time < 0.0);
    CHECK(s.hp == s.max_hp);
}

TEST_CASE("w8misc W8-06-05: on a bridge over land the arrival lands on the deck; a tank under it is not telefragged") {
    W8_CHRONO_WORLD(world, x0, y0);
    const int W = world.grid_w();
    const int sx = x0 + 2, sy = y0 + 3, tx = x0 + 11, ty = y0 + 3;
    std::vector<std::uint8_t> hb(static_cast<std::size_t>(W) * world.grid_h(), 0);
    hb[static_cast<std::size_t>(ty) * W + tx] = 2;   // a deck over land
    world.set_high_bridges(hb);
    const int tank = static_cast<int>(world.units().size());
    world.add_combat_unit(sx, sy, 0, w8_unit("MTNK", 400, true));
    const int under = static_cast<int>(world.units().size());
    world.add_combat_unit(tx, ty, 1, w8_unit("HTNK", 400, true));
    world.units()[static_cast<std::size_t>(under)].on_deck = 0;
    REQUIRE(world.fire_superweapon_at(0, SuperweaponType::ChronoSphere, sx, sy, tx, ty));
    CHECK(world.units()[static_cast<std::size_t>(tank)].on_deck == 1);
    CHECK(world.units()[static_cast<std::size_t>(tank)].cell_x == tx);
    const Unit& u = world.units()[static_cast<std::size_t>(under)];
    CHECK(u.die_time < 0.0);
    CHECK(u.hp == u.max_hp);
}

TEST_CASE("w8misc W8-06-05: deck occupants are telefragged by a deck arrival; plain ground still telefrags") {
    W8_CHRONO_WORLD(world, x0, y0);
    const int W = world.grid_w();
    const int sx = x0 + 2, sy = y0 + 3, tx = x0 + 11, ty = y0 + 3;
    std::vector<std::uint8_t> hb(static_cast<std::size_t>(W) * world.grid_h(), 0);
    hb[static_cast<std::size_t>(ty) * W + tx] = 2;
    world.set_high_bridges(hb);
    world.add_combat_unit(sx, sy, 0, w8_unit("MTNK", 400, true));
    const int on_deck = static_cast<int>(world.units().size());
    world.add_combat_unit(tx, ty, 1, w8_unit("HTNK", 400, true));
    world.units()[static_cast<std::size_t>(on_deck)].on_deck = 1;
    // A second mover on plain ground (offset one row down) onto an occupant.
    world.add_combat_unit(sx, sy + 1, 0, w8_unit("MTNK", 400, true));
    const int ground = static_cast<int>(world.units().size());
    world.add_combat_unit(tx, ty + 1, 1, w8_unit("HTNK", 400, true));
    REQUIRE(world.fire_superweapon_at(0, SuperweaponType::ChronoSphere, sx, sy, tx, ty));
    CHECK(world.units()[static_cast<std::size_t>(on_deck)].die_time >= 0.0);
    CHECK(world.units()[static_cast<std::size_t>(ground)].die_time >= 0.0);
}

TEST_CASE("w8misc W8-06-06: the Chronosphere ejects a riding Terror Drone, paralysed, at the source") {
    W8_CHRONO_WORLD(world, x0, y0);
    const int sx = x0 + 2, sy = y0 + 3, tx = x0 + 11, ty = y0 + 3;
    const int host = static_cast<int>(world.units().size());
    world.add_combat_unit(sx, sy, 0, w8_unit("MTNK", 400, true));
    const int drone = static_cast<int>(world.units().size());
    world.add_combat_unit(x0 + 14, y0 + 7, 1, w8_unit("DRON", 100, true));
    world.units()[static_cast<std::size_t>(drone)].cargo_of = host;
    world.units()[static_cast<std::size_t>(drone)].parasite_next_tick = 1 << 30;
    world.units()[static_cast<std::size_t>(host)].parasite_on_me = drone;
    REQUIRE(world.fire_superweapon_at(0, SuperweaponType::ChronoSphere, sx, sy, tx, ty));
    const Unit& h = world.units()[static_cast<std::size_t>(host)];
    const Unit& d = world.units()[static_cast<std::size_t>(drone)];
    CHECK(h.cell_x == tx);
    CHECK(h.parasite_on_me == -1);
    CHECK(d.alive);
    CHECK(d.cargo_of == -1);
    CHECK(std::abs(d.cell_x - sx) <= 1);   // left beside the source cell
    CHECK(std::abs(d.cell_y - sy) <= 1);
    CHECK(d.w7.paralysed_until > static_cast<std::int64_t>(world.ability_frame()));
}

namespace {
// A water corridor (rows 50/51, x 60..90 of mapsmd03; see test_world's naval
// cases), a powered Robot Tank (Hover locomotor) and its control centre.
struct RoboRig {
    int robo = -1;
    int rcc = -1;
};
RoboRig robo_rig(World& world, int rx, int ry) {
    using ra2assets::LandType;
    const int gw = world.grid_w(), gh = world.grid_h();
    std::vector<LandType> lt(static_cast<std::size_t>(gw) * gh, LandType::Clear);
    for (int x = 60; x <= 90; ++x) {
        lt[static_cast<std::size_t>(50) * gw + x] = LandType::Water;
        lt[static_cast<std::size_t>(51) * gw + x] = LandType::Water;
    }
    world.set_water_terrain(lt);
    BuildingTypeDef rcc;
    rcc.id = "GAROBO";
    rcc.fw = 2;
    rcc.fh = 2;
    rcc.w6.powers_unit = "ROBO";
    world.set_economy({rcc}, {}, OreValues{}, 600.0, 0.0, 0.0);
    RoboRig r;
    r.rcc = world.add_building(0, 74, 60, 2, 2, 0, 1000, 0, 0.0, true);
    UnitCombat c = w8_unit("ROBO", 200, true);
    c.w6.powered_unit = true;
    c.loco.kind = static_cast<int>(LocoKind::Hover);
    c.movement_domain = MovementDomain::Amphibious;
    r.robo = static_cast<int>(world.units().size());
    world.add_combat_unit(rx, ry, 0, c);
    return r;
}
}  // namespace

TEST_CASE("w8misc W8-04-05: a Robot Tank cut off over water spins 20-30 frames, then drops and dies") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: no install"); return; }
    World world(ra2fixture::mapsmd03_map());
    const RoboRig r = robo_rig(world, 70, 50);
    REQUIRE(world.is_water_cell(70, 50));
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 5; ++f) world.step(dt);
    REQUIRE(world.units()[static_cast<std::size_t>(r.robo)].alive);
    CHECK_FALSE(world.units()[static_cast<std::size_t>(r.robo)].w6.unpowered);
    world.buildings()[static_cast<std::size_t>(r.rcc)].power_off = true;   // the control centre goes
    world.step(dt);
    const Unit& u = world.units()[static_cast<std::size_t>(r.robo)];
    CHECK(u.w6.unpowered);
    const int spin = u.w6.hover_spin;
    CHECK(std::abs(spin) >= 19);   // Random(20,30), one step already taken
    CHECK(std::abs(spin) <= 29);
    const double face0 = u.body_facing;
    world.step(dt);
    CHECK(world.units()[static_cast<std::size_t>(r.robo)].body_facing != face0);   // spinning
    CHECK(world.units()[static_cast<std::size_t>(r.robo)].die_time < 0.0);
    int died_at = -1;
    for (int f = 0; f < 40 && died_at < 0; ++f) {
        world.step(dt);
        if (world.units()[static_cast<std::size_t>(r.robo)].die_time >= 0.0 ||
            !world.units()[static_cast<std::size_t>(r.robo)].alive)
            died_at = f;
    }
    CHECK(died_at >= 0);
    CHECK(died_at + 2 >= std::abs(spin));   // not before the spin ends
}

TEST_CASE("w8misc W8-04-05: a Robot Tank cut off on land spins and survives") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: no install"); return; }
    World world(ra2fixture::mapsmd03_map());
    const RoboRig r = robo_rig(world, 70, 56);
    REQUIRE_FALSE(world.is_water_cell(70, 56));
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 5; ++f) world.step(dt);
    world.buildings()[static_cast<std::size_t>(r.rcc)].power_off = true;
    world.step(dt);
    CHECK(world.units()[static_cast<std::size_t>(r.robo)].w6.hover_spin != 0);
    for (int f = 0; f < 60; ++f) world.step(dt);
    const Unit& u = world.units()[static_cast<std::size_t>(r.robo)];
    CHECK(u.alive);
    CHECK(u.die_time < 0.0);
    CHECK(u.w6.hover_spin == 0);
    CHECK(u.w6.unpowered);
}
