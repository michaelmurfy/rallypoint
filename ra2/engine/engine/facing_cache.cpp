#include "facing_cache.hpp"

namespace ra2engine {

FacingSet build_facing_set(const ra2assets::VxlModel& m, const ra2assets::HvaModel& hva,
                           int n_facings, const ra2assets::Vpl& vpl,
                           const ra2assets::Palette& pal) {
    FacingSet out;
    out.sprites.reserve(n_facings);
    for (int i = 0; i < n_facings; ++i)
        out.sprites.push_back(rasterize_model(m, hva, index_to_yaw(i, n_facings), vpl, pal));
    return out;
}

}  // namespace ra2engine
