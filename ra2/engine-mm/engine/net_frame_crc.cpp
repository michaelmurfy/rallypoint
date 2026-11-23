// engine/net_frame_crc.cpp -- see net_frame_crc.hpp. SDL-free.
#include "net_frame_crc.hpp"

#include "map_trigger.hpp"
#include "world.hpp"

namespace ra2engine {

// World's private sim state is read through this friend (world.hpp), the
// SaveAccess pattern. Read-only.
struct NetCrcAccess {
    static std::uint32_t crc(const World& w, const std::vector<int>* extra, const MapTriggerSystem* triggers) {
        FrameCrc c;
        // --- units [RE: FUN_0064D520 infantry / unit loops]: position, facing
        // (body + turret), plus the clone's stronger terms.
        c.add_i(static_cast<int>(w.units_.size()));
        for (const Unit& u : w.units_) {
            if (!u.alive) {   // a tombstone only counts as one (never erased)
                c.add(0xDEADu);
                continue;
            }
            // The original's term: X/10 + (Y/10) << 16 + facing8.
            c.add(static_cast<std::uint32_t>(u.cell_x) + (static_cast<std::uint32_t>(u.cell_y) << 16) +
                  static_cast<std::uint32_t>(u.facing & 0xFF));
            c.add_d(u.fx);
            c.add_d(u.fy);
            c.add_d(u.body_facing);
            c.add_d(u.turret_facing);
            c.add_d(u.altitude);
            c.add_i(u.house);
            c.add_i(u.hp);
            c.add_i(u.target);
            c.add_i(u.target_building);
            c.add_i(u.attack_order ? 1 : 0);
            c.add_i(static_cast<int>(u.path.size()));
            c.add_i(static_cast<int>(u.path_idx));
            c.add_i(u.cargo_of);
            c.add_i(u.capture_target);
            c.add_i(u.infiltrate_target);
            c.add_i(u.enter_target);
            c.add_i(u.rearm_frames);
            c.add_d(u.die_time);
            c.add_i(static_cast<int>(u.harv));
            c.add_d(u.bales_ore);
            c.add_d(u.bales_gems);
        }
        // --- buildings [RE: FUN_0064D520 building loop]
        c.add_i(static_cast<int>(w.buildings_.size()));
        for (const Building& b : w.buildings_) {
            if (!b.alive) {
                c.add(0xDEADu);
                continue;
            }
            c.add(static_cast<std::uint32_t>(b.cx) + (static_cast<std::uint32_t>(b.cy) << 16) +
                  static_cast<std::uint32_t>(b.type & 0xFF));
            c.add_i(b.house);
            c.add_i(b.hp);
            c.add_i(static_cast<int>(b.state));
            c.add_d(b.buildup_time);
            c.add_d(b.die_time);
        }
        // --- projectiles and crates (the original's Logic / layer lists)
        c.add_i(static_cast<int>(w.projectiles_.size()));
        for (const Projectile& p : w.projectiles_) {
            c.add(static_cast<std::uint32_t>(p.x) + (static_cast<std::uint32_t>(p.y) << 16) +
                  static_cast<std::uint32_t>(p.z));
            c.add_i(p.target_unit);
            c.add_i(p.target_building);
            c.add_i(static_cast<int>(p.kind));
        }
        c.add_i(static_cast<int>(w.crates_.size()));
        for (const World::Crate& k : w.crates_) c.add(static_cast<std::uint32_t>(k.x) + (static_cast<std::uint32_t>(k.y) << 16) + static_cast<std::uint32_t>(k.powerup));
        // --- house economy: wallets, every production slot, the legacy pool,
        // defeat / outcome.
        c.add_i(static_cast<int>(w.econ_.size()));
        for (const HouseEcon& e : w.econ_) {
            c.add_d(e.credits);
            c.add_i(e.held);
            for (int q = 0; q < kQueueCats; ++q) {
                const QueueSlot& s = e.queues.state(static_cast<QueueCat>(q)).slot;
                c.add_i(s.type);
                c.add_i(s.stage);
                c.add_d(s.frames_accum);
                c.add_d(s.spent);
                c.add_i(s.ready ? 1 : 0);
                c.add_i(static_cast<int>(e.backlog[static_cast<std::size_t>(q)].size()));
            }
        }
        // W9-05 (sync-hash policy, io_hash_only): the per-house production
        // rules each peer INSTALLED (not saved; the digest is taken at
        // install time) -- a different rulesmd / tech mask is a frame-0
        // desync, not a slow drift. The human mask joins it.
        c.add_i(static_cast<int>(w.econ_rules_set_.size()));
        for (char v : w.econ_rules_set_) c.add_i(v);
        for (std::uint64_t v : w.econ_rules_digest_) c.add_u64(v);
        c.add_u64(w.human_mask_);
        for (double v : w.house_credits_) c.add_d(v);
        for (std::uint8_t v : w.house_defeated_) c.add(v);
        for (int v : w.house_outcome_) c.add_i(v);
        // Diplomacy and the match end (w9fix-match W9-05-03..06): the one-way
        // alliance matrix, the lobby diplomacy rules, game over / observer.
        c.add_i(static_cast<int>(w.ally_pairs_.size()));
        for (std::uint8_t v : w.ally_pairs_) c.add(v);
        c.add_i(w.allies_allowed_ ? 1 : 0);
        c.add_i(w.ally_change_allowed_ ? 1 : 0);
        c.add_i(w.game_over_ ? 1 : 0);
        c.add_i(w.observer_reveal_ ? 1 : 0);
        c.add_i(w.victory_defeats_seen_);
        for (const World::HouseRec& h : w.houses_) { c.add_i(h.alliance_team); c.add_i(h.combatant ? 1 : 0); }
        // --- every sim RNG stream (peeked, never drawn; see the header) and
        // the subsystem frame clocks.
        c.add_i(w.crate_rng_.idx1());
        c.add_i(w.crate_rng_.idx2());
        for (int i = 0; i < 250; ++i) c.add(w.crate_rng_.table_word(i));
        c.add(w.proj_rng_.s);
        c.add(w.combat_rng_.s);
        c.add(w.ore_rng_.s);
        c.add(w.bridge_rng_.s);
        c.add(w.an_.rng);
        c.add_i(w.an_.frame);
        for (const World::HouseAiRec& r : w.ai_houses_) {
            if (!r.is_ai) continue;
            c.add(r.state.rng);
            c.add(r.team_state.rng);
            c.add(r.trigger_state.rng);
            c.add_i(r.enemy_house);
            c.add_i(static_cast<int>(r.team_state.teams.size()));
        }
        c.add_i(w.proj_frame_);
        c.add_u64(w.vet_frame_);
        c.add_u64(w.ability_frame_);
        c.add_u64(w.repair_frame_);
        if (extra)
            for (int v : *extra) c.add_i(v);
        if (triggers) trig(c, *triggers);
        return c.v;
    }
    // The campaign / co-op map-trigger runtime (the full sync hash's
    // SaveAccess::triggers, minus the text log and the static tables): a few
    // hundred words even on a large campaign map.
    static void trig(FrameCrc& c, const MapTriggerSystem& s) {
        c.add(s.rng_);
        c.add_i(static_cast<int>(s.frame_));
        c.add_i(static_cast<int>(s.globals_.size()));
        for (std::uint8_t v : s.globals_) c.add(v);
        c.add_i(static_cast<int>(s.locals_.size()));
        for (std::uint8_t v : s.locals_) c.add(v);
        c.add_i(static_cast<int>(s.mt_start_));
        c.add_i(static_cast<int>(s.mt_left_));
        c.add_i(s.outcome_);
        for (unsigned v : s.house_flags_) c.add(v);
        c.add_i(s.fired_);
        c.add_i(s.logic_fired_);
        c.add_i(s.house_fired_);
        c.add_i(s.variable_fired_);
        c.add_i(static_cast<int>(s.tags_.size()));
        for (const MapTriggerSystem::Tag& t : s.tags_) {
            c.add(static_cast<std::uint32_t>(t.type) + (t.destroyed ? 0x10000u : 0u) +
                  (static_cast<std::uint32_t>(t.instance_count) << 17));
            for (const MapTriggerSystem::Trig& g : t.trigs) {
                c.add(static_cast<std::uint32_t>(g.type) + (g.enabled ? 0x10000u : 0u) + (g.destroyed ? 0x20000u : 0u));
                c.add(g.bits);
                c.add_i(static_cast<int>(g.t_start));
                c.add_i(static_cast<int>(g.t_left));
                c.add_i(g.house_override);
            }
        }
    }
};

std::uint32_t net_frame_crc(const World& w, const std::vector<int>* extra, const MapTriggerSystem* triggers) {
    return NetCrcAccess::crc(w, extra, triggers);
}

}  // namespace ra2engine
