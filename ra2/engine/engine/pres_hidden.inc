// pres_hidden.inc -- W5-E2 (R-146): "See Hidden Objects" (RA2.INI [Options]
// ShowHidden). Included by main.cpp before class GameScene.
//
// [RE: TechnoClass::AI FUN_006F9380 @6F97BC -> FUN_0070E600; AnimClass::AI
// FUN_00423AE0 @423B96; Options +0x1F = DAT_00A8DAE7 ShowHidden]:
//   * an object whose type has CanBeHidden (TechnoType +0x724) and that is not
//     an aircraft, standing on a cell flagged as hidden behind a structure
//     (CellClass +0x100, FUN_00487D10), gets the [AudioVisual] Behind= anim
//     (rulesmd: BEHIND -- Rate=200, LoopStart=1 LoopEnd=18, Layer=top) at its
//     coords;
//   * that anim is invisible unless ShowHidden is on (both sites re-test the
//     option, so toggling it applies at once).
// [RE-FALLBACK: the cell's +0x100 "behind a structure" flag is not traced --
// the clone tests whether a structure drawn after the object covers the
// object's draw point with an opaque pixel of its current image; the
// CanBeHidden default (no stock section sets the key) is taken as yes; only
// objects the player can see are marked.]

struct HiddenMarkers {
    std::map<int, std::vector<uint8_t>> mask;   // building type -> frame-0 index pixels
    long marked = 0;                            // --play evidence: markers drawn last frame

    // Is the canvas point (sx, sy) on an opaque pixel of building b's image?
    bool covers(const ra2engine::Building& b, const BuildingAssets& ba, const ra2engine::World& w,
                const ra2engine::Camera& cam, int sx, int sy) {
        if (b.type < 0 || b.type >= static_cast<int>(ba.art.size())) return false;
        const BuildingArt& art = ba.art[static_cast<std::size_t>(b.type)];
        if (!art.ok || art.invisible || art.shp.count() <= 0) return false;
        const ra2assets::MapCell* mc = w.map().at(b.cx, b.cy);
        const ra2engine::ScreenPt dp = ra2engine::building_draw_point(b.cx, b.cy, mc ? mc->level : 0);
        const int fcx = dp.x + cam.x, fcy = dp.y + cam.y;
        const ra2assets::ShpFile::Frame f = art.shp.frame_info(0);
        const int x0 = fcx - art.shp.width() / 2 + f.x, y0 = fcy - art.shp.height() / 2 + f.y;
        if (sx < x0 || sy < y0 || sx >= x0 + f.w || sy >= y0 + f.h) return false;
        auto it = mask.find(b.type);
        if (it == mask.end()) it = mask.emplace(b.type, art.shp.decode_indexed(0)).first;
        const std::size_t ix = static_cast<std::size_t>(sy - y0) * f.w + (sx - x0);
        return ix < it->second.size() && it->second[ix] != 0;
    }

    void draw(SDL_Renderer* ren, ra2engine::TextureCache& tc, const ra2engine::World& w, const BuildingAssets& ba,
              const ra2engine::Camera& cam, int W, int H, bool show_hidden) {
        marked = 0;
        if (!g_fx || !ba.ok) return;
        const FxAnimType* t = g_fx->type(behind_name());
        if (!t || !t->shp) return;
        const int delay = std::max(1, t->delay);
        const int lo = std::max(0, t->loop_start), hi = std::max(lo, std::min(t->loop_end, t->shp->count() - 1));
        const int frame = lo + static_cast<int>((g_pres_tick / delay) % (hi - lo + 1));
        for (const ra2engine::Unit& u : w.units()) {
            if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || u.is_aircraft) continue;
            if (!can_be_hidden(u.image)) continue;
            if (u.house != g_local_house && !w.object_visible_at(g_local_house, u.cell_x, u.cell_y)) continue;
            // W5-B: infantry stand at sub-cell offsets (fx*256 + sub_x leptons).
            const double ux = u.fx + u.sub_x / 256.0, uy = u.fy + u.sub_y / 256.0;
            const int sx = static_cast<int>((ux - uy) * 30.0) + cam.x + 30;
            const int sy = static_cast<int>((ux + uy) * 15.0) + cam.y + 15 - u.level * 15;
            if (sx < 0 || sy < 0 || sx >= W || sy >= H) continue;
            const int depth = u.cell_x + u.cell_y;
            bool hidden = false;
            for (const ra2engine::Building& b : w.buildings()) {
                if (!b.alive || b.state == ra2engine::Building::State::Dead) continue;
                if (b.cx + b.fw - 1 + b.cy + b.fh - 1 <= depth) continue;   // drawn before the unit
                if (u.cell_x >= b.cx && u.cell_x < b.cx + b.fw && u.cell_y >= b.cy && u.cell_y < b.cy + b.fh)
                    continue;   // standing on the foundation (a garrison / gate)
                if (covers(b, ba, w, cam, sx, sy)) { hidden = true; break; }
            }
            if (!hidden) continue;
            ++marked;
            if (show_hidden)   // [RE: @70E69B / @423B96 -- invisible unless ShowHidden]
                g_fx->blit(ren, tc, *t->shp, "behind", frame, sx, sy, 255, W, H);
        }
    }

    const Ini* rules = nullptr;
    std::string behind_;
    std::map<std::string, bool> cbh_;
    const std::string& behind_name() {
        if (behind_.empty() && rules) behind_ = pres_clean(rules->get("AudioVisual", "Behind"));
        if (behind_.empty()) behind_ = "BEHIND";
        return behind_;
    }
    bool can_be_hidden(const std::string& image) {
        auto it = cbh_.find(image);
        if (it != cbh_.end()) return it->second;
        bool v = true;
        if (rules && g_fx) {
            const std::string id = g_fx3 ? g_fx3->unit_id(image) : image;
            const std::string s = pres_lower(pres_clean(rules->get(id, "CanBeHidden")));
            if (!s.empty()) v = s[0] == 'y' || s[0] == 't' || s[0] == '1';
        }
        return cbh_.emplace(image, v).first->second;
    }
};
