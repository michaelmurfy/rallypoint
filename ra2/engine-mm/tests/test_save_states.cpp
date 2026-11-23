// W8-06-01 + audit (w8fix-save): save -> load round trips in every "special"
// state a unit can hold when the phone autosaves at an arbitrary frame --
// a paratrooper under its chute (superweapon and team planes), a garrison,
// a chrono-erase in progress, a riding parasite, spawner nodes with their
// spawns out, a rocket in flight, a unit on a high-bridge deck and a
// transport inside a transport. Each case: save(load(x)) == x, and the saved
// and the loaded World run on bit-identically to the end of the state.
// W8-05-07: the paradrop plane's jump cadence [RE: FUN_00415960 / 00415C60].
#include "doctest.h"

#include <functional>
#include <set>

#include "../engine/save_fields.hpp"
#include "../engine/savegame.hpp"
#include "air_naval.hpp"
#include "asset_fixture.hpp"
#include "cargo.hpp"
#include "combat.hpp"
#include "harvest.hpp"   // OreValues (set_economy)
#include "world.hpp"

using namespace ra2engine;
using namespace ra2engine::save;

namespace {
constexpr double kDt = 1.0 / 15.0;
const std::vector<int> kAll100 = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};

UnitCombat mk(const char* image, int strength, const char* armor, int dmg, int rof, double range,
              std::vector<int> verses, bool vehicle = false) {
    UnitCombat c;
    c.image = image;
    c.strength = strength;
    c.armor = armor;
    c.damage = dmg;
    c.rof = rof;
    c.range = range;
    c.verses = std::move(verses);
    c.is_vehicle = vehicle;
    if (vehicle) c.rot = 5;
    return c;
}

UnitCombat gi() {
    UnitCombat c = mk("E1", 125, "none", 0, 20, 0.0, kAll100);
    c.is_infantry = true;
    return c;
}

UnitCombat pdplane() {
    UnitCombat c;
    c.image = "PDPLANE";
    c.strength = 400;
    c.armor = "light";
    c.is_aircraft = true;
    c.is_vehicle = true;
    c.verses = std::vector<int>(11, 0);
    c.an.fixed_wing = true;
    c.an.fly_speed_cps = 2.2;
    c.flight_turn = 2 * 256.0 * 15.0;   // [PDPLANE] ROT=2 (w2air_main.inc)
    return c;
}

bool find_open_rect(const World& w, int W, int H, int& x0, int& y0) {
    for (int y = 6; y + H < w.grid_h() - 6; ++y)
        for (int x = 6; x + W < w.grid_w() - 6; ++x) {
            bool ok = true;
            for (int dy = 0; dy < H && ok; ++dy)
                for (int dx = 0; dx < W && ok; ++dx)
                    if (!w.walkable(x + dx, y + dy) || w.unit_at_cell(x + dx, y + dy) >= 0 ||
                        w.building_at_cell(x + dx, y + dy) >= 0)
                        ok = false;
            if (ok) { x0 = x; y0 = y; return true; }
        }
    return false;
}

// The scenario every World of a case starts from (both the saved one and the
// one the save is loaded over -- the game loads over a freshly built scenario).
void base(World& w) {
    w.set_house(0, {World::Side::Allied, 0, "Americans", 0, true});
    w.set_house(1, {World::Side::Soviet, 1, "Russians", 1, true});
    w.enable_subcells();
    w.register_an_type("PDPLANE", pdplane());
    w.register_an_type("E1", gi());
}

using Setup = std::function<void(World&)>;

// save(load(x)) == x, then both run `frames` and stay byte-identical.
void roundtrip(World& a, const Setup& setup, int frames, World* out_b = nullptr) {
    const std::vector<std::uint8_t> s0 = save_world(a);
    World local(ra2fixture::mapsmd03_map());
    World& b = out_b ? *out_b : local;
    setup(b);
    REQUIRE(load_world(b, s0));
    CHECK(save_world(b) == s0);
    for (int f = 0; f < frames; ++f) {
        a.step(kDt);
        b.step(kDt);
    }
    CHECK(save_world(a) == save_world(b));
}

int falling(const World& w) {
    int n = 0;
    for (const Unit& u : w.units())
        if (u.alive && u.cargo_of == kParachuteCargo) ++n;
    return n;
}
int count_image(const World& w, const char* img) {
    int n = 0;
    for (const Unit& u : w.units())
        if (u.alive && u.die_time < 0.0 && u.image == img && u.cargo_of != kParachuteCargo) ++n;
    return n;
}
}  // namespace

#define SAVE_WORLD(world)                                                    \
    if (!ra2fixture::mapsmd03_available()) {                                 \
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR"); \
        return;                                                              \
    }                                                                        \
    World world(ra2fixture::mapsmd03_map());

TEST_CASE("w8save W8-06-01: the validator accepts both cargo_of sentinels and still refuses garbage") {
    ra2assets::MapFile map;
    auto loads = [&](int cargo_of) {
        World c(map);
        c.add_combat_unit(0, 0, 0, gi());
        c.units()[0].cargo_of = cargo_of;
        World d(map);
        return load_world(d, save_world(c));
    };
    CHECK(loads(-1));
    CHECK(loads(kParachuteCargo));   // W8-06-01: was refused ("unit reference")
    CHECK(loads(kGarrisonCargo));
    CHECK_FALSE(loads(1));
    CHECK_FALSE(loads(kParachuteCargo - 1));
    CHECK_FALSE(loads(-2));
}

TEST_CASE("w8save W8-06-01: a superweapon paradrop saved mid-fall loads, falls and lands identically (skirmish)") {
    SAVE_WORLD(a);
    base(a);
    int x0 = 0, y0 = 0;
    REQUIRE(find_open_rect(a, 10, 10, x0, y0));
    a.grant_superweapon_type(0, SuperweaponType::AmerParaDrop, 0.01, false);
    a.step(kDt);
    REQUIRE(a.fire_superweapon_at(0, SuperweaponType::AmerParaDrop, x0 + 5, y0 + 5));
    int plane = -1;
    for (int i = 0; i < static_cast<int>(a.units().size()); ++i)
        if (a.units()[i].image == "PDPLANE") plane = i;
    REQUIRE(plane >= 0);
    for (int f = 0; f < 3000 && !(falling(a) >= 2 && a.units()[plane].an.drop_left > 0); ++f) a.step(kDt);
    REQUIRE(falling(a) >= 2);
    REQUIRE(a.units()[plane].an.drop_left > 0);   // the plane is still over the site
    // The falling trooper as saved.
    int t = -1;
    for (int i = 0; i < static_cast<int>(a.units().size()); ++i)
        if (a.units()[i].cargo_of == kParachuteCargo) { t = i; break; }
    const double alt = a.units()[t].an.para_alt;
    REQUIRE(alt > 0.0);

    World b(ra2fixture::mapsmd03_map());
    roundtrip(a, base, 0, &b);
    CHECK(b.units()[t].cargo_of == kParachuteCargo);   // parachute state
    CHECK(b.units()[t].an.para_alt == alt);            // altitude
    CHECK(b.occupancy().spot_of(b.units()[t].cell_x, b.units()[t].cell_y, t) ==
          a.occupancy().spot_of(a.units()[t].cell_x, a.units()[t].cell_y, t));
    for (int f = 0; f < 600; ++f) {
        a.step(kDt);
        b.step(kDt);
    }
    CHECK(save_world(a) == save_world(b));
    CHECK(falling(b) == 0);              // everyone landed
    CHECK(count_image(b, "E1") == 8);    // AmerParaDropNum=8
    CHECK(b.units()[t].cargo_of == -1);
    CHECK(b.units()[t].cell_x == a.units()[t].cell_x);
    CHECK(b.units()[t].cell_y == a.units()[t].cell_y);
}

TEST_CASE("w8save W8-06-01: a team (reinforcement) paradrop saved mid-drop loads and lands identically (campaign)") {
    SAVE_WORLD(a);
    base(a);
    int x0 = 0, y0 = 0;
    REQUIRE(find_open_rect(a, 10, 10, x0, y0));
    const int plane = a.spawn_drop_plane("PDPLANE", 0, 0, y0 + 5, x0 + 5, y0 + 5, a.grid_w() - 1, y0 + 5);
    REQUIRE(plane >= 0);
    for (int k = 0; k < 5; ++k) REQUIRE(a.add_unit_in_transport(plane, 0, gi()) >= 0);
    for (int f = 0; f < 3000 && !(falling(a) >= 1 && !a.units()[plane].cargo.empty()); ++f) a.step(kDt);
    REQUIRE(falling(a) >= 1);
    REQUIRE_FALSE(a.units()[plane].cargo.empty());
    World b(ra2fixture::mapsmd03_map());
    roundtrip(a, base, 600, &b);
    CHECK(falling(b) == 0);
    CHECK(count_image(b, "E1") == 5);
    CHECK(b.units()[plane].cargo.empty());
}

TEST_CASE("w8save W8-05-07: one jump every 5 frames from the plane, troopers share cells, never water or buildings") {
    SAVE_WORLD(w);
    base(w);
    int x0 = 0, y0 = 0;
    REQUIRE(find_open_rect(w, 10, 10, x0, y0));
    w.grant_superweapon_type(0, SuperweaponType::AmerParaDrop, 0.01, false);
    w.step(kDt);
    REQUIRE(w.fire_superweapon_at(0, SuperweaponType::AmerParaDrop, x0 + 5, y0 + 5));
    int plane = -1;
    for (int i = 0; i < static_cast<int>(w.units().size()); ++i)
        if (w.units()[i].image == "PDPLANE") plane = i;
    REQUIRE(plane >= 0);
    std::vector<int> jump_frames;
    std::vector<double> plane_dist;
    for (int f = 0; f < 3000 && w.units()[plane].an.drop_left > 0; ++f) {
        const int before = w.units()[plane].an.drop_left;
        w.step(kDt);
        if (w.units()[plane].an.drop_left < before) {
            jump_frames.push_back(f);
            const Unit& t = w.units().back();
            const Unit& p = w.units()[plane];
            plane_dist.push_back(std::hypot(t.cell_x - p.fx, t.cell_y - p.fy));
        }
    }
    REQUIRE(jump_frames.size() == 8);
    for (std::size_t k = 1; k < jump_frames.size(); ++k) CHECK((jump_frames[k] - jump_frames[0]) % 5 == 0);
    for (double d : plane_dist) CHECK(d < 1.5);   // under the plane, half a cell off its track
    for (int f = 0; f < 600; ++f) w.step(kDt);
    std::set<std::pair<int, int>> cells;
    int e1 = 0;
    for (const Unit& u : w.units()) {
        if (u.image != "E1" || !u.alive) continue;
        ++e1;
        cells.insert({u.cell_x, u.cell_y});
        CHECK(w.walkable(u.cell_x, u.cell_y));
        CHECK(w.building_at_cell(u.cell_x, u.cell_y) < 0);
        CHECK(u.cargo_of == -1);
    }
    CHECK(e1 == 8);
    for (const Unit& u : w.units())   // FindInfantrySubposition: each holds a spot, not a whole cell
        if (u.image == "E1" && u.alive) CHECK(w.occupancy().spot_of(u.cell_x, u.cell_y, static_cast<int>(&u - w.units().data())) >= 2);
}

TEST_CASE("w8save W8-05-07: a refused jump stays aboard and the plane loops back over the target") {
    SAVE_WORLD(w);
    base(w);
    int x0 = 0, y0 = 0;
    REQUIRE(find_open_rect(w, 12, 12, x0, y0));
    // A vehicle on every cell around the target: no free spot on the first
    // passes. The vehicles leave after a while; the plane is still looping.
    std::vector<int> blockers;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            w.add_combat_unit(x0 + 6 + dx, y0 + 6 + dy, 1, mk("HTNK", 400, "heavy", 0, 60, 0.0, {}, true));
            blockers.push_back(static_cast<int>(w.units().size()) - 1);
        }
    SwParams sw;
    sw.amer_para_num = 3;
    sw.paradrop_radius = 256;   // only the cells right around the target
    w.set_sw_params(sw);
    w.grant_superweapon_type(0, SuperweaponType::AmerParaDrop, 0.01, false);
    w.step(kDt);
    REQUIRE(w.fire_superweapon_at(0, SuperweaponType::AmerParaDrop, x0 + 6, y0 + 6));
    const int plane = static_cast<int>(w.units().size()) - 1;
    REQUIRE(w.units()[plane].image == "PDPLANE");
    bool was_over = false, left_and_back = false, out_after_over = false;
    int f = 0;
    for (; f < 1500 && !left_and_back; ++f) {
        w.step(kDt);
        const AnUnit& a = w.units()[plane].an;
        if (a.para_overfly) {
            if (out_after_over) left_and_back = true;
            was_over = true;
        } else if (was_over) {
            out_after_over = true;
        }
    }
    CHECK(left_and_back);                            // Overfly -> Approach -> Overfly
    CHECK(w.units()[plane].an.drop_left == 3);       // nothing could jump yet
    CHECK(w.units()[plane].an.para_passes >= 0);     // still trying
    for (std::size_t k = 0; k < blockers.size(); ++k)   // the tanks drive off: the cells clear
        w.order_unit_move(blockers[k], x0 + static_cast<int>(k), y0 + 11);
    for (int k = 0; k < 3000 && w.units()[plane].an.drop_left > 0; ++k) {
        w.step(kDt);
    }
    CHECK(w.units()[plane].an.drop_left == 0);
}

TEST_CASE("w8save audit: a garrisoned building round-trips (kGarrisonCargo, garrison_of)") {
    SAVE_WORLD(a);
    int x0 = 0, y0 = 0;
    REQUIRE(find_open_rect(a, 16, 8, x0, y0));
    auto setup = [&](World& w) {
        base(w);
        BuildingTypeDef civ;
        civ.id = "CABHUT";
        civ.fw = 2;
        civ.fh = 2;
        civ.cost = 800;
        civ.tech_level = 1;
        civ.can_be_occupied = true;
        civ.can_occupy_fire = true;
        civ.max_occupants = 5;
        w.set_economy({civ}, {}, OreValues{}, 600.0, 0.0, 0.0);
    };
    setup(a);
    const int b = a.add_building(0, x0 + 6, y0 + 2, 2, 2, 0, 400, armor_index("wood"), 0.0, true);
    UnitCombat g = gi();
    g.occupier = true;
    a.add_combat_unit(x0 + 5, y0 + 2, 0, g);
    a.add_combat_unit(x0 + 5, y0 + 3, 0, g);
    REQUIRE(a.campaign_occupy_now(0, b));
    REQUIRE(a.campaign_occupy_now(1, b));
    a.step(kDt);
    REQUIRE(a.buildings()[b].occupants.size() == 2);
    REQUIRE(a.units()[0].cargo_of == kGarrisonCargo);
    World w2(ra2fixture::mapsmd03_map());
    roundtrip(a, setup, 30, &w2);
    CHECK(w2.buildings()[b].occupants.size() == 2);
    CHECK(w2.units()[0].garrison_of == b);
    // Evacuate after the load: both come out the same way.
    a.buildings()[b].hp = 100;
    w2.buildings()[b].hp = 100;
    for (int k = 0; k < 5; ++k) { a.step(kDt); w2.step(kDt); }
    CHECK(w2.buildings()[b].occupants.empty());
    CHECK(save_world(a) == save_world(w2));
}

TEST_CASE("w8save audit: a chrono-erase in progress round-trips (warp_remaining, temporal_target)") {
    SAVE_WORLD(a);
    int x0 = 0, y0 = 0;
    REQUIRE(find_open_rect(a, 16, 8, x0, y0));
    auto setup = [](World& w) {
        base(w);
        AbilityProfile cp;
        cp.fire_kind = AbilityKind::Temporal;
        cp.warpable = true;
        w.register_ability_profile("CLEG", cp);
        AbilityProfile tp;
        tp.warpable = true;
        w.register_ability_profile("HTNK", tp);
    };
    setup(a);
    a.add_combat_unit(x0, y0, 0, mk("CLEG", 125, "none", 8, 120, 5.0, kAll100));
    a.add_combat_unit(x0 + 3, y0, 1, mk("HTNK", 100, "heavy", 0, 60, 0.0, {}, true));
    a.units()[0].target = 1;
    a.units()[0].attack_order = true;
    for (int k = 0; k < 20; ++k) a.step(kDt);
    REQUIRE(a.units()[1].warp_remaining > 0);
    REQUIRE(a.units()[0].temporal_target == 1);
    World b(ra2fixture::mapsmd03_map());
    roundtrip(a, setup, 400, &b);
    CHECK_FALSE(b.units()[1].alive);   // erased after the load, as in the saved run
}

TEST_CASE("w8save audit: a riding parasite round-trips (cargo_of = host, parasite_on_me)") {
    SAVE_WORLD(a);
    int x0 = 0, y0 = 0;
    REQUIRE(find_open_rect(a, 16, 8, x0, y0));
    auto setup = [](World& w) {
        base(w);
        AbilityProfile dp;
        dp.fire_kind = AbilityKind::Parasite;
        dp.parasiteable = false;
        dp.suppression_threshold = 5;
        w.register_ability_profile("DRON", dp);
    };
    setup(a);
    const std::vector<int> para = {100, 100, 100, 100, 100, 100, 0, 0, 0, 0, 0};
    a.add_combat_unit(x0, y0, 1, mk("DRON", 100, "special_1", 50, 60, 1.83, para));
    a.add_combat_unit(x0 + 1, y0, 0, mk("HTNK", 400, "heavy", 0, 60, 0.0, {}, true));
    a.units()[0].target = 1;
    a.units()[0].attack_order = true;
    for (int k = 0; k < 120 && a.units()[1].parasite_on_me != 0; ++k) a.step(kDt);
    REQUIRE(a.units()[0].cargo_of == 1);
    REQUIRE(a.units()[1].parasite_on_me == 0);
    World b(ra2fixture::mapsmd03_map());
    roundtrip(a, setup, 200, &b);
    CHECK(b.units()[1].hp < 400);
}

namespace {
UnitCombat owner(const std::string& spawns, int number, int regen, int reload) {
    UnitCombat c = mk("W7OWN", 800, "heavy", 0, 60, 25.0, kAll100, true);
    c.an.spawner_slot = 0;
    c.an.spawns = spawns;
    c.an.spawns_number = number;
    c.an.spawn_regen_frames = regen;
    c.an.spawn_reload_frames = reload;
    c.an.spawner_range = 25.0;
    return c;
}
UnitCombat target_dummy() { return mk("W7TGT", 5000, "heavy", 0, 1000, 0.0, std::vector<int>(11, 0)); }
}  // namespace

TEST_CASE("w8save audit: spawner nodes with their spawns out round-trip (sp_unit, owner)") {
    SAVE_WORLD(a);
    const ra2assets::MapCell* c = pick_start_cell(a.map());
    REQUIRE(c != nullptr);
    auto setup = [](World& w) {
        base(w);
        UnitCombat hornet;
        hornet.strength = 75;
        hornet.image = "W7HRN";
        hornet.is_aircraft = true;
        hornet.is_vehicle = true;
        hornet.range = 1.0;
        hornet.verses = std::vector<int>(11, 0);
        hornet.an.fixed_wing = true;
        hornet.an.spawned = true;
        hornet.an.ammo = 1;
        hornet.an.fly_speed_cps = 0.5;
        w.register_an_type("W7HRN", hornet);
    };
    setup(a);
    a.add_combat_unit(c->x, c->y, 0, owner("W7HRN", 3, 600, 150));
    a.add_combat_unit(c->x + 6, c->y, 1, target_dummy());
    for (int f = 0; f < 50; ++f) {
        a.units()[0].an.spawn_fire_unit = 1;
        a.step(kDt);
    }
    int out = 0;
    for (const Unit& u : a.units()) out += u.an.t.spawned && u.alive;
    REQUIRE(out >= 2);
    World b(ra2fixture::mapsmd03_map());
    roundtrip(a, setup, 0, &b);
    for (int f = 0; f < 200; ++f) {
        a.units()[0].an.spawn_fire_unit = 1;
        b.units()[0].an.spawn_fire_unit = 1;
        a.step(kDt);
        b.step(kDt);
    }
    CHECK(save_world(a) == save_world(b));
}

TEST_CASE("w8save audit: a rocket in flight round-trips (RocketLocomotion, launch hold)") {
    SAVE_WORLD(a);
    const ra2assets::MapCell* c = pick_start_cell(a.map());
    REQUIRE(c != nullptr);
    auto setup = [](World& w) {
        base(w);
        SwParams sw;
        sw.rockets.block[1].pause_frames = 20;
        sw.rockets.block[1].tilt_frames = 60;
        w.set_sw_params(sw);
        w.register_an_type("PDPLANE", pdplane());
        w.register_an_type("E1", gi());
        UnitCombat m;
        m.strength = 50;
        m.image = "W7MSL";
        m.is_aircraft = true;
        m.is_vehicle = true;
        m.verses = std::vector<int>(11, 0);
        m.an.spawned = true;
        m.an.missile = true;
        m.an.rocket_kind = 1;
        m.an.rocket_speed = 1;
        w.register_an_type("W7MSL", m);
    };
    setup(a);
    a.add_combat_unit(c->x, c->y, 0, owner("W7MSL", 2, 80, 0));
    a.add_combat_unit(c->x + 12, c->y, 1, target_dummy());
    bool flying = false;
    for (int f = 0; f < 200 && !flying; ++f) {
        if (f < 5) a.units()[0].an.spawn_fire_unit = 1;
        a.step(kDt);
        for (const Unit& u : a.units())
            if (u.an.t.missile && u.alive && u.an.rocket.phase > 0) flying = true;
    }
    REQUIRE(flying);
    roundtrip(a, setup, 300);
}

TEST_CASE("w8save audit: units on a high-bridge deck and under it round-trip (on_deck)") {
    SAVE_WORLD(a);
    auto setup = [](World& w) {
        base(w);
        const int W = w.grid_w();
        const std::size_t n = static_cast<std::size_t>(W) * w.grid_h();
        std::vector<std::uint8_t> hb(n, 0);
        hb[20 * W + 9] = 2;
        for (int x = 10; x <= 14; ++x) hb[20 * W + x] = 1;
        hb[20 * W + 15] = 2;
        w.set_high_bridges(hb);
        World::LandSpeedRules r;
        w.arm_land_speed(std::vector<std::uint8_t>(n, 0), std::vector<std::uint8_t>(n, 0), r);
    };
    setup(a);
    Unit& u = a.add_unit(12, 20, "GI");
    a.sync_deck_layer(u);
    REQUIRE(a.units()[0].on_deck == 1);
    a.order_unit_move(0, 15, 20);
    a.step(kDt);
    World b(ra2fixture::mapsmd03_map());
    roundtrip(a, setup, 60, &b);
    CHECK(b.units()[0].on_deck == a.units()[0].on_deck);
}

TEST_CASE("w8save audit: a transport inside a transport round-trips and unloads identically") {
    SAVE_WORLD(a);
    int x0 = 0, y0 = 0;
    REQUIRE(find_open_rect(a, 16, 8, x0, y0));
    base(a);
    UnitCombat lcrf = mk("LCRF", 600, "heavy", 0, 60, 0.0, {}, true);
    lcrf.passengers = 3;
    lcrf.size_limit = 3;
    UnitCombat ifv = mk("FV", 200, "light", 0, 60, 0.0, {}, true);
    ifv.passengers = 1;
    ifv.size_limit = 2;
    ifv.size = 3;
    a.add_combat_unit(x0 + 4, y0 + 4, 0, lcrf);
    const int inner = a.add_unit_in_transport(0, 0, ifv);
    REQUIRE(inner == 1);
    const int rider = a.add_unit_in_transport(inner, 0, gi());
    REQUIRE(rider == 2);
    REQUIRE(a.units()[inner].cargo_of == 0);
    REQUIRE(a.units()[rider].cargo_of == inner);
    a.step(kDt);
    World b(ra2fixture::mapsmd03_map());
    roundtrip(a, base, 10, &b);
    CHECK(b.units()[rider].cargo_of == inner);
    a.order_unload(0);
    b.order_unload(0);
    for (int k = 0; k < 60; ++k) { a.step(kDt); b.step(kDt); }
    CHECK(save_world(a) == save_world(b));
    CHECK(b.units()[inner].cargo_of == -1);   // the IFV is out ...
    CHECK(b.units()[rider].cargo_of == inner);   // ... with its rider still aboard
}
