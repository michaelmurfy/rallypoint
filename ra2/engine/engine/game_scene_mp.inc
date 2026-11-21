// W9-05: the in-match multiplayer layer of a network game -- in-game
// diplomacy (one-way alliances, war, locked alliances), the observer after
// defeat, Leave / Observe, chat channels, beacons and player names.
// #included INSIDE the GameScene class body (public section), after
// game_scene_net.inc. Everything that changes the simulation goes through a
// command (ALLY 0x03, DESTRUCT 0x08) so every peer applies it on the same
// frame; chat and beacons are out of band on the global channel, as in the
// original (they never touch the sync).
// [RE: docs/gap/wave9/05-ingame-mp-robustness.md W9-05-03..11; Ghidra logs in
//  D:\ra2-re\w9mp-05-out (d1/d2/m1/m2/x1).]

    // ---- player names (W9-05-10) --------------------------------------------
    // [RE: TXT_PLAYER_DEFEATED / TXT_HAS_ALLIED use the house's UIName
    // (+0x1602A), the player's nickname in a network game, FUN_005C92C0.]
    // "" outside a network match (the callers keep their country fallback).
    std::string mp_name(int h) const { return net_ ? net_->name_of(h) : std::string(); }
    bool mp_human(int h) const { return world_.is_human(h) && !world_.ai_enabled(h); }

    // ---- diplomacy (W9-05-03) -------------------------------------------------
    // The mode's [MultiplayerDialogSettings] AlliesAllowed / AllyChangeAllowed
    // [RE: Rules+0x14B5 / +0x14BB, loader FUN_006718E0; the mode's rules
    // override from mpmodesmd.ini is read over rulesmd.ini at game start]:
    // Battle MPBattleMD.ini AlliesAllowed=yes; Team Game MPTeamMD.ini
    // AllyChangeAllowed=no (locked alliances); Free For All / Cooperative
    // AlliesAllowed=no (data checked in this install's ra2md.mix).
    static bool mp_ini_flag(const std::string& v, bool fallback) {
        if (v.empty()) return fallback;
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(v[0])));
        return c == 'y' || c == 't' || c == '1';
    }
    // The rules-override INI of mpmodesmd.ini mode `mode` ("" = none).
    std::string mp_mode_rules_file(int mode) const {
        if (mode <= 0) return "";
        const ra2assets::Ini modes = load_ini_from_ra2md(dir_, "mpmodesmd.ini");
        const std::string key = std::to_string(mode);
        for (const std::string& sec : modes.section_names())
            for (const auto& kv : modes.section(sec)) {
                if (kv.first != key) continue;
                // "GUI:Battle, STT:ModeBattle, MPBattleMD.ini, standard, true"
                std::stringstream ss(kv.second);
                std::string tok;
                for (int i = 0; i < 3 && std::getline(ss, tok, ','); ++i) {}
                const auto a = tok.find_first_not_of(" \t"), b = tok.find_last_not_of(" \t");
                return a == std::string::npos ? std::string() : tok.substr(a, b - a + 1);
            }
        return "";
    }
    void mp_setup_diplomacy() {
        bool allies = mp_ini_flag(rules_ini_.get("MultiplayerDialogSettings", "AlliesAllowed"), false);
        bool change = mp_ini_flag(rules_ini_.get("MultiplayerDialogSettings", "AllyChangeAllowed"), true);
        const std::string ov = mp_mode_rules_file(cfg_.mp_mode);
        if (!ov.empty()) {
            const ra2assets::Ini o = load_ini_from_ra2md(dir_, ov);
            allies = mp_ini_flag(o.get("MultiplayerDialogSettings", "AlliesAllowed"), allies);
            change = mp_ini_flag(o.get("MultiplayerDialogSettings", "AllyChangeAllowed"), change);
        }
        world_.set_diplomacy_rules(allies, change);
        // W9-05-05: a defeated human of a network match observes with the map
        // revealed (sim-side, armed identically on every peer before frame 0).
        if (g_pending_netplay) world_.enable_observer_reveal(true);
        chat_listen_.assign(8, 1);   // the Diplomacy "Chat" column: every listener ticked
    }

    // The scene's half of an ALLY event (CommandHost::diplomacy, every peer):
    // the message line and the EVA [RE: MakeAlly FUN_004F9990 @0x4F9BB3:
    // TXT_HAS_ALLIED (a, b) in a's colour for MessageDelay; announce: one-way
    // -> a == player EVA_RequestingAlliance / b == player EVA_AllianceRequested
    // (@0x4F9CE9..0x4F9D50); mutual -> no EVA for an observer, else
    // EVA_AllianceFormed when the player allies both, EVA_EnemyAllianceFormed
    // otherwise (@0x4F9C69..0x4F9CE2). MakeEnemy FUN_004F9DB0 @0x4F9F44:
    // TXT_AT_WAR (a, b); announce when the alliance was mutual and the player
    // is a or b -> EVA_AllianceBroken]. Only a human a speaks (+0x1EC).
    int mp_dip_events_ = 0;
    std::string mp_dip_last_;
    void mp_on_diplomacy(int a, int b, ra2engine::World::AllyChange ch) {
        using AC = ra2engine::World::AllyChange;
        ++mp_dip_events_;
        const bool ally = ch == AC::Requested || ch == AC::Formed;
        const std::string an = net_ ? mp_name(a) : "Player " + std::to_string(a + 1);
        const std::string bn = net_ ? mp_name(b) : "Player " + std::to_string(b + 1);
        std::string t = camp::csf_text(dir_, ally ? "TXT_HAS_ALLIED" : "TXT_AT_WAR");
        if (t.empty() || t == (ally ? "TXT_HAS_ALLIED" : "TXT_AT_WAR")) t = ally ? "%s has allied with %s" : "%s declares war on %s";
        for (const std::string* s : {&an, &bn}) {
            const std::size_t p = t.find("%s");
            if (p != std::string::npos) t.replace(p, 2, *s);
        }
        mp_dip_last_ = t;
        if (mp_human(a)) post_message(t, -1.0, house_text_color(a));
        pres_log(0, "mp diplomacy " + std::to_string(a) + (ally ? " allies " : " war ") + std::to_string(b) +
                        (ch == AC::Formed ? " mutual" : ch == AC::BrokenMutual ? " mutual-broken" : ""));
        std::cerr << "[net] diplomacy: " << t << " (frame " << sim_frame_ << ")\n";
        if (!mp_human(a)) return;
        const int me = local_house_;
        if (ch == AC::Requested) {
            if (a == me) gf_eva("EVA_RequestingAlliance");
            else if (b == me) gf_eva("EVA_AllianceRequested");
        } else if (ch == AC::Formed) {
            if (!observer_)
                gf_eva(world_.allied_with(me, a) && world_.allied_with(me, b) ? "EVA_AllianceFormed"
                                                                             : "EVA_EnemyAllianceFormed");
        } else if (ch == AC::BrokenMutual && (a == me || b == me)) {
            gf_eva("EVA_AllianceBroken");
        }
    }

    // ToggleAlliance [RE: FUN_007309C0]: a network game, the player not
    // defeated, the target another human, AlliesAllowed + AllyChangeAllowed;
    // then EVENT_ALLY (HouseID = target) goes into the OutList.
    bool mp_request_alliance(int target) {
        if (!net_ || observer_) return false;
        if (!world_.alliance_toggle_allowed(local_house_, target)) return false;
        ra2engine::Command c;
        c.type = ra2engine::EventType::Ally;
        c.value = target;
        issue(std::move(c));
        return true;
    }
    // The hotkey: the owner of the first selected object (CurrentObjects[0]).
    void hotkey_toggle_alliance() {
        int owner = -1;
        const int u = input_.selection().lowest();
        if (u >= 0 && u < static_cast<int>(world_.units().size())) owner = world_.units()[static_cast<std::size_t>(u)].house;
        const int sb = input_.selected_building();
        if (owner < 0 && sb >= 0 && sb < static_cast<int>(world_.buildings().size()))
            owner = world_.buildings()[static_cast<std::size_t>(sb)].house;
        if (owner >= 0 && owner != local_house_) mp_request_alliance(owner);
    }

    // ---- observer (W9-05-05) --------------------------------------------------
    // [RE: MPlayer_Defeated FUN_004FBED0, the player branch: PlayerPtr stays
    // in the game -- DAT_00A8A4A0 (observer) = 1, FUN_005779D0 reveals the
    // map, FUN_00656830(1) forces the radar on, every chat listener is ticked
    // (0xA8C070 = 0x01010101 x2); the score screen comes only at the whole
    // game's end.] The map reveal is the World's observer_reveal (every peer).
    bool observer_ = false;
    void mp_enter_observer() {
        if (observer_) return;
        observer_ = true;
        g_observer_radar = true;
        chat_listen_.assign(8, 1);
        input_.selection().clear();
        beacon_placing_ = false;
        std::cerr << "[net] observer: house " << local_house_ << " defeated at frame " << sim_frame_
                  << ", watching on\n";
        pres_log(0, "mp observer");
    }

    // ---- Leave / Observe (W9-05-06) -------------------------------------------
    // [RE: FUN_0048C7C0 case 2 (the non-WOL abort): TXT_OK / TXT_CANCEL
    // confirm FUN_005C5AF0, then DESTRUCT (alive) + EXIT FUN_00646C40; case 3
    // (dialog 0xB6 FUN_004F1750): 3 = Leave -> DESTRUCT + EXIT, 4 = Observe
    // -> DESTRUCT only. Observe is offered while the player is alive and at
    // least two humans are alive (STT:ConfirmExitButtonObserve).]
    bool mp_leave_confirm_ = false;
    bool mp_alive() const {
        return !world_.house_defeated(local_house_) && world_.house_outcome(local_house_) == 0 && !observer_;
    }
    bool mp_can_observe() const {
        if (!net_ || !mp_alive()) return false;
        int humans = 0;
        for (const ra2mp::MpPlayer& p : net_->session.players)
            if (!p.is_ai && !world_.house_defeated(p.house) &&
                std::find(net_left_.begin(), net_left_.end(), p.house) == net_left_.end())
                ++humans;
        return humans >= 2;
    }
    void mp_ask_leave() {
        mp_leave_confirm_ = true;
        gf_abort_.ask(gf_csf("GUI:AskAbortMission", "What would you like to do?"),
                      {gf_csf("TXT_OK", "OK"), gf_csf("TXT_CANCEL", "Cancel")});
    }
    void mp_leave_answer(int c) {
        if (c < 0) return;
        mp_leave_confirm_ = false;
        if (c == 0) mp_leave();
    }
    void mp_destruct() {
        ra2engine::Command c;
        c.type = ra2engine::EventType::Destruct;
        issue(std::move(c));
        std::cerr << "[net] DESTRUCT queued at frame " << sim_frame_ << "\n";
    }
    void mp_leave() {
        // "AI takes over" (clone option) keeps the house for the computer.
        if (net_ && mp_alive() && !net_->ai_takeover) mp_destruct();
        menu_.open_ = false;
        if (!next_scene_) {   // net_filter_next sends EXIT
            // W9-03-16 (w9fix-menus): the leaver sees the multiplayer score page too.
            if (gf_outcome_ == 0) gf_outcome_ = -1;
            next_scene_ = gf_make_skirmish_result_scene(no_audio_, cfg_, gf_outcome_, gf_frames_, gf_stats());
        }
    }
    void mp_observe() {
        menu_.open_ = false;
        if (mp_can_observe()) mp_destruct();
    }

    // ---- chat channels (W9-05-07) ---------------------------------------------
    // [RE: FUN_0055E050: Enter = the Diplomacy "Chat" listeners (mode 1),
    // Backspace = allies (mode 2, PlayerPtr->IsAlliedWith), '\' = everyone
    // (mode 3); the edit prompt TXT_FROM "From %s:" (own name); an observer
    // sending to listeners / allies that leave a player out gets
    // TXT_OBSERVER_CANT_SEND and nothing is sent; each recipient gets a
    // DATA_ACK copy. Received lines last MessageDelay x 900 ticks.]
    std::vector<char> chat_listen_ = std::vector<char>(8, 1);
    int chat_mode_ = 1;
    int chat_sent_ = 0, chat_blocked_ = 0;
    std::vector<int> mp_chat_last_to_;
    bool mp_chat_to(int h, int mode) const {
        if (mode == 3) return true;
        if (mode == 2) return world_.allied_with(local_house_, h);
        return h >= 0 && h < static_cast<int>(chat_listen_.size()) && chat_listen_[static_cast<std::size_t>(h)] != 0;
    }
    std::vector<int> mp_chat_recipients(int mode, bool& blocked) const {
        std::vector<int> to;
        blocked = false;
        if (!net_) return to;
        for (const ra2mp::MpPlayer& p : net_->session.players) {
            if (p.is_ai || p.house == local_house_) continue;
            if (mp_chat_to(p.house, mode)) to.push_back(p.house);
            else if (observer_) blocked = true;
        }
        return to;
    }
    std::string mp_chat_prompt() const {
        if (beacon_editing_) return gf_csf("TXT_BEACON_MESSAGE", "Beacon Message:");
        std::string t = camp::csf_text(dir_, "TXT_FROM");
        if (t.empty() || t == "TXT_FROM") t = "From %s:";
        const std::size_t p = t.find("%s");
        if (p != std::string::npos) t.replace(p, 2, net_ ? net_->name_of(local_house_) : std::string("Player"));
        return t;
    }
    void mp_open_chat(int mode) {
        chat_mode_ = mode;
        net_chat_open_ = true;
        net_chat_ime();   // W9-04-12: the soft keyboard at the chat line (starts text input)
    }
    // Diplomacy page "Chat" column: toggle a listener.
    void mp_toggle_listener(int h) {
        if (h < 0 || h >= static_cast<int>(chat_listen_.size()) || h == local_house_) return;
        chat_listen_[static_cast<std::size_t>(h)] = chat_listen_[static_cast<std::size_t>(h)] ? 0 : 1;
    }

    // ---- beacons (W9-05-08, YR) -----------------------------------------------
    // [RE: PlaceBeacon FUN_00536DD0 -> FUN_00730B00 (not as an observer):
    // the beacon cursor; BeaconManager 0x89B328 = 8 houses x 3 slots, each
    // 0x114 bytes (coords, flags, 128-char text, owner); FUN_00430B20 place
    // (slot -1 = first free; all 3 used -> refused), the placer hears
    // EVA_BeaconPlaced + TXT_BEACON_HELP1/2 and sends global 0x20; a received
    // beacon of a MUTUAL ally that is not defeated raises a radar event
    // (FUN_0065F4B0) and EVA_BeaconDetected; Delete FUN_00730AE0 ->
    // FUN_00431140 removes the selected own beacon (global 0x21); Enter on a
    // selected beacon edits its text (TXT_BEACON_MESSAGE, FUN_004313D0, global
    // 0x22).] Scene-side, never in the sync (the original keeps them out of
    // the EventClass stream too).
    struct MpBeacon { bool used = false; int cx = 0, cy = 0; std::string text; };
    MpBeacon beacons_[8][3];
    bool beacon_placing_ = false, beacon_editing_ = false;
    int beacon_sel_ = -1;   // own slot selected (Delete / Enter)
    int beacon_events_ = 0;
    static bool mp_bh(int h) { return h >= 0 && h < 8; }
    int beacon_count(int h) const {
        int n = 0;
        if (mp_bh(h))
            for (const MpBeacon& b : beacons_[h]) n += b.used ? 1 : 0;
        return n;
    }
    // FUN_004F9830 + FUN_004F9870: mutual allies (or our own), owner alive.
    bool beacon_visible(int owner) const {
        if (owner == local_house_) return true;
        return world_.allied_with(local_house_, owner) && world_.allied_with(owner, local_house_) &&
               !world_.house_defeated(owner);
    }
    int beacon_slot_at(int h, int cx, int cy) const {
        if (!mp_bh(h)) return -1;
        for (int s = 0; s < 3; ++s)
            if (beacons_[h][s].used && beacons_[h][s].cx == cx && beacons_[h][s].cy == cy) return s;
        return -1;
    }
    // Place a beacon of house h (slot -1 = first free). False = refused.
    bool beacon_place(int h, int slot, int cx, int cy, bool local) {
        if (!mp_bh(h)) return false;
        if (slot < 0) {
            for (int s = 0; s < 3 && slot < 0; ++s)
                if (!beacons_[h][s].used) slot = s;
            if (slot < 0) return false;   // 3 beacons already
        }
        if (slot > 2) return false;
        beacons_[h][slot] = MpBeacon{true, cx, cy, std::string()};
        ++beacon_events_;
        if (local) {
            post_message(gf_csf("TXT_BEACON_HELP1", "Select a beacon and hit 'enter' key to enter text"));
            post_message(gf_csf("TXT_BEACON_HELP2", "Select a beacon and hit 'delete' key to remove"));
            gf_eva("EVA_BeaconPlaced");
            if (net_) {
                ra2::net::BeaconMsg m;
                m.cmd = ra2::net::kGlobalBeaconPlace;
                m.sender = net_->name_of(h);
                m.x = cx * 256 + 128;
                m.y = cy * 256 + 128;
                m.slot = slot;
                net_->game->send_beacon(m);
            }
            beacon_sel_ = slot;
        } else if (beacon_visible(h)) {
            if (pres_.radar_ping(11, cx, cy)) gf_eva("EVA_BeaconDetected");   // RadarEvent BeaconPlaced
        }
        pres_log(0, "mp beacon place " + std::to_string(h) + " " + std::to_string(slot));
        std::cerr << "[net] beacon " << (local ? "placed" : "received") << " house " << h << " slot " << slot << " at "
                  << cx << "," << cy << "\n";
        return true;
    }
    void beacon_delete(int h, int slot, bool local) {
        if (!mp_bh(h) || slot < 0 || slot > 2 || !beacons_[h][slot].used) return;
        beacons_[h][slot] = MpBeacon{};
        ++beacon_events_;
        if (local && net_) {
            ra2::net::BeaconMsg m;
            m.cmd = ra2::net::kGlobalBeaconDelete;
            m.sender = net_->name_of(h);
            m.slot = slot;
            net_->game->send_beacon(m);
        }
        if (local && beacon_sel_ == slot) beacon_sel_ = -1;
        std::cerr << "[net] beacon " << (local ? "deleted" : "removed") << " house " << h << " slot " << slot << "\n";
    }
    void beacon_set_text(int h, int slot, const std::string& text, bool local) {
        if (!mp_bh(h) || slot < 0 || slot > 2 || !beacons_[h][slot].used) return;
        beacons_[h][slot].text = text.substr(0, ra2::net::kBeaconTextMax);
        ++beacon_events_;
        if (local && net_) {
            ra2::net::BeaconMsg m;
            m.cmd = ra2::net::kGlobalBeaconMessage;
            m.sender = net_->name_of(h);
            m.slot = slot;
            m.text = ra2::net::utf8_to_u16(beacons_[h][slot].text);
            net_->game->send_beacon(m);
        } else if (!local && beacon_visible(h)) {
            pres_.radar_ping(11, beacons_[h][slot].cx, beacons_[h][slot].cy);
        }
    }
    void mp_beacon_received(const ra2::net::BeaconMsg& m) {
        if (!mp_bh(m.house) || m.house == local_house_) return;
        if (m.cmd == ra2::net::kGlobalBeaconPlace) beacon_place(m.house, m.slot, m.x >> 8, m.y >> 8, false);
        else if (m.cmd == ra2::net::kGlobalBeaconDelete) beacon_delete(m.house, m.slot, false);
        else beacon_set_text(m.house, m.slot, ra2::net::u16_to_utf8(m.text), false);
    }
    void hotkey_place_beacon() {
        if (net_ && !observer_) beacon_placing_ = true;   // FUN_00730B00: not as an observer
    }
    void hotkey_delete_beacon() {
        if (net_ && beacon_sel_ >= 0) beacon_delete(local_house_, beacon_sel_, true);
    }
    // The beacon cursor / selection (net_handle): true = consumed.
    bool mp_beacon_handle(const ra2engine::InputEvent& ev) {
        if (!net_) return false;
        const bool down = ev.kind == ra2engine::InputEvent::PointerDown;
        if (beacon_placing_) {
            if ((down && ev.button == ra2engine::InputEvent::Right) ||
                (ev.kind == ra2engine::InputEvent::Key && ev.key == ra2engine::kKeyEscape)) {
                beacon_placing_ = false;
                return true;
            }
            if (down && ev.button == ra2engine::InputEvent::Left && ev.x < playfield_w_) {
                int cx = 0, cy = 0;
                input_.resolve_ground_cell(ev.x, ev.y, cx, cy);
                beacon_placing_ = false;
                const int own = beacon_slot_at(local_house_, cx, cy);
                if (own >= 0) beacon_sel_ = own;   // a click on an own beacon selects it
                else beacon_place(local_house_, -1, cx, cy, true);
                return true;
            }
            return false;
        }
        if (ev.kind == ra2engine::InputEvent::Key && ev.key == ra2engine::kKeyEnter && beacon_sel_ >= 0 &&
            !net_chat_open_ && !menu_.open_ && beacons_[local_house_ & 7][beacon_sel_].used) {
            beacon_editing_ = true;
            mp_open_chat(chat_mode_);
            return true;
        }
        return false;
    }
    void draw_beacons(SDL_Renderer* ren, ra2engine::TextureCache& tc) const {
        if (!net_) return;
        const double sc = hud_touch_scale();
        const Uint32 t = SDL_GetTicks();
        for (int h = 0; h < 8; ++h) {
            if (!beacon_visible(h)) continue;
            for (int s = 0; s < 3; ++s) {
                const MpBeacon& b = beacons_[h][s];
                if (!b.used) continue;
                const ra2assets::MapCell* c = world_.map().at(b.cx, b.cy);
                const int level = c ? c->level : 0;
                const int x = (b.cx - b.cy) * 30 + 30 + cam_.x;
                const int y = (b.cx + b.cy) * 15 - level * 15 + cam_.y + 15;
                if (x < -40 || x > playfield_w_ + 40 || y < -40 || y > kWinH + 40) continue;
                const SDL_Color col = house_text_color(h);
                const int r = 6 + static_cast<int>((t / 120) % 6);
                SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, 255);
                const SDL_Point d[5] = {{x, y - r}, {x + r, y}, {x, y + r}, {x - r, y}, {x, y - r}};
                SDL_RenderDrawLines(ren, d, 5);
                SDL_RenderDrawLine(ren, x, y - 24, x, y);
                if (h == local_house_ && s == beacon_sel_) {
                    const SDL_Rect sel{x - 10, y - 10, 21, 21};
                    SDL_RenderDrawRect(ren, &sel);
                }
                if (!b.text.empty()) hud_text(ren, tc, b.text, x, y - 40, col, kHudCenter, true, true, sc);
            }
        }
    }

    // --play probe keys (scripts/play/README.md "Network play").
    void mp_probe(ra2play::Probe& p) const {
        if (!net_) return;
        p.set("observer", observer_ ? 1 : 0);
        p.set("game_over", world_.game_over() ? 1 : 0);
        p.set("allies_allowed", world_.allies_allowed() ? 1 : 0);
        p.set("ally_change_allowed", world_.ally_change_allowed() ? 1 : 0);
        p.set("dip_events", mp_dip_events_);
        p.set_s("dip_last", mp_dip_last_);
        p.set("can_observe", mp_can_observe() ? 1 : 0);
        p.set("chat_mode", chat_mode_);
        p.set("chat_sent", chat_sent_);
        p.set("chat_blocked", chat_blocked_);
        p.set("chat_last_to", static_cast<double>(mp_chat_last_to_.size()));
        p.set("beacon_placing", beacon_placing_ ? 1 : 0);
        p.set("beacon_sel", beacon_sel_);
        p.set("beacon_events", beacon_events_);
        p.set("radar_on", g_observer_radar || house_owns_operational_radar(world_, ba_, local_house_) ? 1 : 0);
        int explored = 0;
        const int gw = world_.grid_w(), gh = world_.grid_h();
        for (int y = 0; y < gh; ++y)
            for (int x = 0; x < gw; ++x) explored += world_.explored_at(local_house_, x, y) ? 1 : 0;
        p.set("explored_pct", gw * gh > 0 ? 100.0 * explored / (gw * gh) : 0.0);
        for (const ra2mp::MpPlayer& pl : net_->session.players) {
            const std::string hs = std::to_string(pl.house);
            p.set_s("player_name(" + hs + ")", net_->name_of(pl.house));
            p.set("ally_view(" + hs + ")", world_.allied_with(local_house_, pl.house) ? 1 : 0);
            p.set("ally_of_me(" + hs + ")", world_.allied_with(pl.house, local_house_) ? 1 : 0);
            p.set("defeated(" + hs + ")", world_.house_defeated(pl.house) ? 1 : 0);
            p.set("listener(" + hs + ")", pl.house < 8 && chat_listen_[static_cast<std::size_t>(pl.house)] ? 1 : 0);
            p.set("beacons(" + hs + ")", beacon_count(pl.house));
            p.set("beacon_visible(" + hs + ")", beacon_visible(pl.house) ? 1 : 0);
            for (int s = 0; s < 3; ++s)
                if (pl.house < 8 && beacons_[pl.house][s].used)
                    p.set_s("beacon_text(" + hs + "," + std::to_string(s) + ")", beacons_[pl.house][s].text);
        }
    }
