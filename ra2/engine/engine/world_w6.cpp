// Wave-6 fix #2 (missions): target legality (Evaluate_Object rejects), the
// IsInPlayfield flag and the per-house difficulty handicap. World members
// declared in world.hpp ("W6 fix #2"). Everything is inert until a campaign
// host calls set_playfield / set_house_handicap / set_building_target_flags.
#include "world.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int
#include "campaign.hpp"   // within_usable_area (IsWithinUsableArea FUN_00577E90)
#include "veterancy.hpp"  // rof_frames (the GetROF ftol)

namespace ra2engine {

bool World::legal_threat_unit(int scanner_house, int j) const {
    if (j < 0 || j >= static_cast<int>(units_.size())) return false;
    return legal_threat_unit(scanner_house, units_[static_cast<std::size_t>(j)]);
}

bool World::legal_threat_building(int scanner_house, int bi) const {
    if (bi < 0 || bi >= static_cast<int>(buildings_.size())) return false;
    return legal_threat_building(scanner_house, buildings_[static_cast<std::size_t>(bi)]);
}

bool World::legal_threat_unit(int /*scanner_house*/, const Unit& v) const {
    if (!v.alive || v.die_time >= 0.0 || v.cargo_of >= 0) return false;
    // [RE: Evaluate_Object FUN_006F71D0 @0x6F7A5C: type +0x231 LegalTarget]
    if (!v.tg.t.legal_target) return false;
    // [@0x6F735E: +0x3D5 IsInPlayfield]
    if (!v.tg.in_playfield) return false;
    // [@0x6F7B60..: an Insignificant (+0x232) non-building is rejected in a
    // campaign (GameMode DAT_00A8A1A0 == 0); in a multiplayer game only an
    // infantry capture/occupy case passes -- not modelled, the flag is
    // campaign-only here.]
    if (playfield_.campaign && v.tg.t.insignificant) return false;
    // W7-07-10 / W7-09-07 [RE: Evaluate_Object 0x6F732F..0x6F733B]: a target
    // whose mission has MissionControl NoThreat (Harmless, Selling).
    if (v.tg.mission >= 0 && mission_control(v.tg.mission).no_threat) return false;
    if (w7_rules_.armed) {
        // W7-09-03 [RE: 0x6F7894..0x6F790E]: Insignificant is rejected in
        // EVERY game mode; [0x6F779E] outside a campaign any object of a
        // passive (civilian) house.
        if (v.tg.t.insignificant) return false;
        if (!playfield_.campaign && passive_house(v.house)) return false;
    }
    return true;
}

bool World::legal_threat_building(int /*scanner_house*/, const Building& b) const {
    if (!b.alive || b.state == Building::State::Dead) return false;
    if (!b.tg.legal_target) return false;
    if (!b.tg.in_playfield) return false;
    // [same block: an Insignificant BUILDING is rejected only when its owner's
    // country is MultiplayPassive (+0x1A6) -- the civilian houses. ALL12:
    // 31 of the 32 Civie1 types, the Kremlin included.]
    if (playfield_.campaign && b.tg.insignificant && passive_house(b.house)) return false;
    if (w7_rules_.armed) {
        // W7-09-03: the Insignificant-building rule in every mode, and outside
        // a campaign any object of a passive house.
        if (b.tg.insignificant && passive_house(b.house)) return false;
        if (!playfield_.campaign && passive_house(b.house)) return false;
    }
    return true;
}

bool World::cell_in_playfield(int cx, int cy) const {
    if (!playfield_.valid) return true;
    MapLocalSize m;
    m.valid = true;
    m.size_w = playfield_.size_w;
    m.size_h = grid_h_;
    m.x = playfield_.x;
    m.y = playfield_.y;
    m.w = playfield_.w;
    m.h = playfield_.h;
    const int lvl = (cx >= 0 && cy >= 0 && cx < grid_w_ && cy < grid_h_) ? cell_level(cx, cy) : 0;
    return within_usable_area(m, cx, cy, lvl);
}

void World::set_playfield(bool campaign, bool valid, int size_w, int x, int y, int w, int h) {
    playfield_.campaign = campaign;
    playfield_.valid = valid;
    playfield_.size_w = size_w;
    playfield_.x = x;
    playfield_.y = y;
    playfield_.w = w;
    playfield_.h = h;
    // [RE: ResizeView FUN_00566E60 -- every object's IsInPlayfield is
    // recomputed from its cell, both ways.]
    for (Unit& u : units_) u.tg.in_playfield = cell_in_playfield(u.cell_x, u.cell_y);
    for (Building& b : buildings_)
        b.tg.in_playfield = cell_in_playfield(b.cx + b.fw / 2, b.cy + b.fh / 2);
}

void World::step_playfield() {
    if (!playfield_.valid) return;
    // [RE: FUN_006F45F0 -- a unit that moves into the usable area gains the
    // flag; it never loses it by moving out.]
    for (Unit& u : units_) {
        if (u.tg.in_playfield || !u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
        if (cell_in_playfield(u.cell_x, u.cell_y)) u.tg.in_playfield = true;
    }
}

void World::set_building_target_flags(int bi, bool insignificant, bool legal_target) {
    if (bi < 0 || bi >= static_cast<int>(buildings_.size())) return;
    Building& b = buildings_[static_cast<std::size_t>(bi)];
    b.tg.insignificant = insignificant;
    b.tg.legal_target = legal_target;
}

void World::set_house_handicap(int h, double firepower, double rof) {
    if (h < 0) return;
    const std::size_t n = static_cast<std::size_t>(h) + 1;
    if (handicap_set_.size() < n) {
        handicap_set_.resize(n, 0);
        handicap_fp_.resize(n, 1.0);
        handicap_rof_.resize(n, 1.0);
    }
    handicap_set_[static_cast<std::size_t>(h)] = 1;
    handicap_fp_[static_cast<std::size_t>(h)] = firepower;
    handicap_rof_[static_cast<std::size_t>(h)] = rof;
    // W6-02-02: units that already exist take the new multiplier now.
    for (int i = 0; i < static_cast<int>(units_.size()); ++i)
        if (units_[static_cast<std::size_t>(i)].house == h && units_[static_cast<std::size_t>(i)].alive)
            refresh_vet_stats(i);
}

double World::house_rof_mul(int h) const {
    if (h >= 0 && h < static_cast<int>(handicap_set_.size()) && handicap_set_[static_cast<std::size_t>(h)])
        return handicap_rof_[static_cast<std::size_t>(h)];
    return ai_enabled(h) ? ai_houses_[static_cast<std::size_t>(h)].cfg.mods.rof_mul : 1.0;
}

double World::house_firepower_mul(int h) const {
    if (h >= 0 && h < static_cast<int>(handicap_set_.size()) && handicap_set_[static_cast<std::size_t>(h)])
        return handicap_fp_[static_cast<std::size_t>(h)];
    return ai_enabled(h) ? ai_houses_[static_cast<std::size_t>(h)].cfg.mods.firepower_mul : 1.0;
}

int World::house_rof_frames(int h, int rof) const {
    const double m = house_rof_mul(h);
    if (m == 1.0) return rof;
    return rof_frames(rof, false, 1.0, m);
}

bool World::psionic_target_ok(int j) const {
    if (j < 0 || j >= static_cast<int>(units_.size())) return false;
    return !units_[static_cast<std::size_t>(j)].tg.t.immune_psionics;
}

}  // namespace ra2engine
