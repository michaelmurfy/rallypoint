// about_page.inc -- W6-06-16: Options > About. Included by main.cpp after
// credits_scene.inc. [Engine page, not in the original.]
//
// Shows the engine version, the non-affiliation disclaimer, the project
// licence and the third-party notices (THIRD_PARTY_NOTICES.md, embedded at
// build time by cmake/ra2_sources.cmake -- stb_image_write's MIT terms ask
// that the notice ship with every copy). The project licence wording is
// user decision U-3: the page carries a placeholder line until then (the
// LICENSE file text is still embedded and shown below it).
#include "ra2_notices.inc"

class AboutScene : public Scene {
public:
    AboutScene(const Rules& rules, bool no_audio, const MenuArt& art) : rules_(rules), no_audio_(no_audio), art_(art) {}
    const char* name() const override { return "about"; }
    void handle(const ra2engine::InputEvent& ev) override {
        if (ev.kind == ra2engine::InputEvent::PointerMove || ev.kind == ra2engine::InputEvent::PointerDown ||
            ev.kind == ra2engine::InputEvent::PointerUp) {
            mx_ = ev.x;
            my_ = ev.y;
        }
        if (ev.button == ra2engine::InputEvent::Left && ev.kind == ra2engine::InputEvent::PointerUp) down_ = false;
        if (ev.kind == ra2engine::InputEvent::Key && ev.key == ra2engine::kKeyEscape) { back_ = true; return; }
        if (ev.kind != ra2engine::InputEvent::PointerDown || ev.button != ra2engine::InputEvent::Left) return;
        down_ = true;
        if (ui_dev(shell_row(3)).contains(ev.x, ev.y)) top_ = std::max(0, top_ - kRows);
        if (ui_dev(shell_row(4)).contains(ev.x, ev.y) && top_ + kRows < lines_n_) top_ += kRows;
        if (ui_dev(shell_row(5)).contains(ev.x, ev.y)) back_ = true;
    }
    void render(ra2engine::Platform& plat, ra2engine::TextureCache& tc) override {
        SDL_Renderer* ren = static_cast<SDL_Renderer*>(plat.renderer());
        const ShellXform xf = ui_xform();
        const ShellPainter p{ren, tc, art_, xf};
        if (lines_.empty()) wrap(p);
        const std::vector<ShellButton> b{
            ShellButton{shell_row(3), "Previous", top_ > 0, ACT_NONE, PAGE_OPTIONS},
            ShellButton{shell_row(4), "Next", top_ + kRows < lines_n_, ACT_NONE, PAGE_OPTIONS},
            ShellButton{shell_row(5), gf_csf("GUI:Back", "Back"), true, ACT_NONE, PAGE_OPTIONS}};
        int lx = 0, ly = 0;
        ui_to_logical(mx_, my_, lx, ly);
        int hov = -1;
        for (int i = 0; i < 3; ++i)
            if (b[(std::size_t)i].enabled && b[(std::size_t)i].rect.contains(lx, ly)) hov = i;
        draw_shell(ren, tc, art_, xf, b, hov, (hov >= 0 && down_) ? hov : -1, "About", "", 0);
        static constexpr ra2engine::SbRect kWell{24, 30, 423, 400};
        p.fill(p.dev(kWell), 0, 0, 0, 190);
        p.frame(p.dev(kWell), true);
        for (int i = 0; i < kRows && top_ + i < lines_n_; ++i) {
            const Line& l = lines_[(std::size_t)(top_ + i)];
            if (l.head) p.header(l.text, kWell.x + 8, kWell.y + 4 + i * kLineH, kWell.w - 16, kLineH, 0);
            else p.label(l.text, kWell.x + 8, kWell.y + 4 + i * kLineH, kWell.w - 16, kLineH, 0);
        }
    }
    Scene* next() override {
        if (!back_) return nullptr;
        back_ = false;
        return new ShellScene(rules_, no_audio_, art_, PAGE_OPTIONS);
    }
    void play_probe(ra2play::Probe& p) override {
        p.scene = "about";
        p.set("about_lines", lines_n_);
        p.set_s("about_version", RA2_VERSION);
        p.set("about_has_sdl", std::strstr(kRa2ThirdPartyNotices, "SDL 2.30.9") ? 1 : 0);
        p.set("about_has_stb", std::strstr(kRa2ThirdPartyNotices, "stb_image_write") ? 1 : 0);
        const ra2engine::SbRect bk = ui_dev(shell_row(5));
        p.targets["back"] = ra2play::Rect{bk.x, bk.y, bk.w, bk.h};
    }

private:
    struct Line { std::string text; bool head = false; };
    static constexpr int kRows = 24, kLineH = 16;
    // The page text, word-wrapped to the well (the 12metfnt widths).
    void wrap(const ShellPainter& p) {
        std::vector<Line> src;
        src.push_back({std::string("RA2 Engine ") + RA2_VERSION, true});
        src.push_back({"", false});
        for (const char* s :
             {"This program is an engine only. It plays Command & Conquer: Red Alert 2 / Yuri's Revenge with the "
              "game files from your own copy of the game; no game data is included or distributed.",
              "\"Command & Conquer\" and \"Red Alert\" are trademarks of Electronic Arts Inc. This project is not "
              "affiliated with, endorsed by or sponsored by Electronic Arts. The art, logos, sounds and text shown "
              "come from your own game files."})
            src.push_back({s, false});
        src.push_back({"", false});
        src.push_back({"Licence", true});
        // U-3: placeholder until the owner picks the licence wording.
        src.push_back({"[Licence text to be confirmed by the project owner (decision U-3).]", false});
        add_text(src, kRa2License);
        src.push_back({"", false});
        add_text(src, kRa2ThirdPartyNotices);
        const int maxw = 423 - 16;
        for (const Line& l : src) {
            if (l.text.empty()) { lines_.push_back(l); continue; }
            std::string cur, word;
            auto flush_word = [&]() {
                if (word.empty()) return;
                const std::string cand = cur.empty() ? word : cur + " " + word;
                if (!cur.empty() && p.text_w(cand, 12.0) > maxw) { lines_.push_back({cur, l.head}); cur = word; }
                else cur = cand;
                word.clear();
            };
            for (char c : l.text) {
                if (c == ' ') flush_word();
                else word += c;
            }
            flush_word();
            if (!cur.empty()) lines_.push_back({cur, l.head});
        }
        lines_n_ = static_cast<int>(lines_.size());
    }
    // Markdown-ish file text -> lines: "# x" / "## x" become headers; code
    // fences dropped; everything else kept line by line.
    static void add_text(std::vector<Line>& out, const char* text) {
        std::string s(text), line;
        auto push = [&](std::string l) {
            while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
            if (l.rfind("```", 0) == 0) return;
            if (l.rfind("## ", 0) == 0) { out.push_back({"", false}); out.push_back({l.substr(3), true}); return; }
            if (l.rfind("# ", 0) == 0) { out.push_back({l.substr(2), true}); return; }
            std::string t;   // drop markdown backticks
            for (char c : l)
                if (c != '`') t += c;
            out.push_back({t, false});
        };
        for (char c : s) {
            if (c == '\n') { push(line); line.clear(); }
            else line += c;
        }
        if (!line.empty()) push(line);
    }
    const Rules& rules_;
    bool no_audio_;
    const MenuArt& art_;
    std::vector<Line> lines_;
    int lines_n_ = 0, top_ = 0;
    bool back_ = false, down_ = false;
    int mx_ = -1, my_ = -1;
};
