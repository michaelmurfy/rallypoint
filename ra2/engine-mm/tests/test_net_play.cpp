// NET-PLAY: two real Worlds in one process play a lockstep match over the
// original-format transport (LoopbackHub with latency / loss / duplication):
// each side drives ONE human house with scripted commands (produce, place,
// train, attack, move) that travel as EventClass records through
// LockstepSession; both sides must compute the identical full-state hash
// (fnv1a over save_world) on every frame. Plus: a deliberate divergence on
// one side is caught as out-of-sync; an EXIT removes the player on the other
// side at a common frame.
#include "doctest.h"
#include "net_frame_crc.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "asset_fixture.hpp"
#include "../engine/command.hpp"
#include "../engine/harvest.hpp"
#include "../engine/net/net_game.hpp"
#include "../engine/savegame.hpp"
#include "../engine/world.hpp"

using ra2engine::Command;
using ra2engine::EventType;
using ra2engine::ProdResult;
using ra2engine::QueueCat;
using ra2engine::Rtti;
using ra2::net::NetGame;

namespace {

std::vector<ra2engine::BuildingTypeDef> np_roster() {
    std::vector<ra2engine::BuildingTypeDef> t(3);
    t[0].id = "GACNST"; t[0].conyard = true; t[0].cost = 3000; t[0].tech_level = 1;
    t[0].strength = 1000; t[0].adjacent = 3; t[0].fw = 1; t[0].fh = 1;
    t[1].id = "GAPOWR"; t[1].cost = 600; t[1].tech_level = 1; t[1].strength = 750;
    t[1].power = 200; t[1].adjacent = 3; t[1].fw = 1; t[1].fh = 1;
    t[2].id = "GAPILE"; t[2].cost = 500; t[2].tech_level = 1; t[2].strength = 500;
    t[2].power = -10; t[2].adjacent = 3; t[2].fw = 1; t[2].fh = 1;
    return t;
}
ra2engine::PlayerEconRules np_rules() {
    ra2engine::UnitTypeDef d;
    d.id = "E1";
    d.cost = 200;
    d.tech_level = 1;
    d.prerequisite = {"GAPILE"};
    d.combat.strength = 125;
    d.combat.armor = "none";
    d.combat.image = "GI";
    d.combat.damage = 15;
    d.combat.rof = 20;
    d.combat.range = 4.0;
    d.combat.verses = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};
    ra2engine::PlayerEconRules r;
    r.units = {d};
    r.factory_types[static_cast<int>(QueueCat::Structure)] = {0};
    r.factory_types[static_cast<int>(QueueCat::Defense)] = {0};
    r.factory_types[static_cast<int>(QueueCat::Infantry)] = {2};
    r.build_speed = 0.1;
    r.wall_rules = false;
    return r;
}
constexpr double kDt = 1.0 / 15.0;

bool np_open_cell(const ra2engine::World& w, int x0, int y0, int minr, int& ox, int& oy) {
    for (int r = minr; r < 60; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                const int x = x0 + dx, y = y0 + dy;
                bool ok = true;
                for (int yy = y - 3; yy <= y + 3 && ok; ++yy)
                    for (int xx = x - 3; xx <= x + 3 && ok; ++xx)
                        ok = w.walkable(xx, yy) && w.building_at_cell(xx, yy) < 0;
                if (ok) { ox = x; oy = y; return true; }
            }
    return false;
}

// The scene-side half of an apply (production / placement through the
// World's per-house economy, like GameScene's SceneCmdHost).
struct EconHost : ra2engine::CommandHost {
    ra2engine::World* w;
    explicit EconHost(ra2engine::World* ww) : w(ww) {}
    static QueueCat cat(int rtti) {
        return rtti == static_cast<int>(Rtti::BuildingType) ? QueueCat::Structure : QueueCat::Infantry;
    }
    void produce(int h, int rtti, int type, bool) override { w->econ_produce(h, cat(rtti), type); }
    void suspend(int h, int rtti, int, bool) override { w->econ_suspend(h, cat(rtti)); }
    void abandon(int h, int rtti, int type, bool) override { w->econ_abandon(h, cat(rtti), type, false); }
    void place(int h, int, int, int cx, int cy) override { w->econ_place(h, QueueCat::Structure, cx, cy); }
};

bool g_frame_crc = false;   // W9-01-04: feed net_frame_crc instead of the full sync hash
std::uint32_t world_hash32(const ra2engine::World& w);
std::uint32_t sync_hash32(const ra2engine::World& w) {
    return g_frame_crc ? ra2engine::net_frame_crc(w) : world_hash32(w);
}
std::uint32_t world_hash32(const ra2engine::World& w) {   // the debug full hash (W9-05 policy)
    const std::uint64_t h = ra2engine::save::world_sync_hash(w);
    return static_cast<std::uint32_t>(h ^ (h >> 32));
}

// One peer: a World, its NetGame and the script for its own house.
struct Peer {
    int house = 0;
    std::unique_ptr<ra2engine::World> w;
    std::unique_ptr<EconHost> host;
    std::unique_ptr<ra2::net::NetManager> nm;
    std::unique_ptr<NetGame> game;
    int base_x = 0, base_y = 0, enemy_x = 0, enemy_y = 0;
    int stage = 0;
    int gis_ordered = 0;
    std::vector<std::uint32_t> trail;   // the hash fed for every frame
    int stalls = 0;
    int cmds_applied = 0;
    bool attacked = false;
    ra2::net::NetGame::Step last = ra2::net::NetGame::Step::Run;

    bool place_cell(int type, int& ox, int& oy) const {
        for (int r = 1; r <= 4; ++r)
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx)
                    if (w->econ_placement(house, type, base_x + dx, base_y + dy).ok) {
                        ox = base_x + dx;
                        oy = base_y + dy;
                        return true;
                    }
        return false;
    }
    // The player's actions for this frame (only from the shared sim state, so
    // the script itself is deterministic; the orders go out through the net).
    void script() {
        const ra2engine::HouseEcon* e = w->econ(house);
        if (!e) return;
        const uint32_t f = game->frame();
        auto produce = [&](Rtti r, int type) {
            game->issue(ra2engine::make_type_event(EventType::Produce, r, type, false));
        };
        switch (stage) {
            case 0:
                if (f >= 5) { produce(Rtti::BuildingType, 1); stage = 1; }
                break;
            case 1:
            case 3:
                if (e->queues.ready(QueueCat::Structure)) {
                    const int type = stage == 1 ? 1 : 2;
                    int cx = 0, cy = 0;
                    if (place_cell(type, cx, cy)) {
                        Command c = ra2engine::make_type_event(EventType::Place, Rtti::BuildingType, type, false);
                        c.cx = static_cast<std::int16_t>(cx);
                        c.cy = static_cast<std::int16_t>(cy);
                        game->issue(c);
                        stage = stage == 1 ? 2 : 4;
                    }
                }
                break;
            case 2:
                if (e->queues.producing(QueueCat::Structure) == -1) { produce(Rtti::BuildingType, 2); stage = 3; }
                break;
            case 4:
                if (e->queues.producing(QueueCat::Structure) == -1 && f % 30 == 0) {
                    produce(Rtti::InfantryType, 0);
                    if (++gis_ordered == 4) stage = 5;
                }
                break;
            case 5: {
                std::vector<int> mine;
                for (int i = 0; i < static_cast<int>(w->units().size()); ++i)
                    if (w->units()[static_cast<std::size_t>(i)].alive && w->units()[static_cast<std::size_t>(i)].house == house)
                        mine.push_back(i);
                if (mine.size() >= 2 && f % 45 == 0) {
                    // First a group move toward the enemy, then attack its yard.
                    if (!attacked) {
                        game->issue(ra2engine::make_mega(*w, mine, ra2engine::Mission::Move, ra2engine::Target{},
                                                         ra2engine::Target::cell((base_x + enemy_x) / 2,
                                                                                 (base_y + enemy_y) / 2)));
                        attacked = true;
                    } else {
                        int yard = -1;
                        for (int b = 0; b < static_cast<int>(w->buildings().size()); ++b)
                            if (w->buildings()[static_cast<std::size_t>(b)].alive &&
                                w->buildings()[static_cast<std::size_t>(b)].house != house)
                                yard = b;
                        if (yard >= 0)
                            game->issue(ra2engine::make_mega(*w, mine, ra2engine::Mission::Attack,
                                                             ra2engine::Target::building(yard), ra2engine::Target{}));
                        stage = 6;
                    }
                }
                break;
            }
            default: break;
        }
    }
    // One driver pass: at most one sim tick when the session allows it.
    void pump(uint32_t max_frames, int diverge_at = -1) {
        if (game->frame() >= max_frames) {
            game->net().service();
            return;
        }
        script();
        std::vector<Command> cmds;
        last = game->step([this]() { return sync_hash32(*w); }, cmds);
        if (last == NetGame::Step::Stall) { ++stalls; return; }
        if (last != NetGame::Step::Run) return;
        const uint32_t f = game->frame() - 1;   // the frame step() just released
        trail.push_back(game->crc_of(f));
        for (const Command& c : cmds) {
            w->set_order_issuer(c.house);
            ra2engine::apply_command(*w, c, host.get());
            ++cmds_applied;
        }
        w->set_order_issuer(house);
        if (diverge_at >= 0 && static_cast<int>(f) == diverge_at) w->grant_credits_for_test(house, 1.0);   // the bug
        w->step(kDt);
    }
};

struct Match {
    std::uint32_t now = 1000;
    ra2::net::LoopbackHub hub{[this]() { return now; }, 7};
    Peer p[2];
    ra2assets::MapFile map;

    explicit Match(int hash_every = 1, int crc_from = -1) {
        map = ra2fixture::mapsmd03_map();
        hub.set_latency(5, 60);
        hub.set_loss(0.05);
        hub.set_duplicate(0.03);
        const ra2::net::PeerAddr addr[2] = {ra2::net::PeerAddr{ra2::net::NetAddr{0x0A000001u, 1234}, 0},
                                            ra2::net::PeerAddr{ra2::net::NetAddr{0x0A000002u, 1234}, 0}};
        int cx[2] = {0, 0}, cy[2] = {0, 0};
        for (int k = 0; k < 2; ++k) {
            Peer& q = p[k];
            q.house = k;
            q.w = std::make_unique<ra2engine::World>(map);
            q.w->set_economy(np_roster(), {}, ra2engine::OreValues{}, 600.0, 0.0, 0.0);
            if (k == 0) {
                const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
                REQUIRE(c != nullptr);
                REQUIRE(np_open_cell(*q.w, c->x, c->y, 0, cx[0], cy[0]));
                REQUIRE(np_open_cell(*q.w, cx[0], cy[0], 12, cx[1], cy[1]));
            }
            for (int h = 0; h < 2; ++h) {
                q.w->add_building(0, cx[h], cy[h], 1, 1, h, 1000, 0, 0.0, /*instant=*/true);
                q.w->enable_player_economy(h, 5000.0);
                q.w->set_player_econ_rules(h, np_rules());
            }
            q.w->enable_victory(true, true);
            q.base_x = cx[k];
            q.base_y = cy[k];
            q.enemy_x = cx[1 - k];
            q.enemy_y = cy[1 - k];
            q.host = std::make_unique<EconHost>(q.w.get());
            q.nm = std::make_unique<ra2::net::NetManager>(*hub.create(addr[k]), [this]() { return now; });
            ra2::net::NetGameConfig cfg;
            cfg.local_house = k;
            cfg.human_houses = {0, 1};
            cfg.remotes = {{1 - k, addr[1 - k]}};
            cfg.master = k == 0;
            cfg.master_house = 0;   // W9-05-12
            cfg.hash_every = hash_every;
            cfg.crc_check_from_frame = crc_from;
            q.game = std::make_unique<NetGame>(*q.nm, cfg);
        }
    }
    // Wall clock advances 4 ms per pass; every pass pumps both peers.
    void run(uint32_t frames, int diverge_side = -1, int diverge_at = -1, int max_passes = 400000) {
        for (int pass = 0; pass < max_passes; ++pass) {
            now += 4;
            for (int k = 0; k < 2; ++k) p[k].pump(frames, k == diverge_side ? diverge_at : -1);
            const bool done = p[0].game->frame() >= frames && p[1].game->frame() >= frames;
            const bool failed = (p[0].last != NetGame::Step::Run && p[0].last != NetGame::Step::Stall) ||
                                (p[1].last != NetGame::Step::Run && p[1].last != NetGame::Step::Stall);
            if (done || failed) break;
        }
    }
};

}  // namespace

TEST_CASE("net play: two Worlds in lockstep -- 2000 frames, identical full-state hash every frame") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    Match m;
    m.run(2000);
    for (int k = 0; k < 2; ++k) {
        INFO("peer " << k << " last step " << NetGame::step_name(m.p[k].last));
        REQUIRE(m.p[k].game->frame() == 2000u);
        REQUIRE(m.p[k].trail.size() == 2000u);
    }
    int first_diff = -1;
    for (std::size_t i = 0; i < 2000 && first_diff < 0; ++i)
        if (m.p[0].trail[i] != m.p[1].trail[i]) first_diff = static_cast<int>(i);
    CHECK(first_diff == -1);
    CHECK(world_hash32(*m.p[0].w) == world_hash32(*m.p[1].w));
    // The scripted orders really ran on both sides: each house built a power
    // plant and a barracks, trained soldiers and sent them at the other.
    for (int k = 0; k < 2; ++k) {
        const ra2engine::World& w = *m.p[k].w;
        int blds[2] = {0, 0}, units[2] = {0, 0};
        for (const ra2engine::Building& b : w.buildings())
            if (b.house >= 0 && b.house < 2) ++blds[b.house];
        for (const ra2engine::Unit& u : w.units())
            if (u.house >= 0 && u.house < 2) ++units[u.house];
        CHECK(blds[0] >= 3);
        CHECK(blds[1] >= 3);
        CHECK(units[0] >= 2);
        CHECK(units[1] >= 2);
        CHECK(m.p[k].cmds_applied >= 10);
        CHECK(m.p[k].stage == 6);
    }
    MESSAGE("stalls host=" << m.p[0].stalls << " join=" << m.p[1].stalls << " max_ahead=" << m.p[0].game->session().max_ahead()
                           << " fsr=" << m.p[0].game->session().frame_send_rate() << " hub delivered="
                           << m.hub.delivered() << " dropped=" << m.hub.dropped());
}

TEST_CASE("net play: a divergence on one side is detected as out-of-sync") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    // The original's CRC grace (no compare before start + 256 frames) stays on.
    Match m;
    m.run(1500, /*diverge_side=*/1, /*diverge_at=*/400);
    const bool oos0 = m.p[0].last == NetGame::Step::OutOfSync;
    const bool oos1 = m.p[1].last == NetGame::Step::OutOfSync;
    CHECK((oos0 || oos1));
    const ra2::net::OutOfSyncInfo& o = (oos0 ? m.p[0] : m.p[1]).game->session().out_of_sync();
    CHECK(o.frame >= 401u);
    CHECK(o.frame < 500u);
    CHECK(o.local_crc != o.remote_crc);
    MESSAGE("out of sync: house " << o.house << " frame " << o.frame << " local " << o.local_crc << " remote "
                                  << o.remote_crc << " (detected at frame " << (oos0 ? m.p[0] : m.p[1]).game->frame() << ")");
    // Identical up to (and including) the frame the bug hit, different after.
    REQUIRE(m.p[0].trail.size() > 402u);
    REQUIRE(m.p[1].trail.size() > 402u);
    for (std::size_t i = 0; i <= 400; ++i) REQUIRE(m.p[0].trail[i] == m.p[1].trail[i]);
    CHECK(m.p[0].trail[401] != m.p[1].trail[401]);
}

TEST_CASE("net play: EXIT removes the leaving player on the other side at a common frame") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    Match m;
    m.run(300);
    REQUIRE(m.p[1].game->frame() == 300u);
    // The leaver queues EXIT and keeps running until its own EXIT executes
    // (the original's main loop ends the game there), then flushes.
    m.p[1].game->request_exit();
    std::uint32_t exit_at[2] = {0, 0};
    for (int pass = 0; pass < 200000 && (!exit_at[0] || !exit_at[1]); ++pass) {
        m.now += 4;
        for (int k = 0; k < 2; ++k) {
            if (exit_at[k]) continue;
            std::vector<Command> cmds;
            if (m.p[k].game->step([&]() { return world_hash32(*m.p[k].w); }, cmds) != NetGame::Step::Run) continue;
            for (const Command& c : cmds)
                if (c.type == EventType::Exit && c.house == 1) exit_at[k] = c.frame;
            m.p[k].w->step(kDt);
        }
    }
    REQUIRE(exit_at[0] != 0u);
    CHECK(exit_at[0] == exit_at[1]);
    CHECK(exit_at[0] > 300u);
    bool flushed = false;
    for (int pass = 0; pass < 2000 && !flushed; ++pass) {
        m.now += 16;
        flushed = m.p[1].game->poll_exit();
        m.p[0].game->net().service();
    }
    CHECK(flushed);
    const auto removed = m.p[0].game->take_removed();
    REQUIRE(removed.size() == 1u);
    CHECK(removed[0].first == 1);
    CHECK(removed[0].second == 0);   // left, not lost
    CHECK(m.p[0].game->session().players() == 1);
}

TEST_CASE("net play: chat and away notices travel on the global channel") {
    std::uint32_t now = 1000;
    ra2::net::LoopbackHub hub([&]() { return now; }, 3);
    const ra2::net::PeerAddr a{ra2::net::NetAddr{0x0A000001u, 1}, 0}, b{ra2::net::NetAddr{0x0A000002u, 1}, 0};
    ra2::net::NetManager na(*hub.create(a), [&]() { return now; }), nb(*hub.create(b), [&]() { return now; });
    ra2::net::NetGameConfig ca, cb;
    ca.local_house = 0; ca.human_houses = {0, 1}; ca.remotes = {{1, b}}; ca.master = true;
    cb.local_house = 1; cb.human_houses = {0, 1}; cb.remotes = {{0, a}};
    NetGame ga(na, ca), gb(nb, cb);
    ga.send_chat("Hosty", "gg h\xC3\xA9", 3);
    gb.send_away(false, 90);
    std::vector<ra2::net::NetChat> chat;
    std::vector<ra2::net::AwayNotice> away;
    for (int i = 0; i < 50 && (chat.empty() || away.empty()); ++i) {
        now += 20;
        gb.poll_global(chat, away);
        std::vector<ra2::net::NetChat> c2;
        ga.poll_global(c2, away);
    }
    REQUIRE(chat.size() == 1u);
    CHECK(chat[0].house == 0);
    CHECK(chat[0].sender == "Hosty");
    CHECK(chat[0].text == "gg h\xC3\xA9");
    CHECK(chat[0].color == 3);
    REQUIRE(away.size() >= 1u);
    CHECK(away[0].house == 1);
    CHECK_FALSE(away[0].back);
    CHECK(away[0].seconds == 90u);
}

TEST_CASE("net play: resign_house defeats a leaver and blows up what it owns") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    Match m;
    ra2engine::World& w = *m.p[0].w;
    w.add_combat_unit(m.p[1].base_x + 2, m.p[1].base_y, 1, ra2engine::UnitCombat{});
    w.resign_house(1);
    CHECK(w.house_defeated(1));
    CHECK_FALSE(w.house_defeated(0));
    for (const ra2engine::Building& b : w.buildings())
        if (b.house == 1) CHECK(b.state == ra2engine::Building::State::Dead);
    for (const ra2engine::Unit& u : w.units())
        if (u.house == 1) CHECK(u.die_time >= 0.0);
    for (int f = 0; f < 60; ++f) w.step(kDt);
    CHECK(w.house_outcome(1) == -1);
    CHECK(w.house_outcome(0) == 1);   // the last team standing
    w.resign_house(99);                // out of range: no-op
}

// W9-01-04: the per-frame CRC (net_frame_crc) keeps two real Worlds in sync
// and still catches a one-credit divergence within a frame of the bug.
TEST_CASE("W9-01-04: net play with the per-frame CRC -- in sync, and a divergence is detected") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    g_frame_crc = true;
    {
        Match m;
        m.run(1500, /*diverge_side=*/1, /*diverge_at=*/400);
        const bool oos0 = m.p[0].last == NetGame::Step::OutOfSync;
        const bool oos1 = m.p[1].last == NetGame::Step::OutOfSync;
        CHECK((oos0 || oos1));
        const ra2::net::OutOfSyncInfo& o = (oos0 ? m.p[0] : m.p[1]).game->session().out_of_sync();
        CHECK(o.frame >= 401u);
        CHECK(o.frame < 420u);
    }
    {
        Match m;
        m.run(800);
        CHECK(m.p[0].last == NetGame::Step::Run);
        CHECK(m.p[1].last == NetGame::Step::Run);
        CHECK(world_hash32(*m.p[0].w) == world_hash32(*m.p[1].w));
    }
    g_frame_crc = false;
}

// W9-05-12: REMOVEPLAYER is honoured from the master, from the removed house
// itself, or (the master vanished) against the master -- never a peer
// removing another non-master peer, nor a house that is not a human player.
TEST_CASE("net play: REMOVEPLAYER sender rules (W9-05-12)") {
    std::uint32_t now = 1000;
    ra2::net::LoopbackHub hub{[&now]() { return now; }, 7};
    ra2::net::NetManager nm(*hub.create(ra2::net::PeerAddr{ra2::net::NetAddr{0x0A000001u, 1234}, 0}),
                            [&now]() { return now; });
    ra2::net::NetGameConfig cfg;
    cfg.local_house = 1;
    cfg.human_houses = {0, 1, 2};
    cfg.master_house = 0;
    NetGame g(nm, cfg);
    CHECK(g.removeplayer_allowed(0, 2));    // the host drops a timed-out player
    CHECK(g.removeplayer_allowed(2, 2));    // a player removes itself
    CHECK(g.removeplayer_allowed(1, 0));    // the host vanished: anyone removes it
    CHECK_FALSE(g.removeplayer_allowed(1, 2));   // a peer griefing another peer
    CHECK_FALSE(g.removeplayer_allowed(0, 5));   // not a human house of this match
    CHECK_FALSE(g.removeplayer_allowed(7, 7));   // not a player at all
}

// W9-05-03/06: ALLY (one-way, then mutual) and DESTRUCT travel as EventClass
// records and apply on the same frame on both machines: the full-state hash
// trails stay identical and both Worlds agree on the alliance matrix.
TEST_CASE("net play: ALLY and DESTRUCT in lockstep -- one-way, mutual, resign; identical trails") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    Match m;
    for (int k = 0; k < 2; ++k) {
        ra2engine::World& w = *m.p[k].w;
        for (int h = 0; h < 3; ++h) w.set_house(h, ra2engine::World::HouseRec{ra2engine::World::Side::Allied, h, "", -1, true});
        w.add_building(0, m.p[0].base_x, m.p[0].base_y + 20, 1, 1, 2, 1000, 0, 0.0, /*instant=*/true);   // a third party
    }
    m.run(60);
    REQUIRE(m.p[0].game->frame() == 60u);
    Command al;
    al.type = EventType::Ally;
    al.value = 1;
    m.p[0].game->issue(al);
    m.run(140);
    for (int k = 0; k < 2; ++k) {
        CHECK(m.p[k].w->allied_with(0, 1));
        CHECK_FALSE(m.p[k].w->allied_with(1, 0));   // one way
    }
    al.value = 0;
    m.p[1].game->issue(al);
    m.run(220);
    for (int k = 0; k < 2; ++k) {
        CHECK(m.p[k].w->allied_with(0, 1));
        CHECK(m.p[k].w->allied_with(1, 0));
        CHECK_FALSE(m.p[k].w->allied_with(0, 2));
    }
    Command d;
    d.type = EventType::Destruct;
    m.p[1].game->issue(d);
    m.run(320);
    REQUIRE(m.p[0].trail.size() == m.p[1].trail.size());
    for (std::size_t i = 0; i < m.p[0].trail.size(); ++i) REQUIRE(m.p[0].trail[i] == m.p[1].trail[i]);
    for (int k = 0; k < 2; ++k) {
        CHECK(m.p[k].w->house_defeated(1));
        CHECK_FALSE(m.p[k].w->house_defeated(0));
        CHECK_FALSE(m.p[k].w->game_over());   // 0 and 2 fight on
    }
}
