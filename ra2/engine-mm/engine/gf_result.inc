// gF: the skirmish result screen [RE: docs/re/mcv-deploy.md §5]. The original
// shows TXT_VICTORIOUS / TXT_LOST during BorrowedTime and then its MP score
// screen before returning to the shell; the score screen is not recovered, so
// this page carries the outcome, the game time and the kill/loss tallies
// [RE-FALLBACK: score screen layout], then MAIN MENU or REMATCH (same lobby
// config). Included after campaign_scenes.inc (needs CampButton + GameScene).
// W2-UI-20: drawn as the MP score page 0xBC6 [RE: wave2/04 W2-UI-20 --
// header GUI:SkirmishScore (MPScore.cpp FUN_005AAAE0 picks GUI:SkirmishScore
// vs GUI:MultiplayerScore), columns Player (16,173) Kills (118,173) Losses
// (181,173) Built (256,173) Score (331,173), Continue in the button column].
// [RE-FALLBACK: row pitch 20; the outcome line (TXT_VICTORIOUS / TXT_LOST)
// and the game time sit above the table.] W5-E: the Score column is the
// original's (MPScore.cpp FUN_005C92C0 -- see GfSkirmishStats::Row) and the
// rows are sorted by it, highest first.
// W9-03-16: Continue from a network match's score page goes back to the
// multiplayer page the match came from (Network Lobby / Internet) [SUSPECTED:
// the original returns to its lobby page].
Scene* gf_make_mp_return_scene(bool no_audio, bool lan) {
    if (!g_menu_rules || !g_menu_art) return nullptr;
    if (lan) return new MpLanScene(*g_menu_rules, no_audio, *g_menu_art);
    return new MpOnlineScene(*g_menu_rules, no_audio, *g_menu_art);
}

class GfSkirmishResultScene : public Scene {
public:
    GfSkirmishResultScene(bool no_audio, ra2engine::SkirmishConfig cfg, int outcome, long frames,
                          GfSkirmishStats st)
        : no_audio_(no_audio), cfg_(std::move(cfg)), outcome_(outcome), frames_(frames), st_(std::move(st)) {
        head_ = gf_csf(outcome_ > 0 ? "TXT_VICTORIOUS" : "TXT_LOST", outcome_ > 0 ? "You Are Victorious!" : "Game Over");
    }
    std::string header() const {
        return st_.multiplayer ? gf_csf("GUI:MultiplayerScore", "Multiplayer Score")
                       : gf_csf("GUI:SkirmishScore", "Skirmish Score");
    }
    void handle(const ra2engine::InputEvent& ev) override {
        if (ev.kind == ra2engine::InputEvent::PointerMove || ev.kind == ra2engine::InputEvent::PointerDown ||
            ev.kind == ra2engine::InputEvent::PointerUp) {
            mx_ = ev.x;
            my_ = ev.y;
        }
        if (ev.button == ra2engine::InputEvent::Left && ev.kind == ra2engine::InputEvent::PointerUp) down_ = false;
        if (ev.kind == ra2engine::InputEvent::Key && (ev.key == ra2engine::kKeyEnter || ev.key == ra2engine::kKeyEscape))
            choice_ = 2;
        if (ev.kind != ra2engine::InputEvent::PointerDown || ev.button != ra2engine::InputEvent::Left) return;
        down_ = true;
        if (ui_dev(shell_row(0)).contains(ev.x, ev.y)) choice_ = 2;
    }
    void render(ra2engine::Platform& plat, ra2engine::TextureCache& tc) override {
        SDL_Renderer* ren = static_cast<SDL_Renderer*>(plat.renderer());
        const MenuArt& art = shell_art();
        const ShellXform xf = ui_xform();
        const ShellPainter p{ren, tc, art, xf};
        const std::vector<ShellButton> b{ShellButton{shell_row(0), gf_csf("GUI:Continue", "Continue"), true, ACT_NONE, PAGE_MAIN}};
        const bool hov = ui_dev(shell_row(0)).contains(mx_, my_);
        draw_shell(ren, tc, art, xf, b, hov ? 0 : -1, (hov && down_) ? 0 : -1, header(), "", 0);
        p.header(head_, 16, 60, 440, 30, 1);
        char t[64];
        std::snprintf(t, sizeof t, "%s %02ld:%02ld:%02ld", gf_csf("GUI:Time", "Time").c_str(), frames_ / 15 / 3600,
                      (frames_ / 15 / 60) % 60, (frames_ / 15) % 60);
        if (st_.multiplayer) {   // 0x108: game number 0x6D2 (left) + time 0x3EA (right) on one line
            p.label(gf_csf("GUI:GameNumber", "Game Number") + ": " + std::to_string(st_.game_number), 16, 110, 200, 20, 0);
            p.label(t, 256, 110, 200, 20, 2);
        } else {
            p.label(t, 16, 110, 440, 20, 1);
        }
        static const int cx[5] = {16, 118, 181, 256, 331};
        static const char* ck[5] = {"GUI:Player", "GUI:Kills", "GUI:Losses", "GUI:Built", "GUI:Score"};
        static const char* cf[5] = {"Player", "Kills", "Losses", "Built", "Score"};
        for (int c = 0; c < 5; ++c) p.header(gf_csf(ck[c], cf[c]), cx[c], 173, 100, 20, 0);
        int y = 199;
        for (const GfSkirmishStats::Row& r : st_.rows) {
            if (!r.listed) continue;
            p.label(p.fit(r.name, 100), cx[0], y, 100, 20, 0);
            p.label(std::to_string(r.kills), cx[1], y, 60, 20, 0);
            p.label(std::to_string(r.losses), cx[2], y, 60, 20, 0);
            p.label(std::to_string(r.built), cx[3], y, 60, 20, 0);
            p.label(std::to_string(r.score), cx[4], y, 80, 20, 0);   // W5-E [RE: FUN_005C92C0]
            y += 20;
        }
        std::string st;
        if (hov) st = gf_csf("STT:MPScoreButtonContinue", "");
        shell_status_line(p, st);
    }
    Scene* next() override {
        const int c = choice_;
        choice_ = 0;
        if (c == 1) return defer_game_scene(no_audio_, cfg_);   // W2-AR-10
        if (c == 2) {
            if (st_.multiplayer)
                if (Scene* m = gf_make_mp_return_scene(no_audio_, st_.lan)) return m;
            if (Scene* m = make_main_menu_scene(no_audio_)) return m;
            quit_ = true;
        }
        return nullptr;
    }
    bool wants_quit() const override { return quit_; }
    // G0 --play: the skirmish result page is the session's verdict.
    void play_probe(ra2play::Probe& p) override {
        p.scene = "result";
        p.set_s("outcome", outcome_ > 0 ? "WON" : "LOST");
        if (st_.multiplayer) {   // W9-03-16 (network matches only: skirmish play_hash unchanged)
            p.set_s("score_header", header());
            p.set("score_net", 1);
            p.set("score_game", st_.game_number);
            for (const GfSkirmishStats::Row& r : st_.rows)
                if (r.house >= 0 && r.listed) p.set_s("score_name(" + std::to_string(r.house) + ")", r.name);
        }
        p.set("result_frames", static_cast<double>(frames_));
        p.set("score_rows", static_cast<int>(st_.rows.size()));
        for (const GfSkirmishStats::Row& r : st_.rows)
            if (r.house == st_.local_house) {   // the player's row (the rows are score-sorted)
                p.set("score_kills0", r.kills);
                p.set("score_losses0", r.losses);
            }
        for (const GfSkirmishStats::Row& r : st_.rows)   // W5-E: per house, sorted by score
            if (r.house >= 0) {
                p.set("score(" + std::to_string(r.house) + ")", static_cast<double>(r.score));
                p.set("score_kills(" + std::to_string(r.house) + ")", r.kills);
            }
        if (!st_.rows.empty()) p.set("score_top_house", st_.rows[0].house);
        const ra2engine::SbRect m = ui_dev(shell_row(0));
        p.targets["menu"] = ra2play::Rect{m.x, m.y, m.w, m.h};
        p.targets["continue"] = ra2play::Rect{m.x, m.y, m.w, m.h};
    }
    // Test seam: the choice a click would make (1 rematch, 2 menu).
    void choose(int c) { choice_ = c; }

private:
    bool no_audio_;
    ra2engine::SkirmishConfig cfg_;
    int outcome_;
    long frames_;
    GfSkirmishStats st_;
    std::string head_;
    int choice_ = 0;
    bool quit_ = false, down_ = false;
    int mx_ = 0, my_ = 0;
};

Scene* gf_make_skirmish_result_scene(bool no_audio, const ra2engine::SkirmishConfig& cfg, int outcome,
                                     long frames, const GfSkirmishStats& stats) {
    return new GfSkirmishResultScene(no_audio, cfg, outcome, frames, stats);
}

Scene* gf_make_restart_scene(bool no_audio, const ra2engine::SkirmishConfig& cfg) {
    if (!cfg.campaign_map.empty()) return new CampaignLoadScene(no_audio, cfg);
    return defer_game_scene(no_audio, cfg);   // W2-AR-10: built after the old game is gone
}
