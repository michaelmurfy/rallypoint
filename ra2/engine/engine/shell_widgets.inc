// ============================================================================
// Shared shell control skins (included into main.cpp after draw_shell).
// The Options page (dialog 0xD5) and the Skirmish page (dialog 0x102) draw
// their controls through ONE painter so both pages share the original skins
// [RE: options.md §4, docs/gap/07-skirmish-page.md §1.1/§1.3/§1.4]:
//   * text: 12metfnt, controls yellow (255,255,0), disabled dark red (159,0,0)
//   * control lines red (255,0,0), disabled (159,0,0)
//   * trackbar: trakgrip.pcx thumb, trofl/trofm/trofr.pcx numeric readout well
//   * checkbox: cue_i.pcx (off) / cce_i.pcx (on) 18x18, label at +26
//   * combobox (skin proc 0x5F4BA0): red frame + text + drop arrow; dropped
//     list 7/9 rows. [RE-FALLBACK, audit Q3: the arrow glyph, list frame and
//     scroll strip art were not recovered; drawn as red primitives]
// All geometry is LOGICAL 640x480, mapped through the page's ShellXform.
// ============================================================================

struct ShellPainter {
    SDL_Renderer* ren;
    ra2engine::TextureCache& tc;
    const MenuArt& art;
    const ShellXform& xf;

    SDL_Rect dev(int lx, int ly, int lw, int lh) const { return xf.dev(lx, ly, lw, lh); }
    SDL_Rect dev(const ra2engine::SbRect& r) const { return xf.dev(r.x, r.y, r.w, r.h); }

    void frame(const SDL_Rect& r, bool enabled) const {
        if (enabled) SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
        else         SDL_SetRenderDrawColor(ren, 159, 0, 0, 255);
        SDL_RenderDrawRect(ren, &r);
    }
    void fill(const SDL_Rect& r, Uint8 rr, Uint8 gg, Uint8 bb, Uint8 a = 255) const {
        SDL_SetRenderDrawBlendMode(ren, a == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, rr, gg, bb, a);
        SDL_RenderFillRect(ren, &r);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    }
    // A local.mix PCX (own palette). `key_magenta`: (255,0,255) is transparent
    // (the flag pictures' colour key).
    bool pcx(const char* key, const ra2assets::PcxImage* img, const SDL_Rect& d,
             bool key_magenta = false) const {
        if (!img || img->w <= 0 || img->h <= 0) return false;
        SDL_Texture* t = static_cast<SDL_Texture*>(tc.peek(key));
        if (!t) {
            std::vector<uint8_t> rgba = img->rgba();
            if (key_magenta)
                for (std::size_t i = 0; i + 3 < rgba.size(); i += 4)
                    if (rgba[i] == 255 && rgba[i + 1] == 0 && rgba[i + 2] == 255) rgba[i + 3] = 0;
            t = static_cast<SDL_Texture*>(tc.get_rgba(key, rgba.data(), img->w, img->h));
            if (t && key_magenta) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        }
        if (!t) return false;
        SDL_RenderCopy(ren, t, nullptr, &d);
        return true;
    }
    bool fnt() const { return art.font && art.fnt_ok; }
    double text_w(const std::string& s, double logical_px) const {
        if (!fnt()) return 0.0;
        const double sc = std::max(1.0, std::round(xf.s * logical_px / 16.0));
        return fnt_text_width(*art.font, s) * sc / xf.s;   // in LOGICAL px
    }
    // align: 0 = left, 1 = centre, 2 = right; vertically centred in the rect.
    void text(const std::string& s, int lx, int ly, int lw, int lh, int align,
              const ra2assets::Palette* pal, const char* keypfx, double logical_px) const {
        if (!fnt() || !pal || s.empty()) return;
        const ra2assets::Font& f = *art.font;
        // 12metfnt cell = 16 px; snap to an integer glyph scale (fractional
        // nearest-neighbour scaling visibly mangles the 1-px bevel strokes).
        const double sc = std::max(1.0, std::round(xf.s * logical_px / 16.0));
        const SDL_Rect box = dev(lx, ly, lw, lh);
        const double tw = fnt_text_width(f, s) * sc;
        const double th = f.height() * sc;
        double tx = box.x;
        if (align == 1) tx = box.x + (box.w - tw) / 2.0;
        else if (align == 2) tx = box.x + box.w - tw;
        draw_fnt_text_f(ren, tc, f, *pal, s, tx, box.y + (box.h - th) / 2.0, keypfx, sc);
    }
    // Yellow control text (dark red when disabled).
    void label(const std::string& s, int x, int y, int w, int h, int align, bool en = true) const {
        text(s, x, y, w, h, align, en ? art.font_pal_text : art.font_pal_disabled,
             en ? "opy:" : "opd:", 12.0);
    }
    // Gold section header (the shell font ramp).
    void header(const std::string& s, int x, int y, int w, int h, int align = 0) const {
        text(s, x, y, w, h, align, art.font_pal, "oph:", 14.0);
    }
    // Clip `s` so it fits `w` logical px (appends nothing: the original
    // combobox simply clips).
    std::string fit(const std::string& s, int w) const {
        if (!fnt() || text_w(s, 12.0) <= w) return s;
        std::string t = s;
        while (!t.empty() && text_w(t, 12.0) > w) t.pop_back();
        return t;
    }

    // Trackbar (skin proc 0x5FB270). `pos` relative to the range minimum.
    void trackbar(const ra2engine::SbRect& r, int pos, int range, bool readout,
                  const std::string& readout_text, bool en) const {
        const int ro = readout ? ra2engine::kTrackbarReadoutW : 0;
        if (readout) {   // readout well: trofm stretched, trofl/trofr caps
            const int rx = r.x + r.w - ro + 1;
            pcx("opt:trofm", art.trofm, dev(rx, r.y - 1, ro, 24));
            pcx("opt:trofl", art.trofl, dev(rx, r.y - 1, 8, 24));
            pcx("opt:trofr", art.trofr, dev(rx + ro - 10, r.y - 1, 10, 24));
        }
        const int tx = ra2engine::trackbar_thumb_x(pos, range, r.w, ro);
        if (!pcx("opt:trakgrip", art.trakgrip, dev(r.x + tx, r.y, ra2engine::kTrackbarThumbW, r.h))) {
            SDL_SetRenderDrawColor(ren, en ? 200 : 110, 0, 0, 255);
            const SDL_Rect t = dev(r.x + tx, r.y, ra2engine::kTrackbarThumbW, r.h);
            SDL_RenderFillRect(ren, &t);
        }
        frame(dev(r.x, r.y, r.w - ro, r.h), en);              // groove
        if (readout) {
            frame(dev(r.x + r.w - ro + 2, r.y, ro - 2, r.h), en);
            label(readout_text, r.x + r.w - 49, r.y, 49, r.h, 1, en);
        }
        if (!en) {   // EnableWindow(FALSE): veil
            const SDL_Rect v = dev(r.x, r.y, r.w, r.h);
            fill(v, 0, 0, 0, 110);
        }
    }

    // Checkbox (skin proc 0x5F3CF0): lamp at (x, y-1), label at +26.
    void checkbox(const ra2engine::SbRect& r, bool on, const std::string& lbl, bool en) const {
        const SDL_Rect icon = dev(r.x, r.y - 1, 18, 18);
        const bool drawn = on ? pcx("opt:chk_on", art.chk_on, icon)
                              : pcx("opt:chk_off", art.chk_off, icon);
        if (!drawn) {
            frame(icon, en);
            if (on) {
                SDL_SetRenderDrawColor(ren, 255, 160, 0, 255);
                const SDL_Rect dot = dev(r.x + 5, r.y + 4, 8, 8);
                SDL_RenderFillRect(ren, &dot);
            }
        }
        label(lbl, r.x + 26, r.y, r.w - 26, r.h, 0, en);
    }

    // The drop arrow: a solid red down-pointing triangle at the right edge.
    void arrow(const ra2engine::SbRect& r, bool en) const {
        const SDL_Rect a = dev(r.x + r.w - 15, r.y + r.h / 2 - 3, 10, 6);
        SDL_SetRenderDrawColor(ren, en ? 255 : 159, 0, 0, 255);
        for (int k = 0; k < a.h; ++k) {
            const int inset = a.w * k / (2 * std::max(1, a.h));
            SDL_RenderDrawLine(ren, a.x + inset, a.y + k, a.x + a.w - 1 - inset, a.y + k);
        }
    }
    // Combobox closed face: frame, text, optional colour swatch, drop arrow.
    void combo(const ra2engine::SbRect& r, const std::string& s, bool en, bool hot = false,
               const SDL_Color* swatch = nullptr) const {
        if (hot && en) fill(dev(r), 60, 0, 0, 255);
        frame(dev(r), en);
        int tx = r.x + 4;
        if (swatch) {
            const SDL_Rect sw = dev(r.x + 3, r.y + 4, 5, r.h - 8);
            fill(sw, swatch->r, swatch->g, swatch->b);
            tx += 6;
        }
        label(fit(s, r.w - 17 - (tx - r.x)), tx, r.y, r.w - 16 - (tx - r.x), r.h, 0, en);
        arrow(r, en);
    }
    // Edit box (EM_LIMITTEXT): frame + text + blinking caret when focused.
    void edit(const ra2engine::SbRect& r, const std::string& s, bool focused, bool caret_on) const {
        if (focused) fill(dev(r), 40, 0, 0, 255);
        frame(dev(r), true);
        label(fit(s, r.w - 10), r.x + 4, r.y, r.w - 8, r.h, 0, true);
        if (focused && caret_on) {
            const double w = std::min<double>(r.w - 6, text_w(fit(s, r.w - 10), 12.0));
            const SDL_Rect c = dev(r.x + 4 + static_cast<int>(std::lround(w)) + 1, r.y + 3, 1, r.h - 6);
            fill(c, 255, 255, 0);
        }
    }
};

// One dropped list (combobox list or listbox): items, the first visible row,
// the selection and the row height, all LOGICAL. The scroll strip (when the
// list overflows) sits inside the right edge: its top half scrolls up, the
// bottom half down (no wheel events reach scenes).
struct ShellList {
    ra2engine::SbRect r;              // whole list rect (items + strip)
    std::vector<std::string> items;
    std::vector<int> data;            // per-item value
    std::vector<SDL_Color> swatch;    // optional per-item swatch (colour combo)
    int top = 0, sel = -1, visible = 7, item_h = 16;
    static constexpr int kStripW = 12;
    int strip_w = kStripW;            // W9-04-06: the MP pages widen it on touch

    bool scrolls() const { return static_cast<int>(items.size()) > visible; }
    int rows() const { return std::min(visible, static_cast<int>(items.size())); }
    ra2engine::SbRect item_rect(int i) const {   // i = item index (must be visible)
        return {r.x + 1, r.y + 1 + (i - top) * item_h, r.w - 2 - (scrolls() ? strip_w : 0), item_h};
    }
    ra2engine::SbRect strip_up() const { return {r.x + r.w - strip_w - 1, r.y + 1, strip_w, r.h / 2 - 1}; }
    ra2engine::SbRect strip_down() const {
        return {r.x + r.w - strip_w - 1, r.y + r.h / 2, strip_w, r.h - r.h / 2 - 1};
    }
    void clamp_top() {
        const int n = static_cast<int>(items.size());
        top = std::max(0, std::min(top, n - visible));
    }
    void scroll(int d) { top += d; clamp_top(); }
    void reveal(int i) {
        if (i < 0) return;
        if (i < top) top = i;
        if (i >= top + visible) top = i - visible + 1;
        clamp_top();
    }
    // Item index under a logical point, -1 none.
    int hit(int lx, int ly) const {
        for (int i = top; i < top + rows(); ++i)
            if (item_rect(i).contains(lx, ly)) return i;
        return -1;
    }
    void draw(const ShellPainter& p, bool fill_black = true) const {
        const SDL_Rect d = p.dev(r);
        if (fill_black) p.fill(d, 0, 0, 0);
        for (int i = top; i < top + rows(); ++i) {
            const ra2engine::SbRect ir = item_rect(i);
            if (i == sel) p.fill(p.dev(ir), 128, 0, 0);
            int tx = ir.x + 3;
            if (i < static_cast<int>(swatch.size()) && swatch[(size_t)i].a) {
                const SDL_Color& c = swatch[(size_t)i];
                p.fill(p.dev(ir.x + 3, ir.y + 3, 10, ir.h - 6), c.r, c.g, c.b);
                tx += 13;
            }
            p.label(p.fit(items[(size_t)i], ir.w - (tx - ir.x) - 2), tx, ir.y, ir.w - (tx - ir.x), ir.h, 0);
        }
        if (scrolls()) {
            const ra2engine::SbRect u = strip_up(), dn = strip_down();
            p.frame(p.dev(u), true);
            p.frame(p.dev(dn), true);
            // up / down triangles
            SDL_SetRenderDrawColor(p.ren, 255, 0, 0, 255);
            const SDL_Rect a = p.dev(u.x + 2, u.y + u.h / 2 - 3, u.w - 4, 6);
            const SDL_Rect b = p.dev(dn.x + 2, dn.y + dn.h / 2 - 3, dn.w - 4, 6);
            for (int k = 0; k < a.h; ++k) {
                const int in = a.w * (a.h - 1 - k) / (2 * std::max(1, a.h));
                SDL_RenderDrawLine(p.ren, a.x + in, a.y + k, a.x + a.w - 1 - in, a.y + k);
            }
            for (int k = 0; k < b.h; ++k) {
                const int in = b.w * k / (2 * std::max(1, b.h));
                SDL_RenderDrawLine(p.ren, b.x + in, b.y + k, b.x + b.w - 1 - in, b.y + k);
            }
        }
        p.frame(d, true);
    }
};

// Logical x inside a trackbar's track that selects `pos` (the inverse of
// trackbar_pos_from_x, found by scanning so the harness clicks land exactly).
inline int shell_trackbar_x_for(int pos, int range, int client_w, int readout_w) {
    int first = -1, last = -1;
    for (int x = 0; x < client_w - readout_w; ++x)
        if (ra2engine::trackbar_pos_from_x(x, range, client_w, readout_w) == pos) {
            if (first < 0) first = x;
            last = x;
        }
    return first < 0 ? 0 : (first + last) / 2;
}
