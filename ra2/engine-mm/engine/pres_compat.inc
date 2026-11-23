// pres_compat.inc -- W5-PRES: compile-time hooks onto sim state that other
// wave-5 branches add (w4fix-seq: high-bridge destruction, paratroopers;
// agent B: the infantry sub-cell offset). Each helper detects the member with
// SFINAE, so this branch builds and renders correctly before AND after those
// branches merge; with the member absent the hook is inert.
// Included by main.cpp before pres_map.inc.

// w4fix-seq W4-SQ-10: World::high_bridge_destroyed(x, y) -- a broken span's
// deck stops drawing.
template <class Wd>
auto pres_hbd_impl(const Wd& w, int x, int y, int) -> decltype(bool(w.high_bridge_destroyed(x, y))) {
    return w.high_bridge_destroyed(x, y);
}
template <class Wd>
bool pres_hbd_impl(const Wd&, int, int, long) { return false; }
inline bool pres_high_deck_destroyed(const ra2engine::World& w, int x, int y) { return pres_hbd_impl(w, x, y, 0); }

// w4fix-seq W4-SQ-06: a paratrooper hangs under its chute while
// u.cargo_of == kParachuteCargo (0x3FFFFFFE) with u.an.para_alt > 0 (leptons).
constexpr int kPresParachuteCargo = 0x3FFFFFFE;
template <class U>
auto pres_para_alt_impl(const U& u, int) -> decltype(double(u.an.para_alt)) { return u.an.para_alt; }
template <class U>
double pres_para_alt_impl(const U&, long) { return 0.0; }
inline double pres_para_alt(const ra2engine::Unit& u) {
    return u.cargo_of == kPresParachuteCargo ? pres_para_alt_impl(u, 0) : 0.0;
}

// Agent B (R-061, 3 infantry per cell): the sub-cell position. B landed it as
// Unit::sub_x / sub_y (leptons from the cell centre, interpolated while
// walking). iso_renderer's draw_unit, tile_center_screen overlays, input
// picking and the main.cpp pips/chevrons already add it -- use this only for
// a draw path that projects u.fx/u.fy itself.
template <class U>
auto pres_subcell_impl(const U& u, double& dx, double& dy, int) -> decltype(void(u.sub_x), void(u.sub_y)) {
    dx = u.sub_x / 256.0;
    dy = u.sub_y / 256.0;
}
template <class U>
void pres_subcell_impl(const U&, double& dx, double& dy, long) { dx = dy = 0.0; }
inline void pres_subcell_offset(const ra2engine::Unit& u, double& dx, double& dy) { pres_subcell_impl(u, dx, dy, 0); }

// Agent C (R-047): a still Mirage's disguise tree, u.w5.mirage_tree (a
// TerrainType id; "" none). [hook: inert until w5-abil merges]
template <class U>
auto pres_mirage_impl(const U& u, int) -> decltype(std::string(u.w5.mirage_tree)) { return u.w5.mirage_tree; }
template <class U>
std::string pres_mirage_impl(const U&, long) { return std::string(); }
inline std::string pres_mirage_tree(const ra2engine::Unit& u) {
    // W7-03-04: during the fake blink after a shot the tank shows.
    if (!u.disguised || u.w7.blink_left > 0) return std::string();
    std::string t = pres_mirage_impl(u, 0);
    // --play diagnostic before agent C's state lands: RA2_PRES_MIRAGE=<tree>.
    if (t.empty() && u.disguise_when_still)
        if (const char* e = std::getenv("RA2_PRES_MIRAGE")) t = e;
    return t;
}
