#include "doctest.h"

#include <cmath>
#include <vector>

#include "asset_fixture.hpp"  // shared load-once real map (mapsmd03.mix)
#include "crate.hpp"
#include "veterancy.hpp"
#include "selection.hpp"
#include "world.hpp"

// P5 Veterancy + Crates World integration [RE: docs/re/veterancy.md]. Cases run
// on the shared real map (gated on the install) because real combat needs
// walkable cells; they skip cleanly without it.

namespace {
using ra2engine::UnitCombat;
using ra2engine::World;

bool vmap(ra2assets::MapFile& out) {
    if (!ra2fixture::mapsmd03_available()) return false;
    out = ra2fixture::mapsmd03_map();
    return true;
}

// E1-like killer: Cost 200, real E1 ability lists + an ElitePrimary.
UnitCombat killer_stats(int damage = 200) {
    UnitCombat c;
    c.image = "GI";
    c.strength = 125;
    c.armor = "none";
    c.damage = damage;
    c.rof = 20;
    c.range = 3.0;
    c.verses = std::vector<int>(11, 100);
    c.cost = 200;
    c.rules_speed = 4;
    c.vet_abilities = ra2engine::parse_abilities("STRONGER,FIREPOWER,ROF,SIGHT,FASTER");
    c.elite_abilities = ra2engine::parse_abilities("SELF_HEAL,STRONGER,FIREPOWER,ROF");
    c.has_elite = true;
    c.elite.damage = 250;
    c.elite.rof = 20;
    c.elite.range = 3.0;
    c.elite.verses = std::vector<int>(11, 100);
    return c;
}
// Weaponless target (range 0 -> never fires back), Cost 100.
UnitCombat target_stats(int hp = 10, int cost = 100) {
    UnitCombat c;
    c.image = "CONS";
    c.strength = hp;
    c.armor = "none";
    c.damage = 0;
    c.range = 0.0;
    c.cost = cost;
    c.vet_abilities = ra2engine::parse_abilities("STRONGER,FIREPOWER,ROF,SIGHT,FASTER");
    c.elite_abilities = ra2engine::parse_abilities("SELF_HEAL,STRONGER,FIREPOWER,ROF");
    return c;
}

// First walkable, unoccupied cell in Chebyshev rings around (x0, y0), where
// (x0, y0) is an OFFSET from the map's pick_start_cell (the open interior the
// gates use) -- raw (20,20) is off-map on mapsmd03.
bool free_cell(const World& w, int x0, int y0, int& ox, int& oy) {
    const ra2assets::MapCell* sc = ra2engine::pick_start_cell(w.map());
    if (!sc) return false;
    x0 += sc->x - 20;
    y0 += sc->y - 20;
    for (int r = 0; r < 12; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                if (w.walkable(x0 + dx, y0 + dy) && w.unit_at_cell(x0 + dx, y0 + dy) < 0) {
                    ox = x0 + dx;
                    oy = y0 + dy;
                    return true;
                }
            }
    return false;
}

// Same ring search around an ABSOLUTE cell.
bool free_cell_abs(const World& w, int x0, int y0, int& ox, int& oy) {
    for (int r = 0; r < 12; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                if (w.walkable(x0 + dx, y0 + dy) && w.unit_at_cell(x0 + dx, y0 + dy) < 0) {
                    ox = x0 + dx;
                    oy = y0 + dy;
                    return true;
                }
            }
    return false;
}

// Spawns a target adjacent (Chebyshev 1) to (x,y), returning its index.
int spawn_adjacent(World& w, int x, int y, int house, const UnitCombat& c) {
    const int d[8][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    for (const auto& o : d)
        if (w.walkable(x + o[0], y + o[1]) && w.unit_at_cell(x + o[0], y + o[1]) < 0) {
            w.add_combat_unit(x + o[0], y + o[1], house, c);
            return static_cast<int>(w.units().size()) - 1;
        }
    return -1;
}

void step_until_dead(World& w, int idx, int cap = 200) {
    for (int k = 0; k < cap && w.units()[idx].die_time < 0.0; ++k) w.step(1.0 / 15.0);
    w.step(1.0 / 15.0);   // the credit pass runs right after step_combat
}
}  // namespace

TEST_CASE("P5 world: a legacy World keeps the spawn-baked rank-1 and credits nothing [firewall]") {
    ra2assets::MapFile map;
    if (!vmap(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    int x = 0, y = 0;
    REQUIRE(free_cell(world, 20, 20, x, y));
    UnitCombat vc = killer_stats(40);
    vc.strength = 100;
    vc.veteran_rank = 1;
    world.add_combat_unit(x, y, 0, vc);
    const ra2engine::Unit& v = world.units().back();
    CHECK(v.w_damage == 44);          // legacy round(40 * 1.1)
    CHECK(v.max_hp == 150);           // legacy round(100 * 1.5)
    CHECK(v.vet.cost == 200);         // type data copied (inert)
    CHECK_FALSE(v.vet.recv);          // combat hook NOT armed
    // A kill in a legacy World leaves veterancy untouched.
    const int k = static_cast<int>(world.units().size()) - 1;
    const int t = spawn_adjacent(world, x, y, 1, target_stats());
    REQUIRE(t >= 0);
    step_until_dead(world, t);
    CHECK(world.units()[t].die_time >= 0.0);
    CHECK(world.units()[k].vet.veterancy == 0.0f);
    CHECK(world.units()[t].vet.killed_by == k);   // the inert stamp is written
    CHECK_FALSE(world.veterancy_enabled());
}

TEST_CASE("P5 world: kills credit VeteranRatio experience; promotion refreshes stats [V2/V3/V5/V7]") {
    ra2assets::MapFile map;
    if (!vmap(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.enable_veterancy(ra2engine::VeterancyRules{});
    int x = 0, y = 0;
    REQUIRE(free_cell(world, 20, 20, x, y));
    world.add_combat_unit(x, y, 0, killer_stats(15));
    const int k = static_cast<int>(world.units().size()) - 1;
    CHECK(world.units()[k].vet.recv);
    CHECK(world.units()[k].w_damage == 15);
    CHECK(world.units()[k].w_rof == 20);
    const int t = spawn_adjacent(world, x, y, 1, target_stats(5));
    REQUIRE(t >= 0);
    step_until_dead(world, t);
    REQUIRE(world.units()[t].die_time >= 0.0);
    CHECK(world.units()[k].vet.veterancy == doctest::Approx(1.0 / 6.0).epsilon(1e-6));
    // Push to just below veteran, kill once more -> veteran -> stats refresh.
    world.units()[k].vet.veterancy = 0.9f;
    const int t2 = spawn_adjacent(world, x, y, 1, target_stats(5));
    REQUIRE(t2 >= 0);
    step_until_dead(world, t2);
    const ra2engine::Unit& u = world.units()[k];
    CHECK(ra2engine::vet_rank(u.vet.veterancy) == 1);
    CHECK(u.w_damage == 16);   // ftol(15 * 1.1)
    CHECK(u.w_rof == 12);      // ftol(20 * 0.6)
    const auto ev = world.take_vet_events();
    REQUIRE(ev.size() == 1u);
    CHECK(ev[0].unit == k);
    CHECK(ev[0].old_rank == 0);
    CHECK(ev[0].new_rank == 1);
    // Elite: the ElitePrimary slot (250) x VeteranCombat.
    world.units()[k].vet.veterancy = 2.0f;
    world.step(1.0 / 15.0);
    CHECK(world.units()[k].w_damage == 275);   // ftol(250 * 1.1)
    CHECK(world.units()[k].vet.elite_flash > 0);
}

TEST_CASE("P5 world: allied, untrainable and DontScore kills give nothing [V3]") {
    ra2assets::MapFile map;
    if (!vmap(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.enable_veterancy(ra2engine::VeterancyRules{});
    int x = 0, y = 0;
    REQUIRE(free_cell(world, 20, 20, x, y));
    UnitCombat kc = killer_stats(200);
    kc.trainable = false;
    world.add_combat_unit(x, y, 0, kc);
    const int k = static_cast<int>(world.units().size()) - 1;
    const int t = spawn_adjacent(world, x, y, 1, target_stats());
    REQUIRE(t >= 0);
    step_until_dead(world, t);
    REQUIRE(world.units()[t].die_time >= 0.0);
    CHECK(world.units()[k].vet.veterancy == 0.0f);   // Trainable=no

    world.units()[k].vet.trainable = true;
    UnitCombat ds = target_stats();
    ds.dont_score = true;
    const int t2 = spawn_adjacent(world, x, y, 1, ds);
    REQUIRE(t2 >= 0);
    step_until_dead(world, t2);
    CHECK(world.units()[k].vet.veterancy == 0.0f);   // DontScore victim
}

TEST_CASE("P5 world: a STRONGER veteran divides incoming damage by VeteranArmor [V6]") {
    ra2assets::MapFile map;
    if (!vmap(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.enable_veterancy(ra2engine::VeterancyRules{});
    int x = 0, y = 0;
    REQUIRE(free_cell(world, 20, 20, x, y));
    // Victims: a rookie and a veteran, both weaponless; the shooter fires 15.
    UnitCombat vic = target_stats(1000);
    world.add_combat_unit(x, y, 1, vic);
    const int rook = static_cast<int>(world.units().size()) - 1;
    const int sh = spawn_adjacent(world, x, y, 0, killer_stats(15));
    REQUIRE(sh >= 0);
    for (int k = 0; k < 2; ++k) world.step(1.0 / 15.0);
    const int dmg_rookie = 1000 - world.units()[rook].hp;
    CHECK(dmg_rookie == 15);
    world.units()[rook].vet.veterancy = 1.0f;   // now a STRONGER veteran
    world.units()[rook].hp = 1000;
    for (int k = 0; k < 25; ++k) world.step(1.0 / 15.0);   // one more ROF cycle (20 frames)
    CHECK(1000 - world.units()[rook].hp == 10);   // ftol(15 / 1.5)
}

TEST_CASE("P5 world: elite SELF_HEAL gains +1 HP on the global 14-frame phase [V11]") {
    ra2assets::MapFile map;
    if (!vmap(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.enable_veterancy(ra2engine::VeterancyRules{});
    int x = 0, y = 0;
    REQUIRE(free_cell(world, 20, 20, x, y));
    UnitCombat c = target_stats(100);
    world.add_combat_unit(x, y, 0, c);
    const int i = static_cast<int>(world.units().size()) - 1;
    world.units()[i].hp = 50;
    for (int k = 0; k < 28; ++k) world.step(1.0 / 15.0);   // frames 0..27: rookie -> no heal
    CHECK(world.units()[i].hp == 50);
    world.units()[i].vet.veterancy = 2.0f;                  // elite: SELF_HEAL
    // frames 28..55 -> heal on 28 and 42 (frame % 14 == 0) -> +2
    for (int k = 0; k < 28; ++k) world.step(1.0 / 15.0);
    CHECK(world.units()[i].hp == 52);
    CHECK(world.vet_frame() == 56u);
}

TEST_CASE("P5 world: injected NON-default rules reach the kill credit (wiring proof)") {
    ra2assets::MapFile map;
    if (!vmap(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    ra2engine::VeterancyRules r;
    r.ratio = 0.5;      // one kill of a half-cost victim = 1.0
    r.combat = 2.0;
    world.enable_veterancy(r);
    int x = 0, y = 0;
    REQUIRE(free_cell(world, 20, 20, x, y));
    world.add_combat_unit(x, y, 0, killer_stats(15));
    const int k = static_cast<int>(world.units().size()) - 1;
    const int t = spawn_adjacent(world, x, y, 1, target_stats(5));
    REQUIRE(t >= 0);
    step_until_dead(world, t);
    CHECK(world.units()[k].vet.veterancy == 1.0f);   // 100 / (200 * 0.5)
    CHECK(world.units()[k].w_damage == 30);          // ftol(15 * 2.0)
}

TEST_CASE("P5 world: a destroyed building credits its roster Cost [V3]") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!vmap(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.enable_veterancy(ra2engine::VeterancyRules{}, {600});
    int x = 0, y = 0;
    REQUIRE(free_cell(world, 20, 20, x, y));
    const int b = world.add_building(0, x + 1, y, 1, 1, /*house=*/1, /*strength=*/10, 0, 0.0, true);
    CHECK(world.buildings()[b].vet_recv);
    world.add_combat_unit(x, y, 0, killer_stats(50));
    const int k = static_cast<int>(world.units().size()) - 1;
    tsel_.set(k);
    world.group_attack_building(0, tsel_.ids(), b);
    for (int f = 0; f < 60 && world.buildings()[b].state != ra2engine::Building::State::Dead; ++f)
        world.step(1.0 / 15.0);
    world.step(1.0 / 15.0);
    REQUIRE(world.buildings()[b].state == ra2engine::Building::State::Dead);
    CHECK(world.units()[k].vet.veterancy == 1.0f);   // 600 / (200 * 3)
}

TEST_CASE("P5 world: crates need veterancy; typed crates + conversion + expiry [C2-C6]") {
    ra2engine::Selection tsel_;   // NET L1: the selection is UI state
    ra2assets::MapFile map;
    if (!vmap(map)) { MESSAGE("skipped: no install"); return; }
    World off(map);
    World::CrateConfig cfg;
    cfg.rules.powerups = ra2engine::default_powerups();
    cfg.seed = 1234;
    CHECK_FALSE(off.enable_crates(cfg));   // requires veterancy
    CHECK_FALSE(off.crates_enabled());

    World world(map);
    world.enable_veterancy(ra2engine::VeterancyRules{});
    REQUIRE(world.enable_crates(cfg));
    int active = 0;
    for (const auto& c : world.crates()) if (c.x >= 0) ++active;
    CHECK(active == 1);                    // clamp(1 player, 1, 255)

    int x = 0, y = 0;
    REQUIRE(free_cell(world, 30, 30, x, y));
    world.add_combat_unit(x, y, 0, killer_stats(15));
    const int u = static_cast<int>(world.units().size()) - 1;

    // Typed Money crate placed ON the unit's cell's neighbour; walk onto it by
    // placing it at the unit's own cell is impossible (occupied), so place it
    // nearby and teleport-step the unit via a move order.
    auto drop_and_collect = [&](int powerup) -> World::CrateEvent {
        int cx = 0, cy = 0;
        REQUIRE(free_cell_abs(world, x + 2, y, cx, cy));
        REQUIRE(world.place_crate(cx, cy, powerup));
        int px = -1, py = -1;
        for (const auto& c : world.crates()) if (c.x >= 0 && c.powerup == powerup) { px = c.x; py = c.y; }
        REQUIRE(px >= 0);
        tsel_.set(u);
        world.group_move(ra2engine::kAnyHouse, tsel_.ids(), px, py);
        tsel_.set(u, false);
        for (int f = 0; f < 150; ++f) {
            world.step(1.0 / 15.0);
            auto ev = world.take_crate_events();
            if (!ev.empty()) { x = world.units()[u].cell_x; y = world.units()[u].cell_y; return ev[0]; }
        }
        FAIL("crate never collected");
        return {};
    };
    const double c0 = world.house_credits(0);
    const World::CrateEvent m = drop_and_collect((int)ra2engine::Powerup::Money);
    CHECK(m.applied == (int)ra2engine::Powerup::Money);
    CHECK(m.amount >= 2000);
    CHECK(m.amount <= 2900);
    CHECK(world.house_credits(0) - c0 == doctest::Approx((double)m.amount));

    const World::CrateEvent a = drop_and_collect((int)ra2engine::Powerup::Armor);
    CHECK(a.applied == (int)ra2engine::Powerup::Armor);
    CHECK(world.units()[u].vet.armor_mult == doctest::Approx(1.5));
    // A second Armor crate on an already-armored collector converts to Money (§3.3).
    const World::CrateEvent a2 = drop_and_collect((int)ra2engine::Powerup::Armor);
    CHECK(a2.picked == (int)ra2engine::Powerup::Armor);
    CHECK(a2.applied == (int)ra2engine::Powerup::Money);

    const World::CrateEvent v = drop_and_collect((int)ra2engine::Powerup::Veteran);
    CHECK(v.applied == (int)ra2engine::Powerup::Veteran);
    CHECK(world.units()[u].vet.veterancy == 1.0f);

    const World::CrateEvent f = drop_and_collect((int)ra2engine::Powerup::Firepower);
    CHECK(f.applied == (int)ra2engine::Powerup::Firepower);
    world.step(1.0 / 15.0);
    CHECK(world.units()[u].w_damage == 33);   // ftol(ftol(2.0*15) * 1.1)

    // Expiry: the original crate (and every replacement) relocates once its
    // lifetime passes; total active stays constant.
    int before = 0;
    for (const auto& c : world.crates()) if (c.x >= 0) ++before;
    for (int k = 0; k < 5500; ++k) world.step(1.0 / 15.0);
    int after = 0;
    for (const auto& c : world.crates()) if (c.x >= 0) ++after;
    CHECK(after == before);
    for (const auto& c : world.crates())
        if (c.x >= 0) CHECK(world.vet_frame() - c.start < (std::uint64_t)c.lifetime);
}
