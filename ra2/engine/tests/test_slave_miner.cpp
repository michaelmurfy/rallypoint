#include "doctest.h"
#include "ra2assets/rules.hpp"
#include "../engine/slave_miner.hpp"
using namespace ra2engine;
using ra2assets::Rules;

namespace {

struct Rig {
    std::vector<Unit> units;
    std::vector<Building> buildings;
    std::vector<BuildingTypeDef> types;
    OreField ore{24, 24};
    std::vector<std::uint8_t> pass = std::vector<std::uint8_t>(24 * 24, 1);
    Occupancy occ{24, 24};
    OreValues values{25, 50};
    double credits[2] = {0, 0};
    std::vector<SlaveSpawn> spawns;
    std::vector<SlaveMove> moves;
    int scan = 14;

    Rig() {
        BuildingTypeDef d;
        d.id = "YAREFN";
        d.fw = 3;
        d.fh = 3;
        d.enslaves = "SLAV";
        d.slaves_number = 5;
        d.slave_regen_rate = 500;
        d.slave_reload_rate = 25;
        d.slave_storage = 4;
        types.push_back(d);
        Building b;
        b.type = 0;
        b.cx = 10;
        b.cy = 10;
        b.fw = 3;
        b.fh = 3;
        b.house = 0;
        b.state = Building::State::Idle;
        b.alive = true;
        buildings.push_back(b);
    }

    void apply_spawns() {
        for (const SlaveSpawn& s : spawns) {
            Unit u;
            u.cell_x = s.gx;
            u.cell_y = s.gy;
            u.fx = s.gx;
            u.fy = s.gy;
            u.house = s.house;
            u.alive = true;
            make_slave(u, s.master, s.storage_bales, s.master_is_unit);
            occ.set(s.gx, s.gy, (int)units.size());
            units.push_back(u);
        }
        spawns.clear();
    }

    void apply_moves() {
        for (const SlaveMove& m : moves) {
            if (m.unit < 0 || m.unit >= (int)units.size()) continue;
            Unit& u = units[(size_t)m.unit];
            occ.release_all_of(m.unit, -1, -1);
            u.cell_x = m.gx;
            u.cell_y = m.gy;
            u.fx = m.gx;
            u.fy = m.gy;
            u.path.clear();
            u.path_idx = 0;
            occ.set(m.gx, m.gy, m.unit);
        }
        moves.clear();
    }

    std::vector<SlaveScoot> scoots;
    SlaveMinerScootRules scoot_rules{};
    std::uint8_t ai_mask[2] = {0, 0};

    void step(double dt) {
        spawns.clear();
        moves.clear();
        scoots.clear();
        step_slave_miners(units, buildings, types, ore, pass, 24, 24, occ, dt, values, credits, 2,
                          scan, &scoot_rules, ai_mask, spawns, moves, scoots);
        apply_spawns();
        apply_moves();
    }

    int live_slaves() const {
        int n = 0;
        for (const Unit& u : units)
            if (u.alive && u.die_time < 0.0 && u.is_slave) ++n;
        return n;
    }
};

}  // namespace

TEST_CASE("rules: slave miner accessors (synthetic INI)") {
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[General]\nSlaveMinerSlaveScan=14\nSlaveMinerShortScan=8\nSlaveMinerLongScan=48\n"
        "SlaveMinerScanCorrection=3\nSlaveMinerKickFrameDelay=150\n"
        "AISlaveMinerNumber=4,3,2\n"
        "[SLAV]\nSlaved=yes\nStorage=4\n"
        "[YAREFN]\nEnslaves=SLAV\nSlavesNumber=5\nSlaveRegenRate=500\nSlaveReloadRate=25\n"
        "ResourceGatherer=yes\nResourceDestination=yes\nStorage=200\n"
        "[SMIN]\nEnslaves=SLAV\nSlavesNumber=5\nResourceGatherer=yes\n");
    auto r = ra2assets::Rules::load(ini);
    CHECK(r.enslaves("YAREFN") == "SLAV");
    CHECK(r.slaves_number("YAREFN") == 5);
    CHECK(r.slave_regen_rate("YAREFN") == 500);
    CHECK(r.slave_reload_rate("YAREFN") == 25);
    CHECK(r.slaved("SLAV"));
    CHECK(r.resource_gatherer("YAREFN"));
    CHECK(r.resource_destination("YAREFN"));
    CHECK(r.storage("SLAV") == 4);
    CHECK(r.slave_miner_slave_scan() == 14);
    CHECK(r.slave_miner_short_scan() == 8);
    CHECK(r.slave_miner_long_scan() == 48);
    CHECK(r.slave_miner_scan_correction() == 3);
    CHECK(r.slave_miner_kick_frame_delay() == 150);
    CHECK(r.ai_slave_miner_number(0) == 4);
    CHECK(r.ai_slave_miner_number(1) == 3);
    CHECK(r.ai_slave_miner_number(2) == 2);
    CHECK(r.enslaves("SMIN") == "SLAV");
}

TEST_CASE("slave miner: pick_scoot short-miss and ScanCorrection") {
    int tx = 0, ty = 0;
    // No short ore: any long hit is a scoot target (KickFrameDelay gates callers).
    OreField ore{32, 32};
    ore.set(10, 25, OreKind::Ore, kMaxOreDensity, 0);
    CHECK(slave_miner_pick_scoot(ore, 10, 10, /*short*/ 8, /*long*/ 48, /*corr*/ 3, tx, ty));
    CHECK(tx == 10);
    CHECK(ty == 25);
    // Ore already inside short: nearest search makes long == short → stay.
    OreField local{32, 32};
    local.set(10, 12, OreKind::Ore, kMaxOreDensity, 0);
    CHECK_FALSE(slave_miner_pick_scoot(local, 10, 10, 8, 48, 3, tx, ty));
    // No ore anywhere.
    OreField empty{32, 32};
    CHECK_FALSE(slave_miner_pick_scoot(empty, 10, 10, 8, 48, 3, tx, ty));
}

TEST_CASE("slave miner: AI scoot emits SlaveScoot after KickFrameDelay") {
    Rig r;
    r.ai_mask[0] = 1;
    r.scoot_rules.short_scan = 4;
    r.scoot_rules.long_scan = 40;
    r.scoot_rules.correction = 2;
    r.scoot_rules.kick_delay = 5;
    // Building centre (11,11); ore outside short (Chebyshev dist 8).
    r.ore.set(11, 19, OreKind::Ore, kMaxOreDensity, 0);
    bool got = false;
    for (int f = 0; f < 20 && !got; ++f) {
        r.step(1.0 / 15.0);
        if (!r.scoots.empty()) got = true;
    }
    CHECK(got);
    REQUIRE(!r.scoots.empty());
    CHECK_FALSE(r.scoots[0].master_is_unit);
    CHECK(r.scoots[0].gx == 11);
    CHECK(r.scoots[0].gy == 19);
}

TEST_CASE("slave miner: human house does not scoot") {
    Rig r;
    r.ai_mask[0] = 0;
    r.scoot_rules.kick_delay = 1;
    r.scoot_rules.short_scan = 4;
    r.ore.set(11, 19, OreKind::Ore, kMaxOreDensity, 0);
    for (int f = 0; f < 20; ++f) r.step(1.0 / 15.0);
    CHECK(r.scoots.empty());
}

TEST_CASE("slave miner: unit-master AI scoot toward long-scan ore") {
    Rig r;
    r.buildings.clear();
    Unit smin;
    smin.cell_x = 10;
    smin.cell_y = 10;
    smin.fx = 10;
    smin.fy = 10;
    smin.house = 0;
    smin.alive = true;
    arm_slave_master(smin, 1, 500, 25, 4);
    r.units.push_back(smin);
    r.occ.set(10, 10, 0);
    r.ai_mask[0] = 1;
    r.scoot_rules.short_scan = 4;
    r.scoot_rules.long_scan = 40;
    r.scoot_rules.kick_delay = 3;
    r.ore.set(10, 20, OreKind::Ore, kMaxOreDensity, 0);
    bool got = false;
    for (int f = 0; f < 20 && !got; ++f) {
        r.step(1.0 / 15.0);
        for (const SlaveScoot& s : r.scoots)
            if (s.master_is_unit) got = true;
    }
    CHECK(got);
}

TEST_CASE("slave miner: spawn fills SlavesNumber from a deployed YAREFN") {
    Rig r;
    // First tick: one spawn (immediate).
    r.step(1.0 / 15.0);
    CHECK(r.live_slaves() == 1);
    // ReloadRate=25 frames between further spawns.
    for (int i = 0; i < 4; ++i) {
        for (int f = 0; f < 25; ++f) r.step(1.0 / 15.0);
        CHECK(r.live_slaves() == i + 2);
    }
    CHECK(r.live_slaves() == 5);
    // Cap: no more spawns.
    for (int f = 0; f < 50; ++f) r.step(1.0 / 15.0);
    CHECK(r.live_slaves() == 5);
}

TEST_CASE("slave miner: gather ore and deliver into house credits") {
    Rig r;
    // Dense ore next to the pad so the slave can fill Storage=4 quickly.
    for (int y = 8; y <= 9; ++y)
        for (int x = 8; x <= 12; ++x) r.ore.set(x, y, OreKind::Ore, kMaxOreDensity, 0);

    r.step(1.0 / 15.0);  // spawn one slave
    REQUIRE(r.live_slaves() == 1);
    Unit& s = r.units[0];
    REQUIRE(s.is_slave);

    // Drive the gather loop until delivery credits the house.
    bool delivered = false;
    for (int i = 0; i < 500 && !delivered; ++i) {
        r.step(1.0 / 15.0);
        if (r.credits[0] > 0.0) delivered = true;
    }
    CHECK(delivered);
    CHECK(r.credits[0] == doctest::Approx(4.0 * 25.0));  // 4 ore bales
    // After reload the slave seeks again.
    bool seeking = false;
    for (int i = 0; i < 40; ++i) {
        r.step(1.0 / 15.0);
        if (r.units[0].slave == SlaveState::SeekOre || r.units[0].slave == SlaveState::MoveToOre ||
            r.units[0].slave == SlaveState::Harvesting)
            seeking = true;
    }
    CHECK(seeking);
}

TEST_CASE("slave miner: dead slave arms SlaveRegenRate before respawn") {
    Rig r;
    r.types[0].slaves_number = 1;
    r.types[0].slave_regen_rate = 30;  // 30 frames for a fast test
    r.step(1.0 / 15.0);
    REQUIRE(r.live_slaves() == 1);
    r.units[0].alive = false;
    r.units[0].die_time = 0.0;
    r.step(1.0 / 15.0);  // death consumed; regen armed
    CHECK(r.live_slaves() == 0);
    // Not yet: need ~30 frames.
    for (int f = 0; f < 28; ++f) r.step(1.0 / 15.0);
    CHECK(r.live_slaves() == 0);
    for (int f = 0; f < 5; ++f) r.step(1.0 / 15.0);
    CHECK(r.live_slaves() == 1);
}

TEST_CASE("slave miner: master death liberates slaves") {
    Rig r;
    r.types[0].slaves_number = 2;
    r.step(1.0 / 15.0);
    for (int f = 0; f < 25; ++f) r.step(1.0 / 15.0);
    REQUIRE(r.live_slaves() == 2);
    r.buildings[0].alive = false;
    r.buildings[0].state = Building::State::Dead;
    r.step(1.0 / 15.0);
    for (const Unit& u : r.units) {
        if (!u.alive) continue;
        CHECK_FALSE(u.is_slave);
        CHECK(u.slave == SlaveState::None);
    }
}

TEST_CASE("slave miner: undeployed SMIN (unit master) spawns and gathers") {
    // Retail: SlaveManager attaches to VehicleTypes with Enslaves= (SMIN), not
    // BuildingTypes only [RE: FUN_006F3FC0 / FUN_006AF1A0].
    Rig r;
    r.buildings.clear();
    Unit smin;
    smin.cell_x = 10;
    smin.cell_y = 10;
    smin.fx = 10;
    smin.fy = 10;
    smin.house = 0;
    smin.alive = true;
    arm_slave_master(smin, /*slaves_number=*/3, /*regen=*/500, /*reload=*/25, /*storage=*/4);
    r.units.push_back(smin);
    r.occ.set(10, 10, 0);

    r.step(1.0 / 15.0);
    CHECK(r.live_slaves() == 1);
    for (int i = 0; i < 2; ++i) {
        for (int f = 0; f < 25; ++f) r.step(1.0 / 15.0);
        CHECK(r.live_slaves() == i + 2);
    }
    CHECK(r.live_slaves() == 3);
    for (const Unit& u : r.units) {
        if (!u.is_slave) continue;
        CHECK(u.slave_master_is_unit);
        CHECK(u.slave_master == 0);
    }

    for (int y = 8; y <= 9; ++y)
        for (int x = 8; x <= 12; ++x) r.ore.set(x, y, OreKind::Ore, kMaxOreDensity, 0);
    bool delivered = false;
    for (int i = 0; i < 500 && !delivered; ++i) {
        r.step(1.0 / 15.0);
        if (r.credits[0] > 0.0) delivered = true;
    }
    CHECK(delivered);
}

TEST_CASE("slave miner: killer-house liberation on building master death") {
    Rig r;
    r.types[0].slaves_number = 1;
    r.step(1.0 / 15.0);
    REQUIRE(r.live_slaves() == 1);
    REQUIRE(r.units[0].house == 0);

    Unit killer;
    killer.house = 1;
    killer.alive = true;
    r.units.push_back(killer);
    const int killer_idx = (int)r.units.size() - 1;
    r.buildings[0].killed_by = killer_idx;
    r.buildings[0].alive = false;
    r.buildings[0].state = Building::State::Dead;
    r.step(1.0 / 15.0);
    CHECK_FALSE(r.units[0].is_slave);
    CHECK(r.units[0].house == 1);
}

TEST_CASE("slave miner: killer-house liberation on unit master death") {
    Rig r;
    r.buildings.clear();
    Unit smin;
    smin.cell_x = 10;
    smin.cell_y = 10;
    smin.fx = 10;
    smin.fy = 10;
    smin.house = 0;
    smin.alive = true;
    arm_slave_master(smin, 1, 500, 25, 4);
    r.units.push_back(smin);
    r.occ.set(10, 10, 0);
    r.step(1.0 / 15.0);
    REQUIRE(r.live_slaves() == 1);
    const int slave_idx = 1;  // after master at 0
    REQUIRE(r.units[(size_t)slave_idx].is_slave);
    REQUIRE(r.units[(size_t)slave_idx].house == 0);

    Unit killer;
    killer.house = 1;
    killer.alive = true;
    r.units.push_back(killer);
    r.units[0].vet.killed_by = (int)r.units.size() - 1;
    r.units[0].alive = false;
    r.units[0].die_time = 0.0;
    r.step(1.0 / 15.0);
    CHECK_FALSE(r.units[(size_t)slave_idx].is_slave);
    CHECK(r.units[(size_t)slave_idx].house == 1);
}

TEST_CASE("slave miner: SMIN→YAREFN transplant keeps slaves [RE: FUN_006AF580]") {
    // Without transplant, consume_unit on the SMIN would liberate workers next step.
    Rig r;
    r.buildings.clear();
    Unit smin;
    smin.cell_x = 10;
    smin.cell_y = 10;
    smin.fx = 10;
    smin.fy = 10;
    smin.house = 0;
    smin.alive = true;
    arm_slave_master(smin, /*slaves_number=*/2, /*regen=*/500, /*reload=*/25, /*storage=*/4);
    r.units.push_back(smin);
    r.occ.set(10, 10, 0);
    r.step(1.0 / 15.0);
    for (int f = 0; f < 25; ++f) r.step(1.0 / 15.0);
    REQUIRE(r.live_slaves() == 2);

    Building yarefn;
    yarefn.type = 0;
    yarefn.cx = 9;
    yarefn.cy = 9;
    yarefn.fw = 3;
    yarefn.fh = 3;
    yarefn.house = 0;
    yarefn.state = Building::State::Idle;
    yarefn.alive = true;
    r.buildings.push_back(yarefn);
    // Stamp timers after spawn so the transplant copies a known state.
    r.units[0].slave_spawn_frames = 17;
    r.units[0].slave_needs_regen = true;
    r.units[0].slave_guard_frames = 3;
    // Simulate finish_deploy: unit tombstoned, then FUN_006AF580.
    r.units[0].alive = false;
    r.units[0].die_time = -1.0;
    transplant_slave_manager_to_building(r.units, r.buildings[0], /*unit_master=*/0,
                                         /*building_idx=*/0);
    CHECK_FALSE(r.units[0].is_slave_master);
    CHECK(r.buildings[0].slave_spawn_frames == 17);
    CHECK(r.buildings[0].slave_needs_regen);
    CHECK(r.buildings[0].slave_guard_frames == 3);
    for (const Unit& u : r.units) {
        if (!u.is_slave) continue;
        CHECK_FALSE(u.slave_master_is_unit);
        CHECK(u.slave_master == 0);
        CHECK(u.alive);
    }
    // Still enslaved after a step (no liberation).
    r.step(1.0 / 15.0);
    CHECK(r.live_slaves() == 2);
}

TEST_CASE("slave miner: YAREFN→SMIN transplant keeps slaves [RE: FUN_006AF580]") {
    Rig r;
    r.types[0].slaves_number = 2;
    r.types[0].slave_regen_rate = 40;
    r.types[0].slave_reload_rate = 10;
    r.types[0].slave_storage = 4;
    r.step(1.0 / 15.0);
    for (int f = 0; f < 25; ++f) r.step(1.0 / 15.0);
    REQUIRE(r.live_slaves() == 2);

    Unit smin;
    smin.cell_x = 12;
    smin.cell_y = 12;
    smin.fx = 12;
    smin.fy = 12;
    smin.house = 0;
    smin.alive = true;
    r.units.push_back(smin);
    const int smin_idx = (int)r.units.size() - 1;
    r.buildings[0].slave_spawn_frames = 9;
    r.buildings[0].slave_needs_regen = true;
    transplant_slave_manager_to_unit(r.units, r.buildings[0], /*building_idx=*/0, smin_idx,
                                     r.types[0].slaves_number, r.types[0].slave_regen_rate,
                                     r.types[0].slave_reload_rate, r.types[0].slave_storage);
    // Building goes dead (packed); slaves must already point at the unit.
    r.buildings[0].alive = false;
    r.buildings[0].state = Building::State::Dead;
    CHECK(r.units[(size_t)smin_idx].is_slave_master);
    CHECK(r.units[(size_t)smin_idx].slaves_number == 2);
    CHECK(r.units[(size_t)smin_idx].slave_spawn_frames == 9);
    CHECK(r.units[(size_t)smin_idx].slave_needs_regen);
    for (const Unit& u : r.units) {
        if (!u.is_slave) continue;
        CHECK(u.slave_master_is_unit);
        CHECK(u.slave_master == smin_idx);
    }
    r.step(1.0 / 15.0);
    CHECK(r.live_slaves() == 2);
}
