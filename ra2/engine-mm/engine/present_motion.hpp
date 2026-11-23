#pragma once

// Draw-only blend between the previous logic tick and the current one.
// The simulation stays at 15 Hz (kLogicDt). A display faster than that was
// holding the post-tick pose and then jumping. Nothing here is saved or
// checksummed; step_units is not touched.

#include "world.hpp"

#include <vector>

namespace ra2engine {

// Fraction of the way from the previous tick to the current one.
// logic_acc is the time already accumulated toward the next tick.
inline double present_alpha(double logic_acc, double logic_dt) {
    if (!(logic_dt > 0.0)) return 1.0;
    const double a = logic_acc / logic_dt;
    if (!(a > 0.0)) return 0.0;
    if (a >= 1.0) return 1.0;
    return a;
}

// Previous poses, captured at the start of the last logic tick that ran.
struct PresentMotion {
    struct Sample {
        double fx = 0, fy = 0, sub_x = 0, sub_y = 0;
    };
    std::vector<Sample> prev;
    double alpha = 1.0;
    bool armed = false;

    void capture(const std::vector<Unit>& units) {
        prev.resize(units.size());
        for (std::size_t i = 0; i < units.size(); ++i)
            prev[i] = {units[i].fx, units[i].fy, units[i].sub_x, units[i].sub_y};
        armed = true;
    }

    // Writes the pose to draw. A step longer than 2 cells is a teleport
    // (chronosphere, spawn, cargo): show the new pose, do not slide.
    void blend(const Unit& u, int index, double& fx, double& fy, double& sx, double& sy) const {
        fx = u.fx;
        fy = u.fy;
        sx = u.sub_x;
        sy = u.sub_y;
        if (!armed || !(alpha < 1.0) || index < 0 ||
            static_cast<std::size_t>(index) >= prev.size())
            return;
        const Sample& p = prev[static_cast<std::size_t>(index)];
        const double dx = u.fx - p.fx, dy = u.fy - p.fy;
        if (dx * dx + dy * dy > 4.0) return;
        fx = p.fx + dx * alpha;
        fy = p.fy + dy * alpha;
        sx = p.sub_x + (u.sub_x - p.sub_x) * alpha;
        sy = p.sub_y + (u.sub_y - p.sub_y) * alpha;
    }
};

struct PresentBind {
    const PresentMotion* motion = nullptr;
    const Unit* units = nullptr;
    int n = 0;
};

inline PresentBind& present_bind_slot() {
    static PresentBind b;
    return b;
}

inline int& present_draw_index() {
    static int i = -1;
    return i;
}

// Active for one world draw. The unit vector must not grow while it is up.
struct PresentScope {
    explicit PresentScope(const PresentMotion& m, const std::vector<Unit>& units) {
        PresentBind& s = present_bind_slot();
        s.motion = &m;
        s.units = units.data();
        s.n = static_cast<int>(units.size());
    }
    ~PresentScope() { present_bind_slot() = {}; }
    PresentScope(const PresentScope&) = delete;
    PresentScope& operator=(const PresentScope&) = delete;
};

// Forces the draw index for a copied Unit (a bridge lift) that is not itself
// an element of the bound array. Restores the previous index.
struct PresentIndex {
    int prev;
    explicit PresentIndex(int i) : prev(present_draw_index()) { present_draw_index() = i; }
    ~PresentIndex() { present_draw_index() = prev; }
    PresentIndex(const PresentIndex&) = delete;
    PresentIndex& operator=(const PresentIndex&) = delete;
};

// Ground point in cells, including the infantry sub-cell. With no bind this
// is fx + sub/256, the same value the draw path used before.
inline void present_ground(const Unit& u, double& gx, double& gy) {
    const PresentBind& b = present_bind_slot();
    int index = present_draw_index();
    const Unit* src = &u;
    if (index < 0 && b.units) {
        const int i = static_cast<int>(&u - b.units);
        if (i >= 0 && i < b.n && &b.units[i] == &u) index = i;
    }
    if (b.units && index >= 0 && index < b.n) src = &b.units[index];
    double fx = src->fx, fy = src->fy, sx = src->sub_x, sy = src->sub_y;
    if (b.motion && index >= 0) b.motion->blend(*src, index, fx, fy, sx, sy);
    gx = fx + sx / 256.0;
    gy = fy + sy / 256.0;
}

// Cell fx/fy for depth sort. Unbound, leaves fx/fy alone (no sub-cell term).
inline void present_cell(const Unit& u, double& fx, double& fy) {
    const PresentBind& b = present_bind_slot();
    if (!b.motion || !b.units) return;
    const int i = static_cast<int>(&u - b.units);
    if (i < 0 || i >= b.n || &b.units[i] != &u) return;
    double sx, sy;
    b.motion->blend(u, i, fx, fy, sx, sy);
}

}  // namespace ra2engine
