#pragma once
// W8-03-02 / W8-03-01 (w8fix-place): what a map overlay does to its cell's
// LandType and to building placement. Pure, header-only (unit-tested in
// tests/test_building.cpp).
//
// [RE: CellClass recalc FUN_0047D1C0, overlay branch] the cell takes
// LandType = OverlayType+0x298 (Land=). When that Land is Wall (4) or
// Railroad (9), or +0x2AC NoUseTileLandType is set, the cell keeps it and
// returns before the tile's land type is read; then [General]
// CliffBackImpassability=2 turns it into Rock when one of the six back
// neighbours is 4+ levels higher. Otherwise the tile land wins.
// [RE: OverlayTypeClass::ReadINI @0x5FDF88..0x5FE163] Land= (default the
// constructor's Clear) is read at +0x298; a Tiberium=yes type with Land=Clear
// becomes Tiberium (5); NoUseTileLandType at +0x2AC.
// [CONFIRMED-DATA rulesmd] SROCK01-05, TROCK01-05, LUNRK1-6, FENCE01-22:
// Land=Rock + NoUseTileLandType=true -> impassable Rock. CRAT*/DRUM*/PALET*:
// Land=Rock WITHOUT NoUseTileLandType -> the tile wins (passable).
// TRACKS*/TRACKTUNNEL*: Land=Railroad -> Railroad. RUBBLE_OVERLAY:
// NoUseTileLandType with no Land -> Clear.
#include <cctype>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ra2engine {

struct OverlayLandInfo {
    int land = -1;          // the LandType the overlay forces, -1 = the tile's wins
    bool tiberium = false;  // Tiberium=yes (ore/gems: live in OreField, not a static block)
};

inline int overlay_land_from_name(const std::string& v) {
    static const char* kLand[12] = {"clear", "road", "water", "rock", "wall", "tiberium",
                                    "beach", "rough", "ice", "railroad", "tunnel", "weeds"};
    std::string s;
    for (char c : v) if (!std::isspace(static_cast<unsigned char>(c))) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (int i = 0; i < 12; ++i) if (s == kLand[i]) return i;
    return 0;   // the constructor default (Clear)
}

// `get(name, key)` returns the rules value ("" when absent). One entry per
// RUNTIME overlay index (the order of [OverlayTypes], overlay_names_in_order).
inline std::vector<OverlayLandInfo> overlay_land_table(
    const std::vector<std::string>& names,
    const std::function<std::string(const std::string&, const std::string&)>& get) {
    auto yes = [](const std::string& v) {
        for (char c : v) {
            if (std::isspace(static_cast<unsigned char>(c))) continue;
            const char l = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return l == 'y' || l == 't' || l == '1';
        }
        return false;
    };
    std::vector<OverlayLandInfo> out(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        OverlayLandInfo& o = out[i];
        o.tiberium = yes(get(names[i], "Tiberium"));
        int land = overlay_land_from_name(get(names[i], "Land"));
        if (o.tiberium && land == 0) land = 5;            // 0x5FE0D7
        const bool nouse = yes(get(names[i], "NoUseTileLandType"));
        if (land == 4 || land == 9 || nouse) o.land = land;   // FUN_0047D1C0 overlay branch
    }
    return out;
}

// Applies the overlay branch to `lt` (LandType values, grid_w*grid_h) for
// every cell whose overlay id `ovl[i]` (-1 none) forces its land, then the
// CliffBackImpassability=2 Rock rule over `lvl`. `skip(id)` excludes overlay
// families another module owns (the low bridges: depth_set_terrain).
template <class LandT>
void apply_overlay_land(std::vector<LandT>& lt, const std::vector<int>& ovl, const std::vector<int>& lvl,
                        int gw, int gh, const std::vector<OverlayLandInfo>& tab, int cliff_back,
                        const std::function<bool(int)>& skip) {
    static const int kNb[6][2] = {{0, -1}, {-1, 0}, {2, 2}, {1, 1}, {1, -1}, {-1, 1}};
    for (int y = 0; y < gh; ++y)
        for (int x = 0; x < gw; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * gw + x;
            if (i >= ovl.size() || i >= lt.size()) continue;
            const int id = ovl[i];
            if (id < 0 || id >= static_cast<int>(tab.size()) || tab[id].land < 0) continue;
            if (skip && skip(id)) continue;
            int land = tab[id].land;
            if (cliff_back != 0 && i < lvl.size()) {
                for (const auto& d : kNb) {
                    const int nx = x + d[0], ny = y + d[1];
                    const int nl = (nx >= 0 && ny >= 0 && nx < gw && ny < gh)
                                       ? lvl[static_cast<std::size_t>(ny) * gw + nx] : 0;
                    if (nl >= lvl[i] + 4) {
                        if (cliff_back == 2) land = 3;   // Rock
                        break;
                    }
                }
            }
            lt[i] = static_cast<LandT>(land);
        }
}

}  // namespace ra2engine
