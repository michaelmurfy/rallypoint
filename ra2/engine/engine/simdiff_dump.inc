// ============================================================================
// simdiff dump (--ai-soak --diff-dump <file>): writes the World's state once
// per logic frame in the record format of tools/simdiff (docs/re/
// simdiff-harness.md section 3), the same format the original-game hook
// (tools/simdiff/hook/simhook.dll) writes. READ-ONLY: it only inspects the
// World through const accessors and never draws from any RNG, so a run with
// the switch simulates exactly like a run without it.
//
// Fields the clone does not model are written as '-' (the differ reports
// them as absent): the sync CRC (no map layers / Logic vector / sync RNG),
// UniqueID, Mission, the twin RNG and the Logic census. 'rng' is the crate
// GameRandom (the only R250 instance the clone has).
// ============================================================================
#include <cstdio>

namespace simdiff {

inline unsigned fnv1a_words(const ra2engine::GameRandom& r) {
    unsigned h = 2166136261u;
    for (int i = 0; i < 250; ++i) {
        const std::uint32_t w = r.table_word(i);
        for (int b = 0; b < 4; ++b) { h ^= (w >> (8 * b)) & 0xFFu; h *= 16777619u; }
    }
    return h;
}

inline bool unit_on_map(const ra2engine::Unit& u) {
    return u.alive && u.die_time < 0.0 && u.cargo_of < 0;
}

inline void write_header(std::FILE* f, const std::string& map, unsigned seed) {
    std::fprintf(f, "# simdiff v1 src=clone seed=%08X session=5 scenario=%s every=1\n", seed, map.c_str());
}

inline void write_frame(std::FILE* f, const ra2engine::World& w, int frame) {
    int ninf = 0, nunit = 0, nair = 0, nbld = 0;
    for (const ra2engine::Unit& u : w.units()) {
        if (!unit_on_map(u)) continue;
        if (u.is_infantry) ++ninf; else if (u.is_aircraft) ++nair; else ++nunit;
    }
    for (const ra2engine::Building& b : w.buildings())
        if (b.alive && b.state != ra2engine::Building::State::Dead) ++nbld;
    const ra2engine::GameRandom& r = w.crate_rng();
    std::fprintf(f, "F %d crc - rng %d %d %08X rng2 - - - vec %d %d %d %d logic - layers - - - - - cen -\n",
                 frame, r.idx1(), r.idx2(), fnv1a_words(r), ninf, nunit, nair, nbld);
    for (const ra2engine::Unit& u : w.units()) {
        if (!unit_on_map(u)) continue;
        const int rtti = u.is_infantry ? 15 : (u.is_aircraft ? 2 : 1);
        const std::string& type = u.tg.t.rules_id.empty() ? u.image : u.tg.t.rules_id;
        int facing, turret = -1;
        if (u.is_infantry) {
            facing = (u.facing & 7) * 0x2000;   // clone: 8-way index (assumed 0 = DirStruct 0)
        } else {
            facing = static_cast<int>(u.body_facing) & 0xFFFF;
            if (rtti == 1) turret = static_cast<int>(u.turret_facing) & 0xFFFF;
        }
        std::fprintf(f, "O %d - %s %d %d %d %d %d %d %d -\n", rtti, type.c_str(), u.house,
                     ra2engine::unit_lept_x(u), ra2engine::unit_lept_y(u), ra2engine::unit_lept_z(u),
                     facing, turret, u.hp);
    }
    const auto& bt = w.btypes();
    for (const ra2engine::Building& b : w.buildings()) {
        if (!b.alive || b.state == ra2engine::Building::State::Dead) continue;
        // x,y: the original's BuildingClass Location (+0x9C, what the CRC folds)
        // is the centre of the foundation's ORIGIN cell, not of the foundation
        // (measured on XMP29U2 frame 0: CAOILD, CAAIRP, CASTL01).
        const char* id = (b.type >= 0 && b.type < (int)bt.size()) ? bt[(std::size_t)b.type].id.c_str() : "?";
        std::fprintf(f, "O 6 - %s %d %d %d - - -1 %d -\n", id, b.house,
                     b.cx * 256 + 128, b.cy * 256 + 128, b.hp);   // origin-cell centre, see below
    }
}

}  // namespace simdiff
