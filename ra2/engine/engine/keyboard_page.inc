// keyboard_page.inc -- W5-E (R-140): the Options "Keyboard" page, dialog 0xA3
// (DLGPROC 0x005D97A0, created by FUN_005DA360), drawn as a full-screen shell
// page for both the main-menu Options page and the in-game Game Controls page.
// Included by ingame_pages.inc (needs ShellPainter / ShellList / draw_shell).
//
// [RE: options.md §6] header GUI:KeyboardOptions ("Customize Keyboard");
// Category combobox 0x4C7 (12,81,207,..); Commands listbox 0x4C8
// (231,79,219,178); Description group 0x6E9 (12,105,207,154); "Current Hotkey"
// edit 0x4CD (13,344,186,22); Assign 0x4CF (231,344,124,24); Reset All 0x4D0;
// "Currently assigned to"; Back 0x686 (row 5). The command list, its CSF
// labels and categories come from the CommandClass vtables (hotkeys.cpp
// hot_command_list). Bindings persist in Keyboard.ini [Hotkey]
// <Command>=<VK code> (+256 Shift, +512 Ctrl, +1024 Alt) -- written to the
// writable pref dir (the install is read-only), read back over the install's
// file by load_hotkey_table().
// [RE-FALLBACK: Reset All sits in button row 0; the "Current Hotkey" /
// "Currently assigned to" label positions (13,322) / (231,300) and the
// category list's dropped height were not read from the dialog template.]

ra2engine::HotkeyTable load_hotkey_table();
bool save_hotkey_table(const ra2engine::HotkeyTable& t);
// Bumped whenever the page saves, so a running GameScene reloads its table.
int g_hotkeys_version = 0;

class KeyboardPage {
public:
    enum Result { None, Back };
    bool open_ = false;

    void open() {
        open_ = true;
        table_ = load_hotkey_table();
        cat_ = 0;
        cat_open_ = false;
        capture_ = false;
        pending_ = 0;
        rebuild();
    }
    bool is_open() const { return open_; }

    Result handle(const ra2engine::InputEvent& ev) {
        using IE = ra2engine::InputEvent;
        if (!open_) return None;
        if (ev.kind == IE::PointerMove || ev.kind == IE::PointerDown || ev.kind == IE::PointerUp) {
            mx_ = ev.x;
            my_ = ev.y;
        }
        if (ev.button == IE::Left && ev.kind == IE::PointerUp) down_ = false;
        if (ev.kind == IE::Key) {
            // The hotkey edit captures the next real key (with its modifiers).
            if (capture_ && ev.vk > 0 && ev.vk != 16 && ev.vk != 17 && ev.vk != 18) {
                pending_ = ra2engine::hotkey_code(ev.vk, ev.shift, ev.ctrl, ev.alt);
                capture_ = false;
                return None;
            }
            if (ev.key == ra2engine::kKeyEscape) return back();
            return None;
        }
        if (ev.kind == IE::Quit) return back();
        if (ev.kind != IE::PointerDown || ev.button != IE::Left) return None;
        down_ = true;
        int lx = 0, ly = 0;
        ui_to_logical(ev.x, ev.y, lx, ly);
        if (cat_open_) {
            const int h = cats_.hit(lx, ly);
            if (h >= 0) { cat_ = h; rebuild(); }
            cat_open_ = false;
            return None;
        }
        for (const ShellButton& b : buttons()) {
            if (!b.enabled || !b.rect.contains(lx, ly)) continue;
            if (b.action == 1) return back();
            if (b.action == 2) {   // Reset All: the shipped / install bindings
                table_ = ra2engine::HotkeyTable::defaults();
                reset_ = true;
                pending_ = 0;
                return None;
            }
        }
        if (kCatBox.contains(lx, ly)) { cat_open_ = true; return None; }
        if (cmds_.scrolls() && cmds_.strip_up().contains(lx, ly)) { cmds_.scroll(-1); return None; }
        if (cmds_.scrolls() && cmds_.strip_down().contains(lx, ly)) { cmds_.scroll(+1); return None; }
        const int hit = cmds_.hit(lx, ly);
        if (hit >= 0) {
            cmds_.sel = hit;
            pending_ = current_code();
            return None;
        }
        if (kEdit.contains(lx, ly)) { capture_ = true; return None; }
        if (kAssign.contains(lx, ly) && cmds_.sel >= 0 && pending_ > 0) assign();
        return None;
    }

    void render(SDL_Renderer* ren, ra2engine::TextureCache& tc) const {
        if (!open_) return;
        const MenuArt& art = shell_art();
        const ShellXform xf = ui_xform();
        const ShellPainter p{ren, tc, art, xf};
        int lx = 0, ly = 0;
        ui_to_logical(mx_, my_, lx, ly);
        const std::vector<ShellButton> btns = buttons();
        int hover = -1;
        for (int i = 0; i < static_cast<int>(btns.size()); ++i)
            if (btns[(size_t)i].enabled && btns[(size_t)i].rect.contains(lx, ly)) hover = i;
        draw_shell(ren, tc, art, xf, btns, hover, (down_ && hover >= 0) ? hover : -1,
                   gf_csf("GUI:KeyboardOptions", "Customize Keyboard"), "", -1);
        std::string status;
        p.label(gf_csf("GUI:Category", "Category"), 12, 60, 207, 18, 0);
        p.combo(kCatBox, cat_label(cat_), true, kCatBox.contains(lx, ly));
        p.label(gf_csf("GUI:Commands", "Commands"), 231, 58, 219, 18, 0);
        cmds_.draw(p);
        p.frame(p.dev(cmds_.r), true);
        // Description group.
        p.header(gf_csf("GUI:Description", "Description"), 12, 109, 207, 16, 0);
        p.frame(p.dev(kDesc), true);
        if (cmds_.sel >= 0 && cmds_.sel < static_cast<int>(rows_.size())) {
            const ra2engine::HotCommandInfo& c = ra2engine::hot_command_list()[rows_[(size_t)cmds_.sel]];
            const std::vector<std::string> lines = shell_wrap(p, gf_csf(c.desc_csf, c.fallback.c_str()), kDesc.w - 8);
            for (std::size_t i = 0; i < lines.size() && i < 8; ++i)
                p.label(lines[i], kDesc.x + 4, kDesc.y + 20 + static_cast<int>(i) * 16, kDesc.w - 8, 16, 0);
        }
        // Current hotkey edit + "Currently assigned to".
        p.label(gf_csf("GUI:CurrentHotkey", "Current Hotkey"), 13, 322, 186, 18, 0);
        p.edit(kEdit, capture_ ? std::string() : ra2engine::hotkey_label(pending_), capture_, (SDL_GetTicks() / 400) % 2 == 0);
        const ra2engine::HotBinding owner = table_.command_at(pending_);
        if (pending_ > 0 && owner.cmd != ra2engine::HotCmd::None) {
            p.label(gf_csf("GUI:CurrentlyAssigned", "Currently assigned to:"), 231, 290, 219, 18, 0);
            p.label(p.fit(owner_label(owner), 219), 231, 308, 219, 18, 0);
        }
        shell_small_button(p, kAssign, gf_csf("GUI:Assign", "Assign"), kAssign.contains(lx, ly),
                           down_ && kAssign.contains(lx, ly), cmds_.sel >= 0 && pending_ > 0);
        if (cat_open_) {
            p.fill(p.dev(cats_.r), 0, 0, 0);
            cats_.draw(p);
            p.frame(p.dev(cats_.r), true);
        }
        if (hover >= 0) status = gf_csf(btns[(size_t)hover].action == 1 ? "STT:KeyboardButtonBack"
                                                                        : "STT:KeyboardButtonResetAll", "");
        if (kAssign.contains(lx, ly)) status = gf_csf("STT:KeyboardButtonAssign", "");
        if (cmds_.r.contains(lx, ly)) status = gf_csf("STT:KeyboardListCommands", "");
        if (kCatBox.contains(lx, ly)) status = gf_csf("STT:KeyboardComboCategory", "");
        shell_status_line(p, status);
    }

    // G0 --play surface: kb:cat, kb:cat:<i>, kb:cmd:<NAME>, kb:edit, kb:assign,
    // kb:reset, kb:back; kb_cmd / kb_code / kb_category / kb_rows.
    void play_probe(ra2play::Probe& p) const {
        p.set("kb_open", open_ ? 1 : 0);
        if (!open_) return;
        auto put = [&](const std::string& k, const ra2engine::SbRect& lr) {
            const ra2engine::SbRect r = ui_dev(lr);
            p.targets[k] = ra2play::Rect{r.x, r.y, r.w, r.h};
        };
        put("kb:cat", kCatBox);
        if (cat_open_)
            for (int i = cats_.top; i < cats_.top + cats_.rows(); ++i) put("kb:cat:" + std::to_string(i), cats_.item_rect(i));
        for (int i = cmds_.top; i < cmds_.top + cmds_.rows(); ++i)
            put("kb:cmd:" + ra2engine::hot_command_list()[rows_[(size_t)i]].name, cmds_.item_rect(i));
        put("kb:list:down", cmds_.strip_down());
        put("kb:edit", kEdit);
        put("kb:assign", kAssign);
        for (const ShellButton& b : buttons()) put(b.action == 1 ? "kb:back" : "kb:reset", b.rect);
        p.set("kb_rows", static_cast<int>(rows_.size()));
        p.set("kb_category", cat_);
        p.set("kb_capture", capture_ ? 1 : 0);
        p.set("kb_code", pending_);
        p.set_s("kb_cmd", cmds_.sel >= 0 ? ra2engine::hot_command_list()[rows_[(size_t)cmds_.sel]].name : "");
        for (const ra2engine::HotCommandInfo& c : ra2engine::hot_command_list())
            p.set("kb_bind(" + c.name + ")", table_.code_of(c.binding.cmd, c.binding.arg));
    }

private:
    static constexpr ra2engine::SbRect kCatBox{12, 81, 207, 22};
    static constexpr ra2engine::SbRect kDesc{12, 105, 207, 154};
    static constexpr ra2engine::SbRect kEdit{13, 344, 186, 22};
    static constexpr ra2engine::SbRect kAssign{231, 344, 124, 24};

    // Category 0 = All, then the binary's categories in first-use order.
    std::vector<std::string> category_keys() const {
        std::vector<std::string> v{""};
        for (const ra2engine::HotCommandInfo& c : ra2engine::hot_command_list())
            if (std::find(v.begin(), v.end(), c.cat_csf) == v.end()) v.push_back(c.cat_csf);
        return v;
    }
    static std::string cat_name(const std::string& k) {
        if (k.empty()) return gf_csf("GUI:All", "All");
        static const std::pair<const char*, const char*> fb[] = {
            {"TXT_INTERFACE", "Interface"}, {"TXT_SELECTION", "Selection"}, {"TXT_CONTROL", "Control"},
            {"TXT_TEAM", "Team"}, {"TXT_TAUNT", "Taunts"}};
        for (const auto& e : fb)
            if (k == e.first) return gf_csf(k.c_str(), e.second);
        return gf_csf(k.c_str(), k.c_str());
    }
    std::string cat_label(int i) const {
        const std::vector<std::string> ks = category_keys();
        return i >= 0 && i < static_cast<int>(ks.size()) ? cat_name(ks[(size_t)i]) : std::string();
    }
    static std::string row_label(const ra2engine::HotCommandInfo& c) {
        std::string s = gf_csf(c.ui_csf, c.fallback.c_str());
        // The team / view rows share one CSF label: append the number.
        const std::size_t us = c.name.find_last_of("_");
        if (c.binding.arg > 0 && s.find(std::to_string(c.binding.arg)) == std::string::npos &&
            (us != std::string::npos || c.name.rfind("View", 0) == 0 || c.name.rfind("SetView", 0) == 0))
            s += " " + std::to_string(c.binding.arg);
        return s;
    }
    std::string owner_label(const ra2engine::HotBinding& b) const {
        for (const ra2engine::HotCommandInfo& c : ra2engine::hot_command_list())
            if (c.binding.cmd == b.cmd && c.binding.arg == b.arg) return row_label(c);
        return std::string();
    }
    void rebuild() {
        const std::vector<std::string> ks = category_keys();
        cats_ = ShellList{{12, 103, 207, static_cast<int>(ks.size()) * 16 + 2}, {}, {}, {}, 0, cat_,
                          static_cast<int>(ks.size()), 16};
        for (const std::string& k : ks) cats_.items.push_back(cat_name(k));
        rows_.clear();
        cmds_ = ShellList{{231, 79, 219, 178}, {}, {}, {}, 0, -1, 11, 16};
        const auto& all = ra2engine::hot_command_list();
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (cat_ > 0 && all[i].cat_csf != ks[(size_t)cat_]) continue;
            rows_.push_back(i);
            cmds_.items.push_back(row_label(all[i]));
        }
        pending_ = 0;
    }
    int current_code() const {
        if (cmds_.sel < 0 || cmds_.sel >= static_cast<int>(rows_.size())) return 0;
        const ra2engine::HotCommandInfo& c = ra2engine::hot_command_list()[rows_[(size_t)cmds_.sel]];
        return std::max(0, table_.code_of(c.binding.cmd, c.binding.arg));
    }
    void assign() {
        const ra2engine::HotCommandInfo& c = ra2engine::hot_command_list()[rows_[(size_t)cmds_.sel]];
        table_.assign(c.binding, pending_);
        dirty_ = true;
    }
    Result back() {
        if (dirty_ || reset_) {
            if (save_hotkey_table(table_)) ++g_hotkeys_version;
            dirty_ = reset_ = false;
        }
        open_ = false;
        return Back;
    }
    std::vector<ShellButton> buttons() const {
        return {ShellButton{shell_row(0), gf_csf("GUI:ResetAll", "Reset All"), true, 2, PAGE_MAIN},
                ShellButton{shell_row(5), gf_csf("GUI:Back", "Back"), true, 1, PAGE_MAIN}};
    }

    ra2engine::HotkeyTable table_;
    ShellList cats_{{12, 103, 207, 98}, {}, {}, {}, 0, 0, 6, 16};
    ShellList cmds_{{231, 79, 219, 178}, {}, {}, {}, 0, -1, 11, 16};
    std::vector<std::size_t> rows_;
    int cat_ = 0;
    bool cat_open_ = false, capture_ = false, dirty_ = false, reset_ = false, down_ = false;
    int pending_ = 0;
    int mx_ = -1, my_ = -1;
};
