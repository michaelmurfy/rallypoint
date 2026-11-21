// hud_tooltip.inc -- W2-UI-07: in-game tooltips (cameos + sidebar buttons),
// #included INSIDE the GameScene class body (public section).
//
// [RE: wave2/04 #3, FUN_0067F580 StripClass help text] gated on [Options]
// ToolTips; a cameo shows swprintf(TXT_MONEY_FORMAT_2 "%s $%d", UIName,
// Cost_Of(house)) with every space then replaced by a newline; a superweapon
// cameo shows its UIName.
// [ANCHORED: sidebar buttons use the CSF TIP:* / TXT_*_MODE strings
// (TIP:TAB1..4, TIP:SCROLLUP/DOWN, TIP:OPTIONSBUTTON, TIP:DIPLOMACYBUTTON,
// TXT_REPAIR_MODE / TXT_SELL_MODE) and the power bar TXT_POWER_DRAIN.]
// [RE-FALLBACK (Q3): hover delay 0.5 s of game time; box art = hud_text.inc
// draw_tooltip_box.]

    int tt_x_ = -1, tt_y_ = -1;
    double tt_hold_ = 0.0;
    static constexpr double kTooltipDelay = 0.5;

    void tooltip_tick(double dt) {
        if (mx_ == tt_x_ && my_ == tt_y_) {
            tt_hold_ += dt;
        } else {
            tt_x_ = mx_;
            tt_y_ = my_;
            tt_hold_ = 0.0;
        }
    }

    std::string tt_csf(const char* key, const char* fallback) const {
        const std::string t = camp::csf_text(dir_, key);
        return t == key ? std::string(fallback) : t;
    }

    std::string tt_ui_name(const std::string& id) const {
        const std::string lbl = pres_clean(rules_ini_.get(id, "UIName"));
        if (lbl.empty()) return id;
        const std::string t = camp::csf_text(dir_, lbl);
        return t;
    }

    // Raw help text under window pixel (x, y); "" = none.
    std::string tooltip_at(int x, int y) const {
        if (!(sidebar_up_ && ba_.ok && ba_.sidebar_ok && lay_.contains(x, y))) return "";
        for (const ra2engine::CameoCell& c : lay_.cameos) {
            if (!c.rect.contains(x, y)) continue;
            std::string id;
            int cost = 0;
            if (active_tab_ >= 2) {
                if (c.type < 0 || c.type >= static_cast<int>(ur_.defs.size())) return "";
                id = ur_.defs[(size_t)c.type].id;
                cost = ur_.defs[(size_t)c.type].cost;
            } else {
                if (c.type < 0 || c.type >= static_cast<int>(ba_.roster.size())) return "";
                id = ba_.roster[(size_t)c.type].id;
                cost = ba_.roster[(size_t)c.type].cost;
            }
            return cameo_tooltip_text(tt_ui_name(id), cost);
        }
        for (int i = 0; i < 4; ++i)
            if (lay_.tabs[i].contains(x, y)) {
                static const char* keys[4] = {"TIP:TAB1", "TIP:TAB2", "TIP:TAB3", "TIP:TAB4"};
                static const char* fb[4] = {"Structures Tab", "Armory Tab", "Infantry Tab", "Vehicles Tab"};
                return tt_csf(keys[i], fb[i]);
            }
        if (ba_.has_repair_sell && lay_.repair_btn.contains(x, y)) return tt_csf("TXT_REPAIR_MODE", "Repair Mode");
        if (ba_.has_repair_sell && lay_.sell_btn.contains(x, y)) return tt_csf("TXT_SELL_MODE", "Sell Mode");
        if (ba_.has_scroll && lay_.scroll_up.contains(x, y)) return tt_csf("TIP:SCROLLUP", "Up Scroll Arrow");
        if (ba_.has_scroll && lay_.scroll_dn.contains(x, y)) return tt_csf("TIP:SCROLLDOWN", "Down Scroll Arrow");
        if (ba_.has_optbtns) {
            const ra2engine::SbRect ob{lay_.opt_btn.x, lay_.opt_btn.y, ba_.sb_optbtn.width(), ba_.sb_optbtn.height()};
            const ra2engine::SbRect db{lay_.diplo_btn.x, lay_.diplo_btn.y, ba_.sb_diplobtn.width(), ba_.sb_diplobtn.height()};
            if (ob.contains(x, y)) return tt_csf("TIP:OPTIONSBUTTON", "Options Button");
            if (db.contains(x, y)) return tt_csf("TIP:DIPLOMACYBUTTON", "Diplomacy Button");
        }
        if (lay_.power.contains(x, y)) {
            const ra2engine::PowerTotals pt = ra2engine::power_totals(world_.buildings(), ba_.roster, local_house_);
            std::string f = tt_csf("TXT_POWER_DRAIN", "Power = %d \n Drain = %d");
            char buf[160];
            std::snprintf(buf, sizeof buf, f.c_str(), pt.output, pt.drain);
            std::string s = buf, o;
            for (std::size_t i = 0; i < s.size(); ++i) {   // " \n " -> "\n"
                if (s[i] == ' ' && i + 1 < s.size() && s[i + 1] == '\n') continue;
                if (s[i] == ' ' && i > 0 && s[i - 1] == '\n') continue;
                o += s[i];
            }
            return o;
        }
        return "";
    }

    bool tooltips_enabled() const { return !g_user_settings || g_user_settings->tooltips; }

    // The tooltip currently up ("" = none).
    std::string tooltip_now() const {
        if (!tooltips_enabled()) return "";
        if (menu_.open_ || sld_.is_open()) return "";
        // W4-AM-06: a finger has no hover -- a tap must not leave a tooltip
        // behind; on touch the tooltip shows only while a finger HOLDS on the
        // item (past half the hold ring).
        if (!mouse_present_) {
            int hx = 0, hy = 0;
            float pr = 0.0f;
            if (g_platform && g_platform->touch_hold(hx, hy, pr) && pr >= 0.5f) return tooltip_at(hx, hy);
            return "";
        }
        if (tt_hold_ < kTooltipDelay) return "";
        return tooltip_at(mx_, my_);
    }

    void draw_tooltip(SDL_Renderer* ren, ra2engine::TextureCache& tc) const {
        const std::string t = tooltip_now();
        if (t.empty()) return;
        int px = mx_, py = my_;
        float pr = 0.0f;
        if (!mouse_present_ && g_platform && g_platform->touch_hold(px, py, pr)) {
            // W6-01-04 [engine affordance, touch]: the box sits ABOVE and left
            // of the finger (a thumb covers everything below the touch point),
            // at the touch text scale.
            const double sc = hud_touch_scale();
            const int h = static_cast<int>((hud_text_height(t) + 6) * sc);
            draw_tooltip_box(ren, tc, t, px, std::max(0, py - 44 - h), kWinW, kWinH, /*left_of_pointer=*/true, sc);
            return;
        }
        draw_tooltip_box(ren, tc, t, px, py, kWinW, kWinH, /*left_of_pointer=*/lay_.contains(px, py));
    }
