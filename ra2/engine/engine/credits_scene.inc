// credits_scene.inc -- W2-UI-26: Movies & Credits > View Credits. Included by
// main.cpp after ShellScene (it returns to the Movies & Credits page).
//
// [RE: wave2/04 RE #8 -- local.mix/credits.txt (15,350 B) is the credits
// text; "{LABEL}" runs are CSF labels (CRD:* lines, CRD_TTL:* section titles)
// FUN_004B37C0 reads CREDITS.TXT.]
// [RE-FALLBACK: scroll speed 30 logical px/s, centred lines over the static
// MNSCRNS screen in 12metfnt (titles in the gold header ramp, names in the
// control yellow); any click / Esc / the end returns to page 0x101; the
// CREDITS theme is not played (menu audio carries only the shell theme).]

class CreditsScene : public Scene {
public:
    // W6-06-07: `to_main` = the end-of-campaign roll (returns to the main
    // menu, not to Movies & Credits).
    CreditsScene(const Rules& rules, bool no_audio, const MenuArt& art, bool to_main = false)
        : rules_(rules), no_audio_(no_audio), art_(art), to_main_(to_main) {
        try {
            const std::string dir = env_install_dir();
            ra2assets::MixFile ra2 = ra2assets::MixFile::open_path(dir + "ra2.mix");
            ra2assets::MixFile local = ra2.open_nested("local.mix");
            if (const ra2assets::MixEntry* e = local.find(std::string("credits.txt"))) {
                const auto raw = local.read(*e);
                std::string cur;
                auto flush = [&]() {
                    while (!cur.empty() && (cur.back() == '\r' || cur.back() == ' ')) cur.pop_back();
                    std::size_t a = cur.find_first_not_of(' ');
                    std::string t = a == std::string::npos ? std::string() : cur.substr(a);
                    bool title = t.find("{CRD_TTL:") != std::string::npos;
                    std::string out;
                    for (std::size_t i = 0; i < t.size(); ++i) {   // expand {CSF labels}
                        if (t[i] == '{') {
                            const std::size_t j = t.find('}', i);
                            if (j != std::string::npos) {
                                const std::string lbl = t.substr(i + 1, j - i - 1);
                                out += gf_csf(lbl, lbl.c_str());
                                i = j;
                                continue;
                            }
                        }
                        out += t[i];
                    }
                    lines_.push_back({out, title});
                    cur.clear();
                };
                for (uint8_t c : raw) {
                    if (c == '\n') flush();
                    else cur += static_cast<char>(c);
                }
                if (!cur.empty()) flush();
            }
        } catch (const std::exception& ex) {
            std::cerr << "[credits] credits.txt unavailable: " << ex.what() << "\n";
        }
    }
    const char* name() const override { return "credits"; }
    void handle(const ra2engine::InputEvent& ev) override {
        if ((ev.kind == ra2engine::InputEvent::Key && ev.key == ra2engine::kKeyEscape) ||
            (ev.kind == ra2engine::InputEvent::PointerDown && ev.button == ra2engine::InputEvent::Left))
            done_ = true;
    }
    void update(double dt) override {
        scroll_ += dt * kSpeed;
        if (scroll_ > 448 + static_cast<double>(lines_.size()) * kLineH) done_ = true;
    }
    void render(ra2engine::Platform& plat, ra2engine::TextureCache& tc) override {
        SDL_Renderer* ren = static_cast<SDL_Renderer*>(plat.renderer());
        const ShellXform xf = ui_xform();
        draw_shell(ren, tc, art_, xf, {}, -1, -1, gf_csf("GUI:ViewCredits", "View Credits"), "", -1);
        const ShellPainter p{ren, tc, art_, xf};
        const SDL_Rect clip = p.dev(0, 0, 472, 448);
        SDL_RenderSetClipRect(ren, &clip);
        double y = 448 - scroll_;
        for (const Line& l : lines_) {
            if (y > -kLineH && y < 448 && !l.text.empty()) {
                if (l.title) p.header(l.text, 0, static_cast<int>(y), 472, kLineH, 1);
                else p.label(l.text, 0, static_cast<int>(y), 472, kLineH, 1);
            }
            y += kLineH;
        }
        SDL_RenderSetClipRect(ren, nullptr);
    }
    Scene* next() override {
        if (!done_) return nullptr;
        done_ = false;
        if (to_main_) return new MainMenuScene(rules_, no_audio_, art_);
        return new ShellScene(rules_, no_audio_, art_, PAGE_MOVIES);
    }
    void play_probe(ra2play::Probe& p) override {
        p.scene = "credits";
        p.set("credits_to_main", to_main_ ? 1 : 0);
        p.set("credit_lines", static_cast<int>(lines_.size()));
        p.set("credit_scroll", static_cast<int>(scroll_));
    }

private:
    struct Line { std::string text; bool title = false; };
    static constexpr double kSpeed = 30.0;
    static constexpr int kLineH = 18;
    const Rules& rules_;
    bool no_audio_;
    const MenuArt& art_;
    std::vector<Line> lines_;
    double scroll_ = 0.0;
    bool done_ = false;
    bool to_main_ = false;
};

Scene* make_campaign_credits_scene(bool no_audio) {
    if (g_menu_rules && g_menu_art) return new CreditsScene(*g_menu_rules, no_audio, *g_menu_art, /*to_main=*/true);
    return make_main_menu_scene(no_audio);
}
