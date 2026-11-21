#pragma once
#include <cstdint>
#include <vector>

namespace ra2engine {

// A width*height grid mapping each cell to the index of the unit that owns it
// (its current cell) or is reserving it (the next cell it is stepping into),
// or kEmpty. Pure: no SDL/OS deps, so it is unit-testable and compiled into
// both ra2assets_tests and ra2engine (see CMakeLists.txt). Off-grid
// coordinates read as kEmpty and are never enterable; all mutators no-op
// off-grid.
//
// W5-B R-061 infantry sub-cells [RE: CellClass::FindInfantrySubposition
// FUN_00481090; OccupationFlags 0x1 centre, 0x2 top (unused), 0x4 right,
// 0x8 left, 0x10 down, 0x20 vehicle]: once enable_subcells() is called, a
// unit marked as infantry (mark_infantry) holds one of the three spots 2/3/4
// of a cell instead of the whole cell -- up to three infantry share a cell --
// while a vehicle still takes the whole cell. An infantry may not enter a
// cell a vehicle holds; a vehicle may not enter a cell with infantry in it.
// Without enable_subcells() every unit is whole-cell (the pre-W5 behaviour).
class Occupancy {
public:
    friend struct SaveAccess;  // P7 Save/Load (engine/save_fields.hpp)
    static constexpr int kEmpty = -1;
    static constexpr int kSpots = 3;     // spots 2, 3, 4
    static constexpr int kFirstSpot = 2;
    // w8fix-layer W8-01-05 / W8-02-02 / W8-03-04 [RE: CellClass keeps two
    // content chains -- FirstObject +0xE4 (the ground) and AltObject +0xE8
    // (the bridge deck) -- and two occupation words +0x124 / +0x128; add /
    // remove FUN_0047E7B0 / FUN_0047E9A0 pick the chain by the object's
    // OnBridge byte +0x8C; IsClearToMove FUN_004833B0 reads the bridge word
    // for a deck mover; UnitClass::Crush 0x7404B0 walks only its own layer's
    // chain]: two planes. Plane 0 is the ground (and water) of every cell,
    // plane 1 the high-bridge deck (only deck cells ever hold anything there).
    // A query with kAnyLayer sees both planes (the pre-w8 single-plane view,
    // still used by placement / targeting callers); a mutator with kAnyLayer
    // uses the unit's recorded layer (mark_layer).
    static constexpr int kAnyLayer = -1;

    Occupancy() = default;
    Occupancy(int width, int height)
        : w_(width < 0 ? 0 : width), h_(height < 0 ? 0 : height),
          owner_(static_cast<std::size_t>(w_) * h_, kEmpty) {}

    int width() const { return w_; }
    int height() const { return h_; }

    // --- layers (w8fix-layer) ------------------------------------------------
    // The layer (0 ground, 1 deck) a unit stands on -- Unit::on_deck. The
    // default plane of set / set_spot.
    void mark_layer(int unit, int layer) {
        if (unit < 0) return;
        if (static_cast<std::size_t>(unit) >= lay_.size()) {
            if (layer <= 0) return;
            lay_.resize(static_cast<std::size_t>(unit) + 1, 0);
        }
        lay_[static_cast<std::size_t>(unit)] = layer > 0 ? 1 : 0;
    }
    int layer_of(int unit) const {
        return unit >= 0 && static_cast<std::size_t>(unit) < lay_.size() ? lay_[static_cast<std::size_t>(unit)] : 0;
    }

    // --- sub-cell mode -------------------------------------------------------
    void enable_subcells() {
        if (spots_.empty()) spots_.assign(static_cast<std::size_t>(w_) * h_ * kSpots, kEmpty);
        if (!owner_d_.empty() && spots_d_.empty()) spots_d_.assign(spots_.size(), kEmpty);
    }
    bool subcells() const { return !spots_.empty(); }
    void mark_infantry(int unit, bool infantry) {
        if (unit < 0) return;
        if (static_cast<std::size_t>(unit) >= inf_.size()) inf_.resize(static_cast<std::size_t>(unit) + 1, 0);
        inf_[static_cast<std::size_t>(unit)] = infantry ? 1 : 0;
    }
    bool is_infantry(int unit) const {
        return subcells() && unit >= 0 && static_cast<std::size_t>(unit) < inf_.size() &&
               inf_[static_cast<std::size_t>(unit)] != 0;
    }
    // Occupant of spot s (2..4) of (x,y), kEmpty if free/off-grid.
    int spot_unit(int x, int y, int s, int layer = kAnyLayer) const {
        if (!in(x, y) || !subcells() || s < kFirstSpot || s >= kFirstSpot + kSpots) return kEmpty;
        for (int l = lo(layer); l <= hi(layer); ++l) {
            const int v = sp(l, x, y, s);
            if (v != kEmpty) return v;
        }
        return kEmpty;
    }
    // The spot (2..4) `unit` holds in (x,y), or -1.
    int spot_of(int x, int y, int unit, int layer = kAnyLayer) const {
        if (!in(x, y) || !subcells()) return -1;
        for (int l = lo(layer); l <= hi(layer); ++l)
            for (int s = kFirstSpot; s < kFirstSpot + kSpots; ++s)
                if (sp(l, x, y, s) == unit) return s;
        return -1;
    }
    int infantry_count(int x, int y, int layer = kAnyLayer) const {
        if (!in(x, y) || !subcells()) return 0;
        int n = 0;
        for (int l = lo(layer); l <= hi(layer); ++l)
            for (int s = kFirstSpot; s < kFirstSpot + kSpots; ++s) n += sp(l, x, y, s) != kEmpty;
        return n;
    }
    // FindInfantrySubposition's choice: `pref` (0..4, the quadrant of the
    // approach point) if it is a free infantry spot, else the first free entry
    // of its preference row (DAT_0081BC94: 0->1,2,3,4  1->0,2,3,4  2->0,1,4,3
    // 3->0,1,4,2  4->0,2,3,1; spots 0/1 are never taken). pref 0 uses the
    // rotation row `rot` (0..3) of DAT_0081BCA8 (the original draws it from
    // the sync RNG). -1 = no free spot, or a vehicle holds the cell. With
    // kAnyLayer the spot must be free (and no vehicle) on both planes.
    int free_spot(int x, int y, int pref, int rot = 0, int layer = kAnyLayer) const {
        if (!in(x, y) || !subcells()) return -1;
        for (int l = lo(layer); l <= hi(layer); ++l)
            if (own(l, x, y) != kEmpty) return -1;
        static const signed char kRows[5][4] = {{1, 2, 3, 4}, {0, 2, 3, 4}, {0, 1, 4, 3},
                                                {0, 1, 4, 2}, {0, 2, 3, 1}};
        static const signed char kRot[4][4] = {{1, 2, 3, 4}, {2, 3, 4, 1}, {3, 4, 1, 2},
                                               {4, 1, 2, 3}};
        if (pref >= kFirstSpot && pref < kFirstSpot + kSpots && spot_unit(x, y, pref, layer) == kEmpty)
            return pref;
        const signed char* row = (pref <= 0 || pref > 4) ? kRot[rot & 3] : kRows[pref];
        for (int k = 0; k < 4; ++k) {
            const int s = row[k];
            if (s >= kFirstSpot && spot_unit(x, y, s, layer) == kEmpty) return s;
        }
        return -1;
    }
    // Put infantry `unit` in spot s of (x,y) on `layer` (kAnyLayer: the unit's
    // recorded layer), off any other spot / plane of that cell.
    void set_spot(int x, int y, int unit, int s, int layer = kAnyLayer) {
        if (!in(x, y) || !subcells() || s < kFirstSpot || s >= kFirstSpot + kSpots) return;
        const int l = layer == kAnyLayer ? layer_of(unit) : (layer > 0 ? 1 : 0);
        release(x, y, unit);
        if (l == 1) grow_deck();
        spr(l, x, y, s) = unit;
        note_claim(unit, x, y);
    }

    // --- whole-cell view (every pre-W5 caller) -------------------------------
    // The vehicle holding the cell, else the first infantry (spot order), else
    // kEmpty. kAnyLayer: the ground plane first, then the deck.
    int at(int x, int y, int layer = kAnyLayer) const {
        if (!in(x, y)) return kEmpty;
        for (int l = lo(layer); l <= hi(layer); ++l) {
            const int o = own(l, x, y);
            if (o != kEmpty) return o;
            if (subcells())
                for (int s = kFirstSpot; s < kFirstSpot + kSpots; ++s)
                    if (sp(l, x, y, s) != kEmpty) return sp(l, x, y, s);
        }
        return kEmpty;
    }
    bool is_free(int x, int y, int layer = kAnyLayer) const { return in(x, y) && at(x, y, layer) == kEmpty; }

    // unit may enter (x,y) iff it is in-grid AND (free OR already owned by
    // unit). Sub-cell mode: an infantry needs no vehicle there and a free spot
    // (or one it already holds); a vehicle needs the cell clear of infantry.
    // `layer`: the plane entered (kAnyLayer: both planes must allow it).
    bool can_enter(int x, int y, int unit, int layer = kAnyLayer) const {
        if (!in(x, y)) return false;
        for (int l = lo(layer); l <= hi(layer); ++l) {
            const int o = own(l, x, y);
            if (o != kEmpty && o != unit) return false;
            if (!subcells() || is_infantry(unit)) continue;
            for (int s = kFirstSpot; s < kFirstSpot + kSpots; ++s) {
                const int v = sp(l, x, y, s);
                if (v != kEmpty && v != unit) return false;
            }
        }
        if (subcells() && is_infantry(unit))
            return spot_of(x, y, unit, layer) >= 0 || free_spot(x, y, 0, 0, layer) >= 0;
        return true;
    }

    // Claim (x,y) for `unit` on `layer` (kAnyLayer: the unit's recorded
    // layer; the unit leaves the other plane of the cell): an infantry keeps
    // the spot it already holds there, else takes the first free one (spot
    // order 2,3,4; a full cell's spot 2 is overwritten, the pre-W5 "set
    // overwrites" contract); a vehicle owns the whole cell.
    void set(int x, int y, int unit, int layer = kAnyLayer) {
        if (!in(x, y)) return;
        const int l = layer == kAnyLayer ? layer_of(unit) : (layer > 0 ? 1 : 0);
        if (l == 1) grow_deck();
        drop_plane(1 - l, x, y, unit);
        note_claim(unit, x, y);
        if (!is_infantry(unit)) {
            ownr(l, x, y) = unit;
            return;
        }
        if (ownr(l, x, y) == unit) ownr(l, x, y) = kEmpty;
        if (spot_of(x, y, unit, l) >= 0) return;
        int s = -1;
        for (int k = kFirstSpot; k < kFirstSpot + kSpots && s < 0; ++k)
            if (sp(l, x, y, k) == kEmpty) s = k;
        spr(l, x, y, s < 0 ? kFirstSpot : s) = unit;
    }
    void clear(int x, int y) {
        if (!in(x, y)) return;
        for (int l = 0; l < planes(); ++l) {
            ownr(l, x, y) = kEmpty;
            if (subcells())
                for (int s = kFirstSpot; s < kFirstSpot + kSpots; ++s) spr(l, x, y, s) = kEmpty;
        }
    }
    // Clears (x,y) only if it is currently owned by `unit` (so a unit releasing
    // its old cell cannot stomp a claim another unit already made there).
    // Both planes.
    void release(int x, int y, int unit) {
        if (!in(x, y)) return;
        for (int l = 0; l < planes(); ++l) drop_plane(l, x, y, unit);
        forget_claim(unit, x, y);
    }
    // Clear EVERY cell owned by `unit` except (keep_x, keep_y). Used when a
    // unit is re-ordered mid-walk: it may hold a reservation on a next cell it
    // will now never enter, which would otherwise stay owned forever and block
    // other units.
    // W9-02-01 / W9-02-09 [RE: the original keeps occupancy per object -- a
    // FootClass marks only its own cell and its next-cell reservation, so an
    // order costs O(1) for occupancy]: the cells a unit was ever written into
    // (set / set_spot, either plane) are tracked per unit (claims_), and only
    // those are visited -- O(claims), 1-4 cells, instead of the old O(w*h)
    // scan of every cell on both planes (66 % of a late 8-player frame).
    // The result is identical to the full scan: the list is a superset of the
    // cells the unit holds (a stomp by another unit or clear() only leaves a
    // stale entry, which release() ignores), and release() is per (cell,
    // unit). After a load the index is rebuilt from the planes once.
    void release_all_of(int unit, int keep_x, int keep_y) {
        if (unit < 0) return;
        if (claims_dirty_) rebuild_claims();
        if (static_cast<std::size_t>(unit) >= claims_.size()) return;
        std::vector<std::uint32_t>& c = claims_[static_cast<std::size_t>(unit)];
        const bool keep_in = in(keep_x, keep_y);
        const std::uint32_t keep = keep_in ? static_cast<std::uint32_t>(idx(keep_x, keep_y)) : 0xFFFFFFFFu;
        bool kept = false;
        for (const std::uint32_t k : c) {
            if (k == keep) { kept = true; continue; }
            const int x = static_cast<int>(k % static_cast<std::uint32_t>(w_));
            const int y = static_cast<int>(k / static_cast<std::uint32_t>(w_));
            for (int l = 0; l < planes(); ++l) drop_plane(l, x, y, unit);
        }
        c.clear();
        if (kept) c.push_back(keep);
    }
    // Test seam: the number of tracked claim entries of `unit` (bounded: a
    // walking unit's list never grows past its live cells + stale stomps).
    std::size_t claim_count(int unit) const {
        return unit >= 0 && static_cast<std::size_t>(unit) < claims_.size() ? claims_[static_cast<std::size_t>(unit)].size() : 0;
    }

private:
    bool in(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
    std::size_t idx(int x, int y) const {
        return static_cast<std::size_t>(y) * w_ + x;
    }
    std::size_t sidx(int x, int y, int s) const {
        return idx(x, y) * kSpots + static_cast<std::size_t>(s - kFirstSpot);
    }
    int planes() const { return owner_d_.empty() ? 1 : 2; }
    int lo(int layer) const { return layer == kAnyLayer ? 0 : (layer > 0 ? 1 : 0); }
    int hi(int layer) const { return layer == kAnyLayer ? planes() - 1 : (layer > 0 ? 1 : 0); }
    // Reads of the (lazily allocated) deck plane are empty until it exists.
    int own(int l, int x, int y) const {
        const std::vector<int>& v = l ? owner_d_ : owner_;
        return v.empty() ? kEmpty : v[idx(x, y)];
    }
    int sp(int l, int x, int y, int s) const {
        const std::vector<int>& v = l ? spots_d_ : spots_;
        return v.empty() ? kEmpty : v[sidx(x, y, s)];
    }
    int& ownr(int l, int x, int y) { return (l ? owner_d_ : owner_)[idx(x, y)]; }
    int& spr(int l, int x, int y, int s) { return (l ? spots_d_ : spots_)[sidx(x, y, s)]; }
    void grow_deck() {
        if (owner_d_.empty()) owner_d_.assign(owner_.size(), kEmpty);
        if (subcells() && spots_d_.empty()) spots_d_.assign(spots_.size(), kEmpty);
    }
    // W9-02-01: the per-unit claim index (derived, never saved; see
    // release_all_of). Entries are flat cell indices, deduplicated.
    void note_claim(int unit, int x, int y) {
        if (unit < 0 || claims_dirty_) return;   // dirty: the rebuild will see it
        if (static_cast<std::size_t>(unit) >= claims_.size()) claims_.resize(static_cast<std::size_t>(unit) + 1);
        std::vector<std::uint32_t>& c = claims_[static_cast<std::size_t>(unit)];
        const std::uint32_t k = static_cast<std::uint32_t>(idx(x, y));
        for (const std::uint32_t e : c)
            if (e == k) return;
        c.push_back(k);
    }
    void forget_claim(int unit, int x, int y) {
        if (unit < 0 || claims_dirty_ || static_cast<std::size_t>(unit) >= claims_.size()) return;
        for (int l = 0; l < planes(); ++l) {
            if (own(l, x, y) == unit) return;
            if (subcells())
                for (int s = kFirstSpot; s < kFirstSpot + kSpots; ++s)
                    if (sp(l, x, y, s) == unit) return;
        }
        std::vector<std::uint32_t>& c = claims_[static_cast<std::size_t>(unit)];
        const std::uint32_t k = static_cast<std::uint32_t>(idx(x, y));
        for (std::size_t i = 0; i < c.size(); ++i)
            if (c[i] == k) {
                c[i] = c.back();
                c.pop_back();
                return;
            }
    }
    // One O(w*h) pass after a load (the planes come from the save; the index
    // does not).
    void rebuild_claims() {
        claims_dirty_ = false;
        claims_.clear();
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x)
                for (int l = 0; l < planes(); ++l) {
                    const int o = own(l, x, y);
                    if (o != kEmpty) note_claim(o, x, y);
                    if (subcells())
                        for (int s = kFirstSpot; s < kFirstSpot + kSpots; ++s) {
                            const int v = sp(l, x, y, s);
                            if (v != kEmpty) note_claim(v, x, y);
                        }
                }
    }
    void drop_plane(int l, int x, int y, int unit) {
        if (l < 0 || l >= planes()) return;
        if (ownr(l, x, y) == unit) ownr(l, x, y) = kEmpty;
        if (subcells())
            for (int s = kFirstSpot; s < kFirstSpot + kSpots; ++s)
                if (spr(l, x, y, s) == unit) spr(l, x, y, s) = kEmpty;
    }
    int w_ = 0, h_ = 0;
    std::vector<int> owner_;
    std::vector<int> spots_;            // W5-B: 3 infantry spots per cell (sub-cell mode)
    std::vector<std::uint8_t> inf_;     // W5-B: per unit index, 1 = infantry
    std::vector<int> owner_d_;          // w8fix-layer: the deck plane (empty until a deck unit)
    std::vector<int> spots_d_;          // w8fix-layer: the deck plane's infantry spots
    std::vector<std::uint8_t> lay_;     // w8fix-layer: per unit index, 1 = on a deck
    // W9-02-01: derived per-unit claim index (NOT saved; rebuilt after a load).
    std::vector<std::vector<std::uint32_t>> claims_;
    bool claims_dirty_ = false;
};

// W5-B R-061 [RE: spot coordinates DAT_0089D960, static initializer
// 0x48E390]: spot k's offset from the cell centre in leptons -- 0 centre
// (128,128), 1 top (64,64), 2 right (192,64), 3 left (64,192), 4 down
// (192,192), minus the centre.
inline void spot_offset(int spot, int& dx, int& dy) {
    static const signed char kOff[5][2] = {{0, 0}, {-64, -64}, {64, -64}, {-64, 64}, {64, 64}};
    if (spot < 0 || spot > 4) spot = 0;
    dx = kOff[spot][0];
    dy = kOff[spot][1];
}

// FindInfantrySubposition's quadrant of a point (in-cell offset from the cell
// centre, leptons): within 60 leptons of the centre -> 0; else x>0 -> +1,
// y>0 -> +2, nonzero +1 (so right 2, left 3, down 4; the top quadrant -> 0).
inline int spot_quadrant(int ox, int oy) {
    if (ox * ox + oy * oy < 60 * 60) return 0;
    const int q = (ox > 0 ? 1 : 0) | (oy > 0 ? 2 : 0);
    return q ? q + 1 : 0;
}

}  // namespace ra2engine
