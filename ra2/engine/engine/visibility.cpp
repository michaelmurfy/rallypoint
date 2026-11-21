#include "visibility.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

namespace ra2engine {

int reveal_disc(std::vector<std::uint8_t>& explored,
                std::vector<std::uint8_t>& visible,
                int gw, int gh, int cx, int cy, int sight) {
    if (gw <= 0 || gh <= 0) return 0;
    if ((int)explored.size() != gw * gh || (int)visible.size() != gw * gh) return 0;
    const int r = sight < 0 ? 0 : sight;   // negative sight -> source cell only
    int n = 0;
    for (int dy = -r; dy <= r; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= gh) continue;
        for (int dx = -r; dx <= r; ++dx) {
            const int x = cx + dx;
            if (x < 0 || x >= gw) continue;
            if (!cell_in_sight(dx, dy, r)) continue;   // near-circular filter
            const int idx = y * gw + x;
            visible[idx] = 1;
            explored[idx] = 1;   // sticky
            ++n;
        }
    }
    return n;
}

}  // namespace ra2engine
