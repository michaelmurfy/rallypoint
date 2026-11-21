// shell_pages.inc -- W2-UI-13..17/23: helpers shared by every shell page
// (main-menu pages and the in-game dialogs drawn AS shell pages). Included by
// main.cpp right after shell_widgets.inc (needs MenuArt / ShellXform /
// ShellPainter / draw_shell).
//
// [RE: wave2/04 "Exact layout specs" -- all geometry is the 640x480 logical
// canvas; buttons on the §7.2 grid x=484, 156x42, row y = 199 + 42k; status
// line 0x695 at (15, 458, 454, 19); popups 0x120/0x121 are 451x326 centred
// at (95, 77), text static (60, 65, 330, 81), MNBTTN 124x24 buttons at
// (310, 219 / 251 / 284) (RE #6).]

// The shell art for scenes that are not handed one (GameScene's in-game
// pages). run_menu / --menu-shot publish theirs in g_menu_art; otherwise the
// first call loads a private copy (no audio) -- defined after load_menu_art.
const MenuArt* g_menu_art = nullptr;
const MenuArt& shell_art();

// W2-AR-06: centred in the CURRENT canvas (1280 on the shell, the wider game
// canvas for the in-game pages on a 20:9 panel).
inline ShellXform ui_xform() { return ShellXform::make(g_ui_w, g_ui_h); }
inline ra2engine::SbRect ui_dev(const ra2engine::SbRect& r) { return ui_xform().hit(r); }
inline void ui_to_logical(int dx, int dy, int& lx, int& ly) {
    const ShellXform xf = ui_xform();
    lx = static_cast<int>(std::floor((dx - xf.ox) / xf.s));
    ly = static_cast<int>(std::floor((dy - xf.oy) / xf.s));
}
inline ra2engine::SbRect shell_row(int k) { return {484, 199 + 42 * k, 156, 42}; }

// Status line 0x695 (the STT help text of the control under the pointer).
inline void shell_status_line(const ShellPainter& p, const std::string& s) {
    if (!s.empty()) p.label(s, 15, 458, 454, 19, 0);
}

// MNBTTN.SHP small button under MAINBTTN.PAL, stretched into `r` (logical):
// frame 0 normal, 2 hover, 1 pressed [wave2/04 W2-UI-17]; label centred.
inline void shell_small_button(const ShellPainter& p, const ra2engine::SbRect& r, const std::string& label,
                               bool hover, bool pressed, bool en = true) {
    const MenuArt& art = p.art;
    const ra2assets::Palette* pal = art.mainbttn_pal_ok ? &art.mainbttn_pal
                                    : art.shell_pal_ok  ? &art.shell_pal : art.pal;
    int frame = (en && pressed) ? 1 : (en && hover) ? 2 : 0;
    bool drawn = false;
    if (art.btn && pal && art.btn->count() > 0) {
        if (frame >= art.btn->count()) frame = 0;
        if (SDL_Texture* t = shp_frame_tex(p.tc, "shell:mnbtn" + std::to_string(frame), *art.btn, frame, *pal)) {
            SDL_SetTextureColorMod(t, en ? 255 : 120, en ? 255 : 120, en ? 255 : 130);
            const SDL_Rect d = p.dev(r);
            SDL_RenderCopy(p.ren, t, nullptr, &d);
            SDL_SetTextureColorMod(t, 255, 255, 255);
            drawn = true;
        }
    }
    if (!drawn) {
        p.fill(p.dev(r), 60, 0, 0);
        p.frame(p.dev(r), en);
    }
    p.label(label, r.x, r.y, r.w, r.h, 1, en);
}

// Word-wrap `text` to `w` logical px in the control face (12 px).
inline std::vector<std::string> shell_wrap(const ShellPainter& p, const std::string& text, int w) {
    std::vector<std::string> out;
    std::string line, word;
    auto flush_word = [&]() {
        if (word.empty()) return;
        const std::string cand = line.empty() ? word : line + " " + word;
        if (!line.empty() && p.text_w(cand, 12.0) > w) { out.push_back(line); line = word; }
        else line = cand;
        word.clear();
    };
    for (char c : text) {
        if (c == '\n') { flush_word(); out.push_back(line); line.clear(); }
        else if (c == ' ') flush_word();
        else word += c;
    }
    flush_word();
    if (!line.empty()) out.push_back(line);
    return out;
}

// --- popup dialog 0x120 / 0x121 (W2-UI-17) -----------------------------------
inline ra2engine::SbRect popup_box() { return {95, 77, 451, 326}; }
// Button i of n (the LAST is Cancel at y 284; then 251; a third at 219).
inline ra2engine::SbRect popup_button(int i, int n) {
    static const int ys[3] = {219, 251, 284};
    const int k = 3 - n + i;
    return {95 + 310, 77 + ys[std::max(0, std::min(2, k))], 124, 24};
}
// Paint a popup over whatever is on screen: pudlgbg? under dialog.pal (side
// keyed: Allied pudlgbga / Soviet pudlgbgs), wrapped text in the static,
// MNBTTN buttons. hover / pressed = button indices or -1.
// (Declared ahead in gf_start.inc, so GfConfirm can draw itself as a popup.)
void popup_render(SDL_Renderer* ren, ra2engine::TextureCache& tc, const std::string& text,
                  const std::vector<std::string>& buttons, int hover, int pressed, bool soviet) {
    const MenuArt& art = shell_art();
    const ShellXform xf = ui_xform();
    const ShellPainter p{ren, tc, art, xf};
    const ra2engine::SbRect b = popup_box();
    const ra2assets::ShpFile* bg = (soviet && art.pudlg_s) ? art.pudlg_s : art.pudlg;
    bool drawn = false;
    if (bg && art.dialog_pal && bg->count() > 0) {
        if (SDL_Texture* t = shp_frame_tex(tc, soviet && art.pudlg_s ? "shell:pudlgs" : "shell:pudlga",
                                           *bg, 0, *art.dialog_pal)) {
            const SDL_Rect d = p.dev(b);
            SDL_RenderCopy(ren, t, nullptr, &d);
            drawn = true;
        }
    }
    if (!drawn) {
        p.fill(p.dev(b), 18, 20, 26);
        p.frame(p.dev(b), true);
    }
    int y = b.y + 65;
    for (const std::string& ln : shell_wrap(p, text, 330)) {
        p.label(ln, b.x + 60, y, 330, 16, 0);
        y += 16;
    }
    const int n = static_cast<int>(buttons.size());
    for (int i = 0; i < n; ++i)
        shell_small_button(p, popup_button(i, n), buttons[(std::size_t)i], i == hover, i == pressed);
}

// Device rect of popup button i of n (GfConfirm's hit-test).
ra2engine::SbRect popup_button_dev(int i, int n) { return ui_dev(popup_button(i, n)); }

// W2-UI-22: control 0x71D = "%s %s" of GUI:Version and the build [RE: wave2/04
// Q7; the build string is the clone's 1.006].
inline std::string shell_version_text() {
    std::string v = gf_csf("GUI:Version", "Version");
    while (!v.empty() && v.back() == ' ') v.pop_back();
    return v + " 1.006";
}
