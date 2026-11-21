#include "group_move.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

#include <cmath>

namespace ra2engine {

std::vector<Cellxy> nearest_free_cells(Cellxy center, int count,
                                       const std::vector<uint8_t>& passable, int w, int h,
                                       const Occupancy& occ) {
    std::vector<Cellxy> out;
    if (count <= 0 || w <= 0 || h <= 0) return out;

    auto walkable = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h &&
               passable[static_cast<std::size_t>(y) * w + x] != 0;
    };
    auto consider = [&](int x, int y) {
        if ((int)out.size() >= count) return;
        if (!walkable(x, y) || !occ.is_free(x, y)) return;
        out.push_back({x, y});
    };

    // Ring 0 is the center; ring r is the square border at Chebyshev distance r.
    // The maximum Chebyshev distance inside a w*h grid is max(w,h)-1 < w+h, so every cell is reachable before the loop ends.
    const int kMaxRing = w + h;
    consider(center.x, center.y);
    for (int r = 1; r <= kMaxRing && (int)out.size() < count; ++r) {
        // Top and bottom edges of the ring.
        for (int x = center.x - r; x <= center.x + r; ++x) {
            consider(x, center.y - r);
            consider(x, center.y + r);
        }
        // Left and right edges (excluding the corners already done above).
        for (int y = center.y - r + 1; y <= center.y + r - 1; ++y) {
            consider(center.x - r, y);
            consider(center.x + r, y);
        }
    }
    return out;
}

std::vector<Cellxy> assign_formation(Cellxy goal, const std::vector<Cellxy>& mover_cells,
                                     const std::vector<uint8_t>& passable, int w, int h,
                                     const Occupancy& occ) {
    const std::size_t n = mover_cells.size();
    std::vector<Cellxy> targets(mover_cells);  // default: each mover keeps its own cell

    if (n == 0) return targets;

    // Treat the movers' own cells as free (they are about to leave), so a tight
    // group can fill the goal region even though they currently occupy it.
    Occupancy tmp = occ;
    for (const Cellxy& c : mover_cells) tmp.clear(c.x, c.y);

    std::vector<Cellxy> slots = nearest_free_cells(goal, static_cast<int>(n), passable, w, h, tmp);

    // Greedy: for each slot (nearest-to-goal first) pick the nearest unassigned
    // mover. Deterministic tie-break by mover index (first wins).
    std::vector<bool> taken(n, false);
    for (const Cellxy& slot : slots) {
        int best = -1;
        double best_d2 = 0.0;
        for (std::size_t m = 0; m < n; ++m) {
            if (taken[m]) continue;
            const double dx = mover_cells[m].x - slot.x;
            const double dy = mover_cells[m].y - slot.y;
            const double d2 = dx * dx + dy * dy;
            if (best < 0 || d2 < best_d2) {
                best = static_cast<int>(m);
                best_d2 = d2;
            }
        }
        if (best < 0) break;
        targets[best] = slot;
        taken[best] = true;
    }
    return targets;
}

std::vector<Cellxy> assign_infantry_formation(Cellxy goal, const std::vector<Cellxy>& mover_cells,
                                              const std::vector<int>& mover_ids,
                                              const std::vector<uint8_t>& passable, int w, int h,
                                              const Occupancy& occ,
                                              const std::vector<Cellxy>& blocked_cells) {
    const std::size_t n = mover_cells.size();
    std::vector<Cellxy> targets(mover_cells);
    if (n == 0 || w <= 0 || h <= 0) return targets;
    Occupancy tmp = occ;
    for (std::size_t m = 0; m < n && m < mover_ids.size(); ++m)
        tmp.release(mover_cells[m].x, mover_cells[m].y, mover_ids[m]);
    auto blocked = [&](int x, int y) {
        for (const Cellxy& c : blocked_cells)
            if (c.x == x && c.y == y) return true;
        return false;
    };
    // Slots: each free cell repeated by its free-spot count, nearest ring first.
    std::vector<Cellxy> slots;
    auto consider = [&](int x, int y) {
        if (slots.size() >= n) return;
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        if (passable[static_cast<std::size_t>(y) * w + x] == 0 || blocked(x, y)) return;
        if (tmp.at(x, y) != Occupancy::kEmpty && !tmp.subcells()) return;
        const int cap = tmp.subcells() ? (tmp.free_spot(x, y, 0) < 0 ? 0 : Occupancy::kSpots - tmp.infantry_count(x, y))
                                       : 1;
        for (int k = 0; k < cap && slots.size() < n; ++k) slots.push_back({x, y});
    };
    consider(goal.x, goal.y);
    for (int r = 1; r <= w + h && slots.size() < n; ++r) {
        for (int x = goal.x - r; x <= goal.x + r; ++x) {
            consider(x, goal.y - r);
            consider(x, goal.y + r);
        }
        for (int y = goal.y - r + 1; y <= goal.y + r - 1; ++y) {
            consider(goal.x - r, y);
            consider(goal.x + r, y);
        }
    }
    std::vector<bool> taken(n, false);
    for (const Cellxy& slot : slots) {
        int best = -1;
        double best_d2 = 0.0;
        for (std::size_t m = 0; m < n; ++m) {
            if (taken[m]) continue;
            const double dx = mover_cells[m].x - slot.x;
            const double dy = mover_cells[m].y - slot.y;
            const double d2 = dx * dx + dy * dy;
            if (best < 0 || d2 < best_d2) {
                best = static_cast<int>(m);
                best_d2 = d2;
            }
        }
        if (best < 0) break;
        targets[static_cast<std::size_t>(best)] = slot;
        taken[static_cast<std::size_t>(best)] = true;
    }
    return targets;
}

}  // namespace ra2engine
