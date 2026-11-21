// pres_radar.inc -- G-09 (render half) the radar minimap: an isometric
// picture of the map (the tactical view's own diamond projection) coloured by
// each tile's radar colour, ore/gem tint, map dressing, shroud (black) and fog
// (dimmed), building footprints and unit blips in house colours, and the
// tactical viewport rectangle. Gated by draw_sidebar on an operational radar
// (A: a radar building + power, World::house_low_power). Click handling is B's.
//
// Tile colour [RE-FALLBACK: RE-I4 open -- the binary's colour source]: the
// per-tile radar colours stored in every TMP image header (XCC
// t_tmp_image_header: bytes 43..45 left RGB, 46..48 right RGB), averaged.

long g_radar_online_since = -1000;   // game frame the player's radar came online (Presentation)
long g_radar_offline_since = -1000;  // W2-UI-09: game frame it went offline

class PresRadar {
public:
    void init(Assets& a, const ra2engine::World& w, const OreArt* ore) {
        a_ = &a;
        ore_ = ore;
        // Projection bounds over every map cell.
        int minu = INT_MAX, maxu = INT_MIN, minv = INT_MAX, maxv = INT_MIN;
        for (const ra2assets::MapCell& c : a.map.cells()) {
            minu = std::min(minu, c.x - c.y);
            maxu = std::max(maxu, c.x - c.y);
            minv = std::min(minv, c.x + c.y);
            maxv = std::max(maxv, c.x + c.y);
        }
        if (minu > maxu) return;
        u0_ = minu;
        v0_ = minv;
        uw_ = maxu - minu + 1;
        vh_ = maxv - minv + 1;
        // Per-cell base colour.
        gw_ = w.grid_w();
        gh_ = w.grid_h();
        base_.assign(static_cast<std::size_t>(gw_) * gh_, ra2assets::Rgba{0, 0, 0, 0});
        for (const ra2assets::MapCell& c : a.map.cells()) {
            if (c.x < 0 || c.y < 0 || c.x >= gw_ || c.y >= gh_) continue;
            base_[static_cast<std::size_t>(c.y) * gw_ + c.x] = tile_colour(c);
        }
        ok_ = true;
    }
    bool ok() const { return ok_; }
    // Radar pixel -> map cell, the exact inverse of draw()'s projection (B's
    // radar click uses this so a click lands on the cell drawn under it).
    bool cell_at(int px, int py, int& cx, int& cy) const {
        if (!ok_ || !laid_ || !lr_.contains(px, py) || ls_ <= 0) return false;
        const double u = (px - lx_) / ls_ + u0_, v = (py - ly_) / (0.5 * ls_) + v0_;
        cx = static_cast<int>(std::floor((u + v) / 2.0));
        cy = static_cast<int>(std::floor((v - u) / 2.0));
        cx = std::clamp(cx, 0, std::max(0, gw_ - 1));
        cy = std::clamp(cy, 0, std::max(0, gh_ - 1));
        return true;
    }
    // Radar power-up: radar.shp frames 1..count-2 play once each time the radar
    // comes online at a 4-tick frame timer (W2-UI-09 [RE: construction.md
    // §11a -- frames 1-31, ~8 s]); returns the frame to show, or 0 once open.
    // Called only while online.
    int boot_frame(int count) {
        const long f = 1 + (g_pres_tick - g_radar_online_since) / 4;
        return (count > 2 && f < count - 1) ? static_cast<int>(f) : 0;
    }
    // W2-UI-09: power-down plays the boot frames in reverse (count-2 .. 1),
    // same 4-tick timer; 0 = finished (the side logo, frame 0, shows).
    int close_frame(int count) const {
        if (count <= 2 || g_radar_offline_since <= g_radar_online_since) return 0;
        const long f = (count - 2) - (g_pres_tick - g_radar_offline_since) / 4;
        return f >= 1 ? static_cast<int>(f) : 0;
    }
    ~PresRadar() { if (tex_) SDL_DestroyTexture(tex_); }
    PresRadar() = default;
    PresRadar(const PresRadar&) = delete;
    PresRadar& operator=(const PresRadar&) = delete;
    void set_view(const ra2engine::Camera& cam, int view_w, int view_h) {
        cam_ = cam;
        vw_ = view_w;
        vh_px_ = view_h;
    }

    void draw(SDL_Renderer* ren, const ra2engine::SbRect& r,
              const ra2engine::World& w, const BuildingAssets& ba) {
        if (!ok_) return;
        // Uniform scale: u spans 2 units per cell horizontally, v 1 unit
        // vertically (the 2:1 diamond), letterboxed into the radar box.
        const double s = std::min(r.w / (uw_ * 1.0), r.h / (vh_ * 0.5));
        const int ox = r.x + static_cast<int>((r.w - uw_ * s) / 2);
        const int oy = r.y + static_cast<int>((r.h - vh_ * 0.5 * s) / 2);
        const int TW = std::max(1, static_cast<int>(uw_ * s)), TH = std::max(1, static_cast<int>(vh_ * 0.5 * s));
        lx_ = ox; ly_ = oy; ls_ = s; lr_ = r; laid_ = true;
        // Rebuild the picture every 8 frames (shroud/ore/buildings change slowly).
        if (!tex_ || ++age_ >= 8) {
            age_ = 0;
            std::vector<uint8_t> px(static_cast<std::size_t>(TW) * TH * 4, 0);
            auto plot = [&](int x, int y, ra2assets::Rgba c, double dim) {
                const double u = (x - y - u0_) * s, v = (x + y - v0_) * 0.5 * s;
                const int x0 = static_cast<int>(u), y0 = static_cast<int>(v);
                const int x1 = std::max(x0 + 1, static_cast<int>(u + s)), y1 = std::max(y0 + 1, static_cast<int>(v + 0.5 * s));
                for (int yy = y0; yy < y1 && yy < TH; ++yy)
                    for (int xx = x0; xx < x1 && xx < TW; ++xx) {
                        if (xx < 0 || yy < 0) continue;
                        uint8_t* p = &px[(static_cast<std::size_t>(yy) * TW + xx) * 4];
                        p[0] = static_cast<uint8_t>(c.r * dim);
                        p[1] = static_cast<uint8_t>(c.g * dim);
                        p[2] = static_cast<uint8_t>(c.b * dim);
                        p[3] = 255;
                    }
            };
            for (int y = 0; y < gh_; ++y)
                for (int x = 0; x < gw_; ++x) {
                    ra2assets::Rgba c = base_[static_cast<std::size_t>(y) * gw_ + x];
                    if (c.a == 0) continue;
                    if (!w.explored_at(g_local_house, x, y)) { plot(x, y, ra2assets::Rgba{0, 0, 0, 255}, 1.0); continue; }
                    if (w.ore().has_ore(x, y))
                        c = w.ore().kind_at(x, y) == ra2engine::OreKind::Gems ? ra2assets::Rgba{60, 110, 200, 255}
                                                                                : ra2assets::Rgba{200, 170, 40, 255};
                    plot(x, y, c, (w.fog_of_war() && !w.visible_at(g_local_house, x, y)) ? 0.55 : 1.0);   // W2-GA-03
                }
            if (g_map_dressing)
                for (const auto& o : g_map_dressing->objects())
                    if (w.explored_at(g_local_house, o.x, o.y))
                        for (int j = 0; j < o.fh; ++j)
                            for (int i = 0; i < o.fw; ++i)
                                plot(o.x + i, o.y + j, ra2assets::Rgba{70, 90, 60, 255}, 1.0);
            for (const ra2engine::Building& b : w.buildings()) {
                if (!b.alive || b.state == ra2engine::Building::State::Dead || !pres_building_seen(w, b)) continue;
                const ra2assets::Rgba hc = w.house_palette(ba.unit_pal, b.house)[24];
                for (int j = 0; j < b.fh; ++j)
                    for (int i = 0; i < b.fw; ++i) plot(b.cx + i, b.cy + j, hc, 1.0);
            }
            for (const ra2engine::Unit& u : w.units()) {
                if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
                if (u.house != g_local_house && !w.object_visible_at(g_local_house, u.cell_x, u.cell_y)) continue;
                // W7-05-13 [RE: radar cell draw FUN_00655690 @0x65583F: an object
                // whose type is RadarInvisible (+0x22F) is drawn only for a house
                // allied with the player (FUN_004F98B0)] -- the Night Hawk.
                if (u.an.t.radar_invisible && u.house != g_local_house && !w.are_allied(g_local_house, u.house))
                    continue;
                plot(u.cell_x, u.cell_y, w.house_palette(ba.unit_pal, u.house)[24], 1.0);
            }
            if (tex_ && (tw_ != TW || th_ != TH)) { SDL_DestroyTexture(tex_); tex_ = nullptr; }
            if (!tex_) {
                tex_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, TW, TH);
                tw_ = TW;
                th_ = TH;
            }
            if (tex_) SDL_UpdateTexture(tex_, nullptr, px.data(), TW * 4);
        }
        SDL_Rect dst{ox, oy, TW, TH};
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_Rect bg{r.x, r.y, r.w, r.h};
        SDL_RenderFillRect(ren, &bg);
        if (tex_) SDL_RenderCopy(ren, tex_, nullptr, &dst);
        // Tactical viewport: screen (0..vw, 0..vh) -> cell -> radar.
        auto to_radar = [&](int sx, int sy, int& rx, int& ry) {
            const double px = sx - cam_.x, py = sy - cam_.y;   // projected px
            const double u = px / 30.0, v = py / 15.0;          // (x-y), (x+y)
            rx = ox + static_cast<int>((u - u0_) * s);
            ry = oy + static_cast<int>((v - v0_) * 0.5 * s);
        };
        int x0, y0, x1, y1;
        to_radar(0, 0, x0, y0);
        to_radar(vw_, vh_px_, x1, y1);
        SDL_Rect vr{x0, y0, std::max(2, x1 - x0), std::max(2, y1 - y0)};
        SDL_RenderSetClipRect(ren, &bg);
        SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        SDL_RenderDrawRect(ren, &vr);
        // W3-FX radar events [RE: presentation.md §1.2, FUN_0065f840]: a
        // rotating, colour-cycling square that zooms in from the radar size by
        // RadarEventSpeed px per frame to RadarEventMinRadius, then holds for
        // the type's visibility frames. [RE-FALLBACK: the colour ramp -- a
        // white/yellow pulse at RadarEventColorSpeed.]
        for (const RadarPingDraw& p : g_radar_pings) {
            const double r0 = std::max(r.w, r.h) / 2.0;
            const double zoom = std::max(0.0, (r0 - g_radar_min_r) / std::max(0.01, g_radar_speed));
            if (p.age > zoom + p.vis) continue;
            const double rad = std::max(g_radar_min_r, r0 - g_radar_speed * p.age);
            const int cx = ox + static_cast<int>((p.x - p.y - u0_) * s);
            const int cy = oy + static_cast<int>((p.x + p.y - v0_) * 0.5 * s);
            const double a = g_radar_rot * p.age;
            const double pulse = 0.5 + 0.5 * std::sin(p.age * g_radar_col * 6.2831853);
            SDL_SetRenderDrawColor(ren, 255, 255, static_cast<Uint8>(80 + 175 * pulse), 255);
            int px[5], py[5];
            for (int k = 0; k < 5; ++k) {
                const double t = a + (k % 4) * 1.5707963;
                px[k] = cx + static_cast<int>(std::lround(std::cos(t) * rad));
                py[k] = cy + static_cast<int>(std::lround(std::sin(t) * rad));
            }
            for (int k = 0; k < 4; ++k) SDL_RenderDrawLine(ren, px[k], py[k], px[k + 1], py[k + 1]);
        }
        SDL_RenderSetClipRect(ren, nullptr);
    }

private:
    ra2assets::Rgba tile_colour(const ra2assets::MapCell& c) {
        const ra2assets::TmpFile* tmp = a_->theater.tmp_for_tile(c.tile);
        if (!tmp || c.subtile >= tmp->count()) return ra2assets::Rgba{40, 40, 40, 255};
        const uint8_t* h = tmp->image_header(c.subtile);
        if (!h) return ra2assets::Rgba{40, 40, 40, 255};
        return ra2assets::Rgba{static_cast<uint8_t>((h[43] + h[46]) / 2), static_cast<uint8_t>((h[44] + h[47]) / 2),
                               static_cast<uint8_t>((h[45] + h[48]) / 2), 255};
    }

    Assets* a_ = nullptr;
    const OreArt* ore_ = nullptr;
    int u0_ = 0, v0_ = 0, uw_ = 1, vh_ = 1, gw_ = 0, gh_ = 0;
    std::vector<ra2assets::Rgba> base_;
    bool ok_ = false;
    SDL_Texture* tex_ = nullptr;
    int age_ = 0, tw_ = 0, th_ = 0;
    ra2engine::Camera cam_;
    int vw_ = 1, vh_px_ = 1;
    int lx_ = 0, ly_ = 0;
    double ls_ = 0;
    ra2engine::SbRect lr_{0, 0, 0, 0};
    bool laid_ = false;
};

PresRadar* g_radar = nullptr;   // live game only; draw_radar_minimap delegates to it
