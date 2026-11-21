// pres_shroud.inc -- G-84 shroud / fog post-pass. Included by main.cpp BEFORE
// render_world (which calls draw_shroud_pass after every object is drawn).
//
// [RE: presentation.md §4, High] The original draws, per cell, a SHROUD.SHP
// tile (FUN_0047eef0) and a FOG.SHP tile (FUN_0047f160) at the cell's screen
// point minus (30,15). Frame = FUN_006d7c70(cell, mode):
//   !(self & ANY) -> 15 (fully covered); !(self & BIT) -> 0 (empty);
//   else mask of the 8 neighbours LACKING the bit, (x-1,y-1)=0x40 (x,y-1)=0x80
//   (x+1,y-1)=0x01 (x-1,y)=0x20 (x+1,y)=0x02 (x-1,y+1)=0x10 (x,y+1)=0x08
//   (x+1,y+1)=0x04, frame = ShroudLUT[mask] (0x007F3194; 0xFF -> 0, 0xFE -> 15).
// Mode 0 (shroud) tests "explored", mode 1 (fog) "visible". Pixel values are
// brightness written to an intensity surface (0x7F = untouched, 0 = black;
// 0xFE transparent): we composite them as black at alpha (127 - v)/127.
// Fog tiles darken further where they overlap (subtractive) -- alpha blending
// over the shroud layer approximates that. [Low: BlendedFog compose not traced.]

const ra2assets::ShpFile* g_shroud_shp = nullptr;   // conquer.mix SHROUD.SHP (GameScene)
const ra2assets::ShpFile* g_fog_shp = nullptr;      // conquer.mix FOG.SHP (GameScene)

namespace pres_shroud {

constexpr uint8_t kLut[256] = {
    0xff,0x21,0x02,0x02,0x22,0x25,0x02,0x02,0x04,0x1a,0x06,0x06,0x04,0x1a,0x06,0x06,
    0x23,0x2d,0x11,0x11,0x26,0x29,0x11,0x11,0x04,0x1a,0x06,0x06,0x04,0x1a,0x06,0x06,
    0x08,0x15,0x0a,0x0a,0x1b,0x1f,0x0a,0x0a,0x0c,0x17,0x0e,0x0e,0x0c,0x17,0x0e,0x0e,
    0x08,0x15,0x0a,0x0a,0x1b,0x1f,0x0a,0x0a,0x0c,0x17,0x0e,0x0e,0x0c,0x17,0x0e,0x0e,
    0x20,0x24,0x19,0x19,0x2c,0x28,0x19,0x19,0x13,0x1e,0x14,0x14,0x13,0x1e,0x14,0x14,
    0x27,0x2b,0x1d,0x1d,0x2a,0x2e,0x1d,0x1d,0x13,0x1e,0x14,0x14,0x13,0x1e,0x14,0x14,
    0x08,0x15,0x0a,0x0a,0x1b,0x1f,0x0a,0x0a,0x0c,0x17,0x0e,0x0e,0x0c,0x17,0x0e,0x0e,
    0x08,0x15,0x0a,0x0a,0x1b,0x1f,0x0a,0x0a,0x0c,0x17,0x0e,0x0e,0x0c,0x17,0x0e,0x0e,
    0x01,0x01,0x03,0x03,0x10,0x10,0x03,0x03,0x05,0x05,0x07,0x07,0x05,0x05,0x07,0x07,
    0x18,0x18,0x12,0x12,0x1c,0x1c,0x12,0x12,0x05,0x05,0x07,0x07,0x05,0x05,0x07,0x07,
    0x09,0x09,0x0b,0x0b,0x16,0x16,0x0b,0x0b,0x0d,0x0d,0xfe,0xfe,0x0d,0x0d,0xfe,0xfe,
    0x09,0x09,0x0b,0x0b,0x16,0x16,0x0b,0x0b,0x0d,0x0d,0xfe,0xfe,0x0d,0x0d,0xfe,0xfe,
    0x01,0x01,0x03,0x03,0x10,0x10,0x03,0x03,0x05,0x05,0x07,0x07,0x05,0x05,0x07,0x07,
    0x18,0x18,0x12,0x12,0x1c,0x1c,0x12,0x12,0x05,0x05,0x07,0x07,0x05,0x05,0x07,0x07,
    0x09,0x09,0x0b,0x0b,0x16,0x16,0x0b,0x0b,0x0d,0x0d,0xfe,0xfe,0x0d,0x0d,0xfe,0xfe,
    0x09,0x09,0x0b,0x0b,0x16,0x16,0x0b,0x0b,0x0d,0x0d,0xfe,0xfe,0x0d,0x0d,0xfe,0xfe,
};

// FUN_006d7c70 for one mode; `has(x,y)` = the cell has the tested bit.
template <class Has>
int frame_for(int x, int y, Has has) {
    if (!has(x, y)) return 15;
    int m = 0;
    if (!has(x - 1, y - 1)) m |= 0x40;
    if (!has(x, y - 1)) m |= 0x80;
    if (!has(x + 1, y - 1)) m |= 0x01;
    if (!has(x - 1, y)) m |= 0x20;
    if (!has(x + 1, y)) m |= 0x02;
    if (!has(x - 1, y + 1)) m |= 0x10;
    if (!has(x, y + 1)) m |= 0x08;
    if (!has(x + 1, y + 1)) m |= 0x04;
    const uint8_t v = kLut[m];
    return v == 0xFF ? 0 : v == 0xFE ? 15 : v;
}

// One frame as an RGBA darkening texture (alpha = (127 - v)/127; 0xFE skip).
inline SDL_Texture* frame_tex(ra2engine::TextureCache& tc, const ra2assets::ShpFile& shp,
                              const char* tag, int frame, SDL_Rect& r) {
    const ra2assets::ShpFile::Frame f = shp.frame_info(frame);
    r = SDL_Rect{f.x, f.y, f.w, f.h};
    if (f.w <= 0 || f.h <= 0) return nullptr;
    char key[40];
    std::snprintf(key, sizeof key, "%s:%d", tag, frame);
    SDL_Texture* tex = static_cast<SDL_Texture*>(tc.peek(key));
    if (tex) return tex;
    const std::vector<uint8_t> idx = shp.decode_indexed(frame);
    std::vector<uint8_t> rgba(static_cast<std::size_t>(f.w) * f.h * 4, 0);
    for (std::size_t i = 0; i < idx.size(); ++i) {
        const int v = idx[i];
        if (v == 0 || v > 127) continue;
        rgba[i * 4 + 3] = static_cast<uint8_t>((127 - v) * 255 / 127);
    }
    return static_cast<SDL_Texture*>(tc.get_rgba(key, rgba.data(), f.w, f.h));
}

// Fallback when the SHPs are unavailable: a solid 60x30 diamond.
inline SDL_Texture* diamond_tex(ra2engine::TextureCache& tc, bool fog) {
    const char* key = fog ? "shroud:dfog" : "shroud:dsolid";
    SDL_Texture* tex = static_cast<SDL_Texture*>(tc.peek(key));
    if (tex) return tex;
    std::vector<uint8_t> rgba(60 * 30 * 4, 0);
    for (int y = 0; y < 30; ++y) {
        const int half = (y < 15 ? y + 1 : 30 - y) * 2;
        for (int x = 30 - half; x < 30 + half; ++x) rgba[(y * 60 + x) * 4 + 3] = fog ? 123 : 255;
    }
    return static_cast<SDL_Texture*>(tc.get_rgba(key, rgba.data(), 60, 30));
}

}  // namespace pres_shroud

// Draws shroud then (FogOfWar only, W2-GA-03) fog over the view for house 0.
// No-op with shroud off. W2-GA-P1: only the cells that can reach the view are
// visited (cached painter-order rows), not the whole map twice per frame.
void draw_shroud_pass(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2assets::MapFile& map,
                      const ra2engine::World& world, const ra2engine::Camera& cam, int W, int H) {
    if (!world.shroud_enabled()) return;
    auto explored = [&](int x, int y) { return world.explored_at(g_local_house, x, y); };
    auto visible = [&](int x, int y) { return world.visible_at(g_local_house, x, y); };
    // [RE: presentation.md §4, FUN_00480100] FOG.SHP is drawn only when the
    // scenario's FogOfWar flag is set; RA2 skirmish defaults it off.
    const int passes = world.fog_of_war() ? 2 : 1;
    for (int pass = 0; pass < passes; ++pass) {   // 0 shroud, 1 fog (the original's order)
        const bool fog = pass == 1;
        const ra2assets::ShpFile* shp = fog ? g_fog_shp : g_shroud_shp;
        ra2engine::for_each_visible_cell(map, cam, W, H, 2, [&](const ra2assets::MapCell& c, int sx, int sy) {
            if (fog && !explored(c.x, c.y)) return;   // fully shrouded already
            const int fr = fog ? pres_shroud::frame_for(c.x, c.y, visible)
                               : pres_shroud::frame_for(c.x, c.y, explored);
            if (fr == 0) return;
            SDL_Texture* tex = nullptr;
            SDL_Rect dst{sx, sy, 60, 30};
            if (!fog && fr == 15) {
                // Fully covered: a solid diamond grown by 1 px so the outermost map
                // cells' light tile seams never peek past the SHP diamond edge.
                tex = pres_shroud::diamond_tex(tc, false);
                dst = SDL_Rect{sx - 1, sy - 1, 62, 32};
            } else if (shp && shp->count() > fr) {
                SDL_Rect r;
                tex = pres_shroud::frame_tex(tc, *shp, fog ? "fogshp" : "shroudshp", fr, r);
                dst = SDL_Rect{sx + r.x, sy + r.y, r.w, r.h};
            } else if (fr == 15) {
                tex = pres_shroud::diamond_tex(tc, fog);
            }
            if (tex) SDL_RenderCopy(ren, tex, nullptr, &dst);
        });
    }
}

// G-84: an enemy/neutral building is drawn once any foundation cell has been
// explored by house 0 (it then stays visible under fog, as in the original).
inline bool pres_building_seen(const ra2engine::World& world, const ra2engine::Building& b) {
    if (b.house == g_local_house || !world.shroud_enabled()) return true;
    for (int j = 0; j < b.fh; ++j)
        for (int i = 0; i < b.fw; ++i)
            if (world.explored_at(g_local_house, b.cx + i, b.cy + j)) return true;
    return false;
}
