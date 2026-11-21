// G0 verification harness front end: `ra2engine --play <script> [--out <dir>]`.
// #included by main.cpp after the scenes (needs Scene, DriverHooks,
// run_scene_driver, MainMenuScene, capture_png). Built on agent B's harness
// groundwork: the Runner is a DriverHooks (per-frame observer) AND an
// EventSource (the scripted InputEvents replace the Platform's), the window is
// Platform::create(..., hidden=true), keys go through key_event_from_sdl (the
// production mapping), and every scene is read through both probe surfaces --
// B's integer Scene::probe()/locate()/cheat() and G's typed Scene::play_probe()
// (targets, strings, hooks). The script language is ra2play (play_script.hpp);
// the format is documented in scripts/play/README.md.
#include <deque>

namespace ra2play_drv {
using namespace ra2play;

namespace {

struct Failure {
    int line = 0;
    long frame = 0;
    std::string what;     // the command text
    std::string detail;   // evaluated detail / reason
    std::vector<std::string> tags;
    bool hard = false;
};

std::string join_tags(const std::vector<std::string>& t) {
    std::string s;
    for (const std::string& x : t) s += (s.empty() ? "@" : " @") + x;
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

SDL_Scancode scancode_for(const std::string& name_in) {
    std::string n = lower(name_in);
    static const std::map<std::string, std::string> alias = {
        {"esc", "Escape"},     {"escape", "Escape"}, {"enter", "Return"},   {"return", "Return"},
        {"space", "Space"},    {"tab", "Tab"},       {"backspace", "Backspace"},
        {"delete", "Delete"},  {"del", "Delete"},    {"up", "Up"},          {"down", "Down"},
        {"left", "Left"},      {"right", "Right"},   {"home", "Home"},      {"end", "End"},
        {"pageup", "PageUp"},  {"pagedown", "PageDown"}, {"shift", "Left Shift"},
        {"ctrl", "Left Ctrl"}, {"alt", "Left Alt"},  {"minus", "-"},        {"plus", "="}};
    if (auto it = alias.find(n); it != alias.end()) return SDL_GetScancodeFromName(it->second.c_str());
    std::string u = name_in;
    if (u.size() == 1) u[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(u[0])));
    else if ((u[0] == 'f' || u[0] == 'F') && u.size() <= 3) u[0] = 'F';
    return SDL_GetScancodeFromName(u.c_str());
}

class Runner final : public DriverHooks, public ra2engine::EventSource {
public:
    const Script& script() const { return s_; }   // W2-CA-19 (start scene)
    Runner(Script s, std::string out_dir) : s_(std::move(s)), out_(std::move(out_dir)) {
        log_.open(out_ + "/play.log", std::ios::trunc);
        std::ofstream(out_ + "/shots.txt", std::ios::trunc);   // W3-B
        // W3-B: `set dt <s>` / `set hz <n>` choose the driver's fixed step for
        // the WHOLE run (run_scene_driver takes it once, before frame 0), so
        // the same script can render at 60 Hz (dt 1/60: one logic tick every
        // 4 frames) or 30 Hz and prove anim timing is frame-rate independent.
        for (const Cmd& c : s_.cmds)
            if (c.op == "set" && c.args.size() >= 2) {
                const std::string k = lower(c.args[0]);
                if (k == "dt") dt_ = std::atof(c.args[1].c_str());
                else if (k == "hz" && std::atof(c.args[1].c_str()) > 0) dt_ = 1.0 / std::atof(c.args[1].c_str());
            }
        // RA2_PLAY_HZ=<n> overrides the script (re-run one script at 30 Hz).
        if (const char* hz = std::getenv("RA2_PLAY_HZ"))
            if (std::atof(hz) > 0) dt_ = 1.0 / std::atof(hz);
    }

    // ---------------------------------------------------------------- driver
    // The scripted InputEvents (EventSource).
    bool poll(ra2engine::InputEvent& ev) override {
        if (q_.empty()) return false;
        ev = q_.front();
        q_.pop_front();
        return true;
    }

    // W6-01-10: `lifecycle relaunch` (run_scene_driver asks once per frame).
    bool want_relaunch() override {
        const bool r = relaunch_;
        relaunch_ = false;
        return r;
    }
    ::Scene* relaunch_scene() override {
        ra2engine::os_set_keep_autosave(false);   // the "new process"
        if (::Scene* r = make_resume_scene(/*no_audio=*/true)) return r;
        return make_main_menu_scene(/*no_audio=*/true);
    }
    bool relaunch_ = false;

    // Both probe surfaces of the current scene, merged (G's typed keys win).
    void probe(Probe& p) {
        if (!scene_ptr_) return;
        scene_ptr_->play_probe(p);
        if (p.scene == "unknown") p.scene = scene_ptr_->name();
        std::map<std::string, long long> b;
        scene_ptr_->probe(b);
        for (const auto& [k, v] : b)
            if (!p.n.count(k) && !p.s.count(k)) p.n[k] = static_cast<double>(v);
    }

    bool begin(int f, ::Scene& s) override {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {}   // the hidden window's own events are ignored
        scene_ptr_ = &s;
        f_ = f + 1;
        release_keys();
        if (park_pending_) {
            park_pending_ = false;
            move_to(park_x_, park_y_);
        }
        if (f_ > max_frames_) {
            fail_hard(-1, "max_frames", "run exceeded " + std::to_string(max_frames_) + " frames", {});
            return false;
        }
        Probe p;
        probe(p);
        note_scene(p);
        for (;;) {
            if (wait_left_ > 0) { --wait_left_; return true; }
            if (until_active_) {
                const Cmd& c = s_.cmds[pc_];
                std::string d;
                const Eval e = eval_expr(c.expr, p, vars_, d);
                if (e == Eval::True) {
                    say("PASS", c, d + " (after " + std::to_string(c.n - until_left_) + " frames)");
                    until_active_ = false;
                    ++pc_;
                    continue;
                }
                if (e == Eval::False && until_left_ <= 0 && c.op == "waituntil") {   // P3: soft
                    emit("PLAY timeout L" + std::to_string(c.line) + " f=" + std::to_string(f_) + " " + c.raw, trace_console_);
                    until_active_ = false;
                    ++pc_;
                    continue;
                }
                if (e == Eval::Missing || e == Eval::Error || until_left_ <= 0) {
                    fail(c, d + (e == Eval::False ? " (timeout " + std::to_string(c.n) + " frames)" : ""));
                    until_active_ = false;
                    ++pc_;
                    continue;
                }
                --until_left_;
                return true;
            }
            if (pc_ >= s_.cmds.size()) { finished_ = true; return false; }
            const Cmd& c = s_.cmds[pc_];
            // W3-B `set align N`: every input / cheat waits for an anim tick
            // that is a multiple of N and (for frame-consuming input) later
            // than the previous input's, so a 60 Hz and a 30 Hz run (N=2)
            // feed the game the same input at the same logic ticks.
            if (align_ > 0) {
                const std::string& o = c.op;
                const bool cheat_op = o == "cheat";
                const bool input_op = o == "click" || o == "drag" || o == "move" || o == "key" || o == "hold" ||
                                      o == "type" || o == "place";
                auto at = p.n.find("anim_tick");
                if ((cheat_op || input_op) && at != p.n.end()) {
                    const long t = static_cast<long>(at->second);
                    if (t % align_ != 0 || (input_op && t <= last_input_tick_)) return true;
                    if (input_op) last_input_tick_ = t;
                }
            }
            trace(c);
            const int r = exec(c, p);
            if (r == kStop) return false;
            if (r == kConsume) return true;
            // kNext: re-probe if the command may have changed state, then go on.
            if (c.op == "cheat" || c.op == "view") { p = Probe{}; probe(p); note_scene(p); }
        }
    }

    bool swallow_quit(int f) override {
        (void)f;
        ++quits_;
        if (expect_quit_ || !quit_continue_) return false;   // quit() records it
        {
            // Recorded against the input command that produced it; the run goes on.
            Failure x{last_input_line_, f_, last_input_raw_,
                      "process would quit at f=" + std::to_string(f_) +
                          " (InputEvent::Quit reached run_scene_driver; swallowed by set on_quit continue)",
                      last_input_tags_, false};
            fails_.push_back(x);
            emit("PLAY FAIL L" + std::to_string(x.line) + " f=" + std::to_string(f_) + " " + x.what + "  [" +
                     x.detail + "] " + join_tags(x.tags),
                 true);
            return true;
        }
    }
    // The process would exit here (a Quit event, or a scene's wants_quit).
    void quit(int f, ::Scene* s, const char* why) override {
        if (finished_ || quit_seen_) return;
        quit_seen_ = true;
        quit_frame_ = f + 1;
        quit_why_ = std::string(why) + " in scene " + (s ? s->name() : "?");
    }
    void transition(int f) override { (void)f; ++transitions_; }
    bool want_render(int f) override {
        (void)f;
        return !shot_.empty() || render_every_ <= 1 || (f_ % render_every_) == 0;
    }
    double dt() const { return dt_; }

    void end(int f, ::Scene& s, ra2engine::Platform& plat) override {
        (void)f;
        scene_ptr_ = &s;
        ren_ = static_cast<SDL_Renderer*>(plat.renderer());
        if (!shot_.empty()) {
            // W3-B: "{tick}" in a shot name -> the scene's anim_tick at capture
            // (same-tick shots from a 60 Hz and a 30 Hz run share a name).
            const std::size_t at = shot_.find("{tick}");
            if (at != std::string::npos) {
                Probe tp;
                probe(tp);
                auto it = tp.n.find("anim_tick");
                shot_.replace(at, 6, it == tp.n.end() ? std::string("na") : std::to_string(static_cast<long>(it->second)));
            }
            capture(shot_);
            // W3-B: <out>/shots.txt -- "<name> <sx> <sy>": the screen point of
            // the last `view` target at capture (the camera clamps at map
            // edges, so it is not always the playfield centre).
            {
                Probe vp;
                probe(vp);
                int sx = -1, sy = -1;
                if (have_view_ && vp.cell_to_screen) vp.cell_to_screen(view_cx_, view_cy_, sx, sy);
                std::ofstream sf(out_ + "/shots.txt", std::ios::app);
                sf << shot_ << " " << sx << " " << sy << std::endl;
            }
            shot_.clear();
        }
        if (f_ % 15 == 0) {
            Probe p;
            probe(p);
            p.set_s("scene", p.scene);
            hash_ = probe_hash(p, hash_ ^ static_cast<std::uint64_t>(f_));
            std::ostringstream o;
            o << "play_state f=" << f_ << " scene=" << p.scene;
            for (const char* k : {"credits", "units(0)", "units(1)", "buildings(0)", "buildings(1)",
                                  "selected", "tab"})
                if (auto it = p.n.find(k); it != p.n.end()) o << " " << k << "=" << it->second;
            if (auto it = p.s.find("outcome"); it != p.s.end()) o << " outcome=" << it->second;
            emit(o.str(), /*console=*/verbose_states_ && (f_ % 150) == 0);
        }
    }

    // ---------------------------------------------------------------- report
    int finish(const std::string& exit_reason) {
        if (quit_seen_ && !expect_quit_) {
            std::vector<std::string> tags = last_input_tags_;
            fail_hard(last_input_line_, last_input_raw_.empty() ? "process-quit" : last_input_raw_,
                      "process would quit at f=" + std::to_string(quit_frame_) +
                          " (" + quit_why_ + ")",
                      tags);
        } else if (!finished_ && !quit_seen_ && !hard_stop_) {
            fail_hard(last_input_line_, last_input_raw_.empty() ? "session-ended" : last_input_raw_,
                      "the scene driver returned at f=" + std::to_string(f_) + " (" + exit_reason +
                          ") before the script finished",
                      last_input_tags_);
        }
        const std::size_t unreached = finished_ ? 0 : (s_.cmds.size() > pc_ ? s_.cmds.size() - pc_ : 0);
        char hb[32];
        std::snprintf(hb, sizeof hb, "%016llx", static_cast<unsigned long long>(hash_));
        std::ostringstream o;
        o << "play_result=" << (fails_.empty() ? "PASS" : "FAIL") << " script=" << s_.name
          << " frames=" << f_ << " failures=" << fails_.size() << " unreached=" << unreached;
        emit(o.str(), true);
        emit(std::string("play_hash=") + hb, true);
        {   // W5-E: the presentation (sound / EVA) event log of the run.
            std::ofstream pl(out_ + "/pres.log", std::ios::trunc);
            for (const std::string& l : g_pres_log) pl << l << '\n';
        }
        // result.txt: one line per failure, machine-friendly for PLAYTEST-STATUS.md.
        std::ofstream r(out_ + "/result.txt", std::ios::trunc);
        r << o.str() << "\nplay_hash=" << hb << "\n";
        for (const Failure& x : fails_)
            r << "FAIL L" << x.line << " f=" << x.frame << " | " << x.what << " | " << x.detail
              << " | " << join_tags(x.tags) << (x.hard ? " | HARD" : "") << "\n";
        if (unreached) {
            r << "UNREACHED " << unreached << " command(s) from L"
              << (pc_ < s_.cmds.size() ? s_.cmds[pc_].line : 0) << "\n";
            for (std::size_t i = pc_; i < s_.cmds.size(); ++i)
                if (!s_.cmds[i].tags.empty())
                    r << "  unreached L" << s_.cmds[i].line << " | " << s_.cmds[i].raw << "\n";
        }
        return fails_.empty() ? 0 : 1;
    }

    bool verbose_states_ = true;

private:
    enum { kNext = 0, kConsume = 1, kStop = 2 };

    // ------------------------------------------------------------- commands
    int exec(const Cmd& c, Probe& p) {
        const std::string& op = c.op;
        if (op == "set") {
            const std::string k = lower(c.args[0]);
            const std::string v = c.args.size() > 1 ? c.args[1] : "";
            if (k == "dt" || k == "hz") {}   // W3-B: applied before the run (Runner ctor)
            else if (k == "align") align_ = std::max(0, std::atoi(v.c_str()));   // W3-B
            else if (k == "leave_band") g_play_leave_band = std::atoi(v.c_str()) != 0;   // W5-E2 (R-143)
            else if (k == "render_every") render_every_ = std::max(1, std::atoi(v.c_str()));
            else if (k == "max_frames") max_frames_ = std::atol(v.c_str());
            else if (k == "park" && c.args.size() > 2) { park_x_ = std::atoi(v.c_str()); park_y_ = std::atoi(c.args[2].c_str()); }
            else if (k == "states") verbose_states_ = v != "off";
            else if (k == "on_quit") quit_continue_ = (v == "continue");
            else if (k == "cycle_max") cycle_max_ = std::max(1, std::atoi(v.c_str()));
            else if (k == "campaign") {}   // W2-CA-19: consumed by run_play (start scene)
            else if (k == "movie_dir") {}  // consumed by run_play (RA2_MOVIE_DIR before the shell starts)
            else if (k == "humans" || k == "local_house") {}   // L2: consumed by run_play (before the game starts)
            else if (k == "canvas") {   // W6-01-15: `set canvas WxH` (or W H) -- before the game starts
                int w = 0, h = 0;
                if (c.args.size() > 2) { w = std::atoi(v.c_str()); h = std::atoi(c.args[2].c_str()); }
                else if (std::sscanf(v.c_str(), "%dx%d", &w, &h) != 2) w = h = 0;
                if (w >= 320 && h >= 240) { g_play_canvas_w = w & ~1; g_play_canvas_h = h & ~1; }
                else fail(c, "set canvas WxH");
            }
            else { fail(c, "unknown setting"); }
            ++pc_;
            return kNext;
        }
        if (op == "wait") { wait_left_ = c.n; ++pc_; return kNext; }
        if (op == "until" || op == "waituntil") { until_active_ = true; until_left_ = c.n; return kNext; }
        if (op == "assert" || op == "require") {
            std::string d;
            const Eval e = eval_expr(c.expr, p, vars_, d);
            ++pc_;
            if (e == Eval::True) { say("PASS", c, d); return kNext; }
            if (op == "require") { fail_hard(c.line, c.raw, d, c.tags); return kStop; }
            fail(c, d);
            return kNext;
        }
        if (op == "let") {
            Value v;
            if (!resolve_operand(c.expr, p, vars_, true, v)) fail(c, "missing-probe:" + c.expr);
            else vars_[c.args[0]] = v;
            ++pc_;
            return kNext;
        }
        if (op == "dump") {
            std::string d = probe_dump(p);
            for (const auto& [k, r] : p.targets)
                d += "target " + k + " @" + std::to_string(r.x) + "," + std::to_string(r.y) + " " +
                     std::to_string(r.w) + "x" + std::to_string(r.h) + "\n";
            emit("PLAY dump L" + std::to_string(c.line) + " f=" + std::to_string(f_) + " scene=" + p.scene +
                     "\n" + d,
                 !c.args.empty() && c.args[0] == "console");
            ++pc_;
            return kNext;
        }
        if (op == "log") { emit("PLAY log L" + std::to_string(c.line) + " " + c.raw.substr(4), true); ++pc_; return kNext; }
        if (op == "expect") {
            if (lower(c.args[0]) == "quit") expect_quit_ = true;
            else fail(c, "expect: only 'expect quit' is supported");
            ++pc_;
            return kNext;
        }
        if (op == "end") {
            if (c.args.size() >= 2 && lower(c.args[0]) == "expect") {
                std::string d;
                const Eval e = eval_expr("outcome == " + c.args[1], p, vars_, d);
                if (e == Eval::True) say("PASS", c, d);
                else fail(c, d);
            }
            ++pc_;
            finished_ = true;
            return kStop;
        }
        if (op == "cheat") {
            ++pc_;
            if (!p.cheat) {
                if (scene_ptr_ && scene_ptr_->cheat(c.args)) return kNext;
                fail(c, "missing-probe:cheat (scene '" + p.scene + "' has no cheats)");
                return kNext;
            }
            std::string err = p.cheat(c.args);
            // Fall back to the scene's own (agent B) cheat surface.
            if (!err.empty() && scene_ptr_ && scene_ptr_->cheat(c.args)) err.clear();
            if (!err.empty()) fail(c, "cheat: " + err);
            return kNext;
        }
        if (op == "shot") {
            shot_ = c.args[0];
            ++pc_;
            return kConsume;
        }
        if (op == "view") {
            ++pc_;
            double cx = 0, cy = 0;
            if (!locate(c, p, 0, cx, cy)) return kNext;
            if (!p.center_on) { fail(c, "missing-probe:center_on"); return kNext; }
            p.center_on(cx, cy);
            view_cx_ = cx;   // W3-B: shots.txt records where this lands on screen
            view_cy_ = cy;
            have_view_ = true;
            return kNext;
        }
        // W6-01-10 / W6-06-09: `lifecycle background|foreground` queues the
        // Android app event (the background autosave); `lifecycle relaunch`
        // ends the running scene as an activity restart does (destructors run,
        // the autosave is kept) and starts again from the app's startup path
        // (the Resume page, else the main menu).
        // W9-04-02/-03: `ime hide` = the soft keyboard's Back (SDL stops text
        // input behind the scene's back); `ime screenkb` = behave as a
        // soft-keyboard platform for the rest of the run (Android).
        if (op == "ime") {
            ++pc_;
            const std::string a = lower(c.args[0]);
            if (a == "hide") SDL_StopTextInput();
            else if (a == "screenkb") g_ime_force_screen_kb = true;
            else fail(c, "ime hide|screenkb");
            return kNext;
        }
        if (op == "lifecycle") {
            ++pc_;
            const std::string a = lower(c.args[0]);
            ra2engine::InputEvent e;
            if (a == "background") e.kind = ra2engine::InputEvent::AppBackground;
            else if (a == "foreground") e.kind = ra2engine::InputEvent::AppForeground;
            else if (a == "relaunch") {
                ra2engine::os_set_keep_autosave(true);
                relaunch_ = true;
                emit("PLAY lifecycle relaunch L" + std::to_string(c.line), true);
                return kConsume;
            } else { fail(c, "lifecycle background|foreground|relaunch"); return kNext; }
            q_.push_back(e);
            return kConsume;
        }
        // ---- input gestures (each consumes the frame) ----
        last_input_line_ = c.line;
        last_input_tags_ = c.tags;
        last_input_raw_ = c.raw;
        if (op == "key" || op == "hold") {
            ++pc_;
            const SDL_Scancode sc = scancode_for(c.args[0]);
            if (sc == SDL_SCANCODE_UNKNOWN) { fail(c, "unknown key '" + c.args[0] + "'"); return kConsume; }
            const int mods = mods_of(c, 1);
            press_mods(mods);
            key_event(sc, true, mods);
            held_[sc] = (op == "hold") ? c.n : 1;
            return kConsume;
        }
        if (op == "type") {
            ++pc_;
            ra2engine::InputEvent e;
            e.kind = ra2engine::InputEvent::Text;
            e.t_ms = tnow();
            std::snprintf(e.text, sizeof e.text, "%s", c.args[0].c_str());
            q_.push_back(e);
            return kConsume;
        }
        if (op == "move") {
            ++pc_;
            int x = 0, y = 0;
            if (!point_of(c, p, 0, x, y)) return kConsume;
            move_to(x, y);
            return kConsume;
        }
        if (op == "if") {
            std::string d;
            if (eval_expr(c.expr, p, vars_, d) != Eval::True) {
                emit("PLAY skip L" + std::to_string(c.line) + " f=" + std::to_string(f_) + " (" + d + ")", trace_console_);
                if (!c.sub.empty() && c.sub[0].op == "place") place_n_ = 0;   // an interrupted multi-frame place
                ++pc_;
                return kNext;
            }
            return exec(c.sub[0], p);   // advances pc_ past this line
        }
        if (op == "cycle") {
            // cycle TARGET EXPR...: click TARGET (one per frame, max 8) until EXPR holds.
            Cmd t = c;
            t.args = {c.args[0]};
            std::string d;
            if (eval_expr(c.expr, p, vars_, d) == Eval::True) {
                cycle_n_ = 0;
                ++pc_;
                return kNext;
            }
            if (++cycle_n_ > cycle_max_) {
                fail(c, d + " (after " + std::to_string(cycle_max_) + " clicks)");
                cycle_n_ = 0;
                ++pc_;
                return kNext;
            }
            int x = 0, y = 0;
            if (!point_of(t, p, 0, x, y)) { cycle_n_ = 0; ++pc_; return kConsume; }
            move_to(x, y);
            button(x, y, false, true);
            button(x, y, false, false);
            return kConsume;
        }
        if (op == "click") {
            int x = 0, y = 0;
            {   // a cameo on another tab: click that tab first, then retry (max 4 frames)
                const std::string nm = target_name(c, 0);
                int tx = 0, ty = 0;
                if (nm.rfind("cameo:", 0) == 0 && !p.targets.count(nm) && scene_ptr_ &&
                    scene_ptr_->locate("cameotab:" + nm.substr(6), tx, ty) && ++tab_tries_ <= 4) {
                    move_to(tx, ty);
                    button(tx, ty, false, true);
                    button(tx, ty, false, false);
                    return kConsume;
                }
                tab_tries_ = 0;
            }
            ++pc_;
            // P4: `soft` -- a target that cannot be resolved / brought on screen
            // is skipped (logged), not a failure (a player who cannot see it
            // simply does not click it).
            soft_ = has_word(c, "soft");
            const bool found = point_of(c, p, 0, x, y);
            soft_ = false;
            if (!found) return kConsume;
            const bool right = has_word(c, "right");
            const int mods = mods_of(c, 1);
            press_mods(mods);
            held_mods_ = mods;
            move_to(x, y);
            button(x, y, right, true);
            button(x, y, right, false);
            if (!has_word(c, "nopark")) park_pending_ = true;
            return kConsume;
        }
        if (op == "touch") {
            ++pc_;
            // W4-AM: drive the REAL gesture layer (TouchGestures) with finger
            // events; its output goes through the scenes' handle() like the
            // Platform's. touch tap|dtap|hold <target> ; touch drag|boxdrag|pan2
            // xy|cell X1 Y1 X2 Y2 (one-finger drag = scroll, hold+drag = box,
            // two-finger drag = scroll).
            if (c.args.empty()) { fail(c, "touch tap|dtap|hold|drag|boxdrag|pan2 ..."); return kConsume; }
            const std::string g = lower(c.args[0]);
            const uint32_t t = tnow();
            if (g == "tap" || g == "dtap" || g == "hold") {
                int x = 0, y = 0;
                if (c.args.size() < 2 || !point_of(c, p, 1, x, y)) return kConsume;
                tg_.finger_down(1, x, y, t);
                if (g == "hold") tg_.tick(t + 700);
                tg_.finger_up(1, x, y, g == "hold" ? t + 720 : t + 60);
                if (g == "dtap") {
                    tg_.finger_down(1, x + 3, y + 2, t + 200);
                    tg_.finger_up(1, x + 3, y + 2, t + 260);
                }
            } else if (g == "drag" || g == "boxdrag" || g == "pan2") {
                if (c.args.size() < 6) { fail(c, "touch " + g + " xy|cell X1 Y1 X2 Y2 | rel <target> FX1 FY1 FX2 FY2"); return kConsume; }
                Cmd a = c, b = c;
                if (lower(c.args[1]) == "rel") {   // w9int: both ends relative to one target's rectangle
                    if (c.args.size() < 7) { fail(c, "touch " + g + " rel <target> FX1 FY1 FX2 FY2"); return kConsume; }
                    a.args = {c.args[1], c.args[2], c.args[3], c.args[4]};
                    b.args = {c.args[1], c.args[2], c.args[5], c.args[6]};
                } else {
                    a.args = {c.args[1], c.args[2], c.args[3]};
                    b.args = {c.args[1], c.args[4], c.args[5]};
                }
                int x1, y1, x2, y2;
                if (!point_of(a, p, 0, x1, y1) || !point_of(b, p, 0, x2, y2)) return kConsume;
                uint32_t tt = t;
                tg_.finger_down(1, x1, y1, tt);
                if (g == "pan2") tg_.finger_down(2, x1 + 80, y1, tt + 5);
                if (g == "boxdrag") { tt += 700; tg_.tick(tt); }
                for (int i = 1; i <= 8; ++i) {
                    tt += 16;
                    const int xi = x1 + (x2 - x1) * i / 8, yi = y1 + (y2 - y1) * i / 8;
                    tg_.finger_move(1, xi, yi, tt);
                    if (g == "pan2") tg_.finger_move(2, xi + 80, yi, tt + 1);
                }
                tg_.finger_up(1, x2, y2, tt + 20);
                if (g == "pan2") tg_.finger_up(2, x2 + 80, y2, tt + 25);
            } else {
                fail(c, "unknown touch gesture '" + g + "'");
                return kConsume;
            }
            ra2engine::InputEvent e;
            while (tg_.pop(e)) q_.push_back(e);
            return kConsume;
        }
        if (op == "drag") {
            ++pc_;
            // drag cell x1 y1 x2 y2 | drag xy x1 y1 x2 y2
            if (c.args.size() < 5) { fail(c, "drag cell|xy X1 Y1 X2 Y2"); return kConsume; }
            int x1, y1, x2, y2;
            Cmd a = c, b = c;
            a.args = {c.args[0], c.args[1], c.args[2]};
            b.args = {c.args[0], c.args[3], c.args[4]};
            if (!point_of(a, p, 0, x1, y1) || !point_of(b, p, 0, x2, y2)) return kConsume;
            const bool right = has_word(c, "right");
            move_to(x1, y1);
            button(x1, y1, right, true);
            // Intermediate motion so the drag threshold is crossed.
            move_to((x1 + x2) / 2, (y1 + y2) / 2);
            move_to(x2, y2);
            button(x2, y2, right, false);
            park_pending_ = true;
            return kConsume;
        }
        if (op == "place") {
            // Frame 0: pick the candidate and point at it; a first try clicks at
            // once. A retry (P4; this placement's last click was refused) acts
            // like a player watching the placement grid: while the hovered origin
            // (place_origin_*) is not the chosen one (the level-aware pointer
            // pick can land one cell off next to a cliff), nudge the pointer
            // (max 12 frames), then click.
            static const int kNudge[][2] = {{0, 0}, {0, -15}, {0, 15}, {-30, 0}, {30, 0}, {0, -30}, {0, 30},
                                            {-15, -8}, {15, -8}, {-15, 8}, {15, 8}, {0, -45}, {0, 45}};
            if (place_n_ == 0) {
                if (!p.auto_place) { fail(c, "missing-probe:auto_place"); ++pc_; return kConsume; }
                int cx = 0, cy = 0;
                const bool near = lower(c.args[0]) == "near" && c.args.size() >= 3;
                if (near && !p.auto_place_near) { fail(c, "missing-probe:auto_place_near"); ++pc_; return kConsume; }
                if (near ? !p.auto_place_near(std::atoi(c.args[1].c_str()), std::atoi(c.args[2].c_str()), cx, cy)
                         : !p.auto_place(cx, cy)) { fail(c, "no valid placement cell (or nothing armed)"); ++pc_; return kConsume; }
                if (!cell_point(p, cx, cy, place_x_, place_y_)) { fail(c, "placement cell off-screen (" + std::to_string(cx) + "," + std::to_string(cy) + ")"); ++pc_; return kConsume; }
                emit("PLAY place L" + std::to_string(c.line) + " cell=" + std::to_string(cx) + "," + std::to_string(cy) +
                     " xy=" + std::to_string(place_x_) + "," + std::to_string(place_y_), false);
                move_to(place_x_, place_y_);
                if (!p.n.count("place_target_x")) {   // first try: one-frame click (as before P4)
                    ++pc_;
                    button(place_x_, place_y_, false, true);
                    button(place_x_, place_y_, false, false);
                    park_pending_ = true;
                    return kConsume;
                }
                place_n_ = 1;   // a retry (the last click was refused): watch the hovered origin
                return kConsume;
            }
            const auto num = [&p](const char* k, double& v) {
                const auto it = p.n.find(k);
                if (it == p.n.end()) return false;
                v = it->second;
                return true;
            };
            double hx = 0, hy = 0, tx = 0, ty = 0;
            const bool known = num("place_origin_x", hx) && num("place_origin_y", hy) &&
                               num("place_target_x", tx) && num("place_target_y", ty);
            const int last = static_cast<int>(sizeof kNudge / sizeof kNudge[0]);
            if (known && (hx != tx || hy != ty) && place_n_ < last) {
                move_to(place_x_ + kNudge[place_n_][0], place_y_ + kNudge[place_n_][1]);
                ++place_n_;
                return kConsume;
            }
            const int k = (known && hx == tx && hy == ty) ? place_n_ - 1 : 0;
            const int x = place_x_ + kNudge[k][0], y = place_y_ + kNudge[k][1];
            place_n_ = 0;
            ++pc_;
            button(x, y, false, true);
            button(x, y, false, false);
            park_pending_ = true;
            return kConsume;
        }
        fail(c, "unhandled command");
        ++pc_;
        return kNext;
    }

    // Resolve the n-th..: target forms used by click/move/view.
    //   cell X Y | xy X Y | unit H:T[:n] | building H:T[:n] | <named target>
    bool locate(const Cmd& c, Probe& p, std::size_t i, double& cx, double& cy) {
        const std::string k = lower(c.args[i]);
        if (k == "cell") {
            if (c.args.size() < i + 3) { fail(c, "cell X Y"); return false; }
            cx = std::atof(c.args[i + 1].c_str());
            cy = std::atof(c.args[i + 2].c_str());
            return true;
        }
        if (k == "point") {   // G16: a named map point (+ optional offset)
            if (c.args.size() < i + 2) { fail(c, "point NAME"); return false; }
            if (!p.find_point) { fail(c, "missing-probe:find_point"); return false; }
            if (!p.find_point(c.args[i + 1], cx, cy)) { fail(c, "no point " + c.args[i + 1]); return false; }
            if (c.args.size() >= i + 4 && (c.args[i + 2][0] == '+' || c.args[i + 2][0] == '-') &&
                (c.args[i + 3][0] == '+' || c.args[i + 3][0] == '-')) {
                cx += std::atof(c.args[i + 2].c_str());
                cy += std::atof(c.args[i + 3].c_str());
            }
            return true;
        }
        if (k == "unit" || k == "building") {
            if (c.args.size() < i + 2) { fail(c, k + " H:TYPE[:n]"); return false; }
            auto& fn = (k == "unit") ? p.find_unit : p.find_building;
            if (!fn) { fail(c, "missing-probe:find_" + k); return false; }
            if (!fn(c.args[i + 1], cx, cy)) { fail(c, "no " + k + " " + c.args[i + 1]); return false; }
            // Optional signed cell offset: "unit 0:E1 +4 -2".
            if (c.args.size() >= i + 4 && (c.args[i + 2][0] == '+' || c.args[i + 2][0] == '-') &&
                (c.args[i + 3][0] == '+' || c.args[i + 3][0] == '-')) {
                cx = std::floor(cx + 0.5) + std::atof(c.args[i + 2].c_str());
                cy = std::floor(cy + 0.5) + std::atof(c.args[i + 3].c_str());
            }
            return true;
        }
        fail(c, "expected cell|unit|building");
        return false;
    }

    bool cell_point(Probe& p, double cx, double cy, int& x, int& y) {
        if (!p.cell_to_screen) return false;
        if (p.cell_to_screen(cx, cy, x, y)) return true;
        if (!p.center_on) return false;
        p.center_on(cx, cy);
        return p.cell_to_screen(cx, cy, x, y);
    }

    // A named target from args[i..]: "button X Y Z" -> "button:X Y Z" (multi-word
    // labels, B's form), "cameo ID" -> "cameo:ID", else args[i] itself.
    static bool is_flag(const std::string& a) {
        const std::string l = lower(a);
        return l == "right" || l == "shift" || l == "ctrl" || l == "alt" || l == "nopark" || l == "onscreen" ||
               l == "soft";
    }
    static std::string target_name(const Cmd& c, std::size_t i) {
        std::string name = c.args[i];
        const std::string l = lower(name);
        if ((l == "button" || l == "cameo") && c.args.size() > i + 1) {
            std::string rest;
            for (std::size_t k = i + 1; k < c.args.size() && !is_flag(c.args[k]); ++k)
                rest += (rest.empty() ? "" : " ") + c.args[k];
            name = l + ":" + rest;
        }
        return name;
    }

    static bool is_number(const std::string& a) {
        if (a.empty()) return false;
        std::size_t k = (a[0] == '-' || a[0] == '+') ? 1 : 0;
        if (k >= a.size()) return false;
        for (; k < a.size(); ++k)
            if (std::isdigit(static_cast<unsigned char>(a[k])) == 0 && a[k] != '.') return false;
        return true;
    }
    // A probe target's window rectangle (`rel` points).
    bool target_rect(const Cmd& c, Probe& p, const std::string& name, Rect& r) {
        const auto it = p.targets.find(name);
        if (it == p.targets.end()) {
            fail(c, "no target '" + name + "' on scene '" + p.scene + "'");
            return false;
        }
        r = it->second;
        return true;
    }
    bool point_of(const Cmd& c, Probe& p, std::size_t i, int& x, int& y) {
        const std::string k = lower(c.args[i]);
        // B's bare form "click X Y" = window pixels.
        if (!k.empty() && (std::isdigit(static_cast<unsigned char>(k[0])) != 0) && c.args.size() > i + 1 &&
            std::isdigit(static_cast<unsigned char>(c.args[i + 1][0])) != 0) {
            x = std::atoi(c.args[i].c_str());
            y = std::atoi(c.args[i + 1].c_str());
            return true;
        }
        // w9int: `rel <target> FX FY [DX DY]` -- a point relative to a target's
        // rectangle (FX / FY: 0 = left / top edge, 1 = right / bottom edge;
        // DX / DY: window pixels added), so a script follows a layout change.
        if (k == "rel") {
            if (c.args.size() < i + 4) { fail(c, "rel <target> FX FY [DX DY]"); return false; }
            Rect r;
            if (!target_rect(c, p, c.args[i + 1], r)) return false;
            const double fx = std::atof(c.args[i + 2].c_str()), fy = std::atof(c.args[i + 3].c_str());
            int dx = 0, dy = 0;
            if (c.args.size() >= i + 6 && is_number(c.args[i + 4]) && is_number(c.args[i + 5])) {
                dx = std::atoi(c.args[i + 4].c_str());
                dy = std::atoi(c.args[i + 5].c_str());
            }
            x = r.x + static_cast<int>(std::floor(fx * r.w + 0.5)) + dx;
            y = r.y + static_cast<int>(std::floor(fy * r.h + 0.5)) + dy;
            if (fx >= 1.0 && dx == 0) x = r.x + r.w - 1;   // the last pixel inside, not the next one
            if (fy >= 1.0 && dy == 0) y = r.y + r.h - 1;
            return true;
        }
        if (k == "xy") {
            if (c.args.size() < i + 3) { fail(c, "xy X Y"); return false; }
            x = std::atoi(c.args[i + 1].c_str());
            y = std::atoi(c.args[i + 2].c_str());
            return true;
        }
        if (k == "cell" || k == "unit" || k == "building" || k == "point") {
            double cx = 0, cy = 0;
            if (!locate(c, p, i, cx, cy)) return false;
            if (!p.cell_to_screen) { fail(c, "missing-probe:cell_to_screen (scene '" + p.scene + "')"); return false; }
            // W4-MC-07 (HB-4): `onscreen` = a human must SEE the target -- no
            // harness centring; an off-screen target is a failure.
            if (has_word(c, "onscreen")) {
                if (!p.cell_to_screen(cx, cy, x, y)) { fail(c, "target off-screen (no centring with onscreen)"); return false; }
                return true;
            }
            if (!cell_point(p, cx, cy, x, y)) {
                // P4: a building whose centre stays outside the playfield margin
                // when the camera is clamped at the map edge (e.g. behind the
                // sidebar) -- a player clicks the part they can see: try its
                // other foundation cells, nearest the centre first.
                int ox = 0, oy = 0, fw = 0, fh = 0;
                if (k == "building" && p.find_building_rect && c.args.size() > i + 1 &&
                    p.find_building_rect(c.args[i + 1], ox, oy, fw, fh)) {
                    std::vector<std::pair<int, int>> cells;
                    for (int yy = oy; yy < oy + fh; ++yy)
                        for (int xx = ox; xx < ox + fw; ++xx) cells.emplace_back(xx, yy);
                    std::stable_sort(cells.begin(), cells.end(), [&](const auto& a, const auto& b) {
                        return std::abs(a.first - cx) + std::abs(a.second - cy) <
                               std::abs(b.first - cx) + std::abs(b.second - cy);
                    });
                    for (const auto& cl : cells)
                        if (p.cell_to_screen(cl.first, cl.second, x, y)) return true;
                }
                fail(c, "cell off-screen and cannot centre");
                return false;
            }
            return true;
        }
        const std::string name = target_name(c, i);
        auto it = p.targets.find(name);
        if (it == p.targets.end() && scene_ptr_ && scene_ptr_->locate(name, x, y)) return true;   // B's locate()
        if (it == p.targets.end()) {
            std::string avail;
            int n = 0;
            for (const auto& [tn, r] : p.targets) {
                (void)r;
                if (n++ < 40) avail += (avail.empty() ? "" : ", ") + tn;
            }
            fail(c, "no target '" + name + "' on scene '" + p.scene + "' (have: " + avail + ")");
            return false;
        }
        x = it->second.cx();
        y = it->second.cy();
        return true;
    }

    static bool has_word(const Cmd& c, const char* w) {
        for (const std::string& a : c.args)
            if (lower(a) == w) return true;
        return false;
    }
    static int mods_of(const Cmd& c, std::size_t from) {
        int m = 0;
        for (std::size_t i = from; i < c.args.size(); ++i) {
            const std::string a = lower(c.args[i]);
            if (a == "shift") m |= KMOD_LSHIFT;
            else if (a == "ctrl") m |= KMOD_LCTRL;
            else if (a == "alt") m |= KMOD_LALT;
        }
        return m;
    }

    // --------------------------------------------------------------- inject
    Uint8* keystate() { return const_cast<Uint8*>(SDL_GetKeyboardState(nullptr)); }

    void press_mods(int mods) {
        held_mods_ = mods;
        SDL_SetModState(static_cast<SDL_Keymod>(mods));
        Uint8* ks = keystate();
        ks[SDL_SCANCODE_LSHIFT] = (mods & KMOD_LSHIFT) ? 1 : 0;
        ks[SDL_SCANCODE_LCTRL] = (mods & KMOD_LCTRL) ? 1 : 0;
        ks[SDL_SCANCODE_LALT] = (mods & KMOD_LALT) ? 1 : 0;
    }

    // Key down: the production mapping (key_event_from_sdl). The SDL key-state
    // array is also set while the key is held, for the arrow-key scroll that
    // GameScene::update still polls.
    void key_event(SDL_Scancode sc, bool down, int mods) {
        keystate()[sc] = down ? 1 : 0;
        if (!down) return;
        ra2engine::InputEvent e;
        ra2engine::key_event_from_sdl(SDL_GetKeyFromScancode(sc), mods, e);
        e.t_ms = tnow();
        q_.push_back(e);
    }
    uint32_t tnow() const { return static_cast<uint32_t>(f_ * 67 + 1000); }   // virtual ms
    ra2engine::InputEvent ptr_ev(ra2engine::InputEvent::Kind k, int x, int y, bool right) const {
        ra2engine::InputEvent e;
        e.kind = k;
        e.x = x;
        e.y = y;
        e.button = right ? ra2engine::InputEvent::Right : ra2engine::InputEvent::Left;
        e.shift = (held_mods_ & KMOD_SHIFT) != 0;
        e.ctrl = (held_mods_ & KMOD_CTRL) != 0;
        e.alt = (held_mods_ & KMOD_ALT) != 0;
        e.t_ms = tnow();
        return e;
    }

    void release_keys() {
        for (auto it = held_.begin(); it != held_.end();) {
            if (--it->second <= 0) {
                key_event(static_cast<SDL_Scancode>(it->first), false, held_mods_);
                it = held_.erase(it);
            } else {
                ++it;
            }
        }
        if (held_.empty() && held_mods_) press_mods(0);
    }

    void move_to(int x, int y) { q_.push_back(ptr_ev(ra2engine::InputEvent::PointerMove, x, y, false)); }
    void button(int x, int y, bool right, bool down) {
        q_.push_back(ptr_ev(down ? ra2engine::InputEvent::PointerDown : ra2engine::InputEvent::PointerUp, x, y,
                            right));
    }

    void capture(const std::string& name) {
        if (!ren_) return;
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(ren_, &w, &h);
        std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4);
        if (SDL_RenderReadPixels(ren_, nullptr, SDL_PIXELFORMAT_RGBA32, px.data(), w * 4) != 0) {
            emit("PLAY shot " + name + " FAILED: " + SDL_GetError(), true);
            return;
        }
        std::string fn = name;
        if (fn.size() < 4 || fn.substr(fn.size() - 4) != ".png") fn += ".png";
        const std::string path = out_ + "/" + fn;
        if (!stbi_write_png(path.c_str(), w, h, 4, px.data(), w * 4))
            emit("PLAY shot " + path + " FAILED (write)", true);
        else
            emit("PLAY shot f=" + std::to_string(f_) + " " + path, true);
    }

    // ------------------------------------------------------------- logging
    void note_scene(Probe& p) {
        if (p.scene != scene_) {
            emit("PLAY scene f=" + std::to_string(f_) + " " + scene_ + " -> " + p.scene, true);
            scene_ = p.scene;
            sf_ = 0;
        } else {
            ++sf_;
        }
        p.set("f", static_cast<double>(f_));
        p.set("sf", static_cast<double>(sf_));
        p.set_s("scene", p.scene);
        p.set("quit_seen", quit_seen_ ? 1 : 0);
        p.set("quits", quits_);
        p.set("transitions", transitions_);
        // R-183: how many optional data files this install lacks (robust-missing
        // only applies to a stripped install and ends early on a full one).
        if (data_missing_optional_ < 0)
            data_missing_optional_ = static_cast<int>(preflight_data_files(env_install_dir()).missing_optional.size());
        p.set("data_missing_optional", data_missing_optional_);
    }
    int data_missing_optional_ = -1;
    void trace(const Cmd& c) {
        if (((c.op == "until" || c.op == "waituntil") && until_active_) || (c.op == "cycle" && cycle_n_ > 0) ||
            (c.op == "place" && place_n_ > 0))
            return;
        emit("PLAY step L" + std::to_string(c.line) + " f=" + std::to_string(f_) + " " + c.raw, trace_console_);
    }
    void say(const char* what, const Cmd& c, const std::string& d) {
        emit(std::string("PLAY ") + what + " L" + std::to_string(c.line) + " f=" + std::to_string(f_) + " " +
                 c.raw + "  [" + d + "]",
             true);
    }
    void fail(const Cmd& c, const std::string& d) {
        if (soft_) {
            emit("PLAY skip L" + std::to_string(c.line) + " f=" + std::to_string(f_) + " soft: " + d, trace_console_);
            return;
        }
        fails_.push_back({c.line, f_, c.raw, d, c.tags, false});
        emit("PLAY FAIL L" + std::to_string(c.line) + " f=" + std::to_string(f_) + " " + c.raw + "  [" + d +
                 "] " + join_tags(c.tags),
             true);
    }
    void fail_hard(int line, const std::string& what, const std::string& d, const std::vector<std::string>& tags) {
        fails_.push_back({line, f_, what, d, tags, true});
        hard_stop_ = true;
        emit("PLAY FAIL(hard) L" + std::to_string(line) + " f=" + std::to_string(f_) + " " + what + "  [" + d +
                 "] " + join_tags(tags),
             true);
    }
    void emit(const std::string& s, bool console) {
        if (console) std::cout << s << "\n" << std::flush;
        if (log_) log_ << s << "\n";
    }

    Script s_;
    std::string out_;
    std::ofstream log_;
    SDL_Renderer* ren_ = nullptr;
    ::Scene* scene_ptr_ = nullptr;
    std::deque<ra2engine::InputEvent> q_;
    ra2engine::TouchGestures tg_;   // W4-AM: `touch` commands
    std::string quit_why_ = "Quit event";
    int tab_tries_ = 0;
    long f_ = 0;
    long sf_ = 0;
    std::string scene_ = "(none)";
    std::size_t pc_ = 0;
    int wait_left_ = 0;
    int cycle_n_ = 0;
    int place_n_ = 0, place_x_ = 0, place_y_ = 0;   // P4: multi-frame `place`
    bool soft_ = false;                              // P4: `click ... soft`
    int cycle_max_ = 8;
    bool until_active_ = false;
    int until_left_ = 0;
    std::map<int, int> held_;   // scancode -> frames left
    int held_mods_ = 0;
    bool park_pending_ = false;
    int park_x_ = 560, park_y_ = 360;
    std::string shot_;
    double dt_ = 1.0 / 15.0;
    double view_cx_ = 0, view_cy_ = 0;   // W3-B: last `view` target (cells)
    int align_ = 0;                      // W3-B `set align N`
    long last_input_tick_ = -1;
    bool have_view_ = false;
    int render_every_ = 1;
    long max_frames_ = 40000;
    std::map<std::string, Value> vars_;
    std::vector<Failure> fails_;
    std::uint64_t hash_ = 1469598103934665603ull;
    bool finished_ = false, hard_stop_ = false;
    bool quit_seen_ = false, expect_quit_ = false;
    long quit_frame_ = 0;
    int quits_ = 0;
    int transitions_ = 0;
    bool quit_continue_ = false;
    std::string last_input_raw_;
    int last_input_line_ = -1;
    std::vector<std::string> last_input_tags_;
    bool trace_console_ = true;
};

}  // namespace

}  // namespace ra2play_drv

// --play entry: parse, isolate RA2.INI/saves (RA2_PREF_DIR), build the real
// shell exactly as run_menu does but in a hidden window, and drive it.
int run_play(const std::string& script_path, const std::string& out_dir_in) {
    using namespace ra2play;
    std::ifstream in(script_path, std::ios::binary);
    if (!in) {
        std::cerr << "play: cannot read " << script_path << "\n";
        return 2;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    Script s;
    std::string err;
    if (!parse_script(ss.str(), s, err)) {
        std::cerr << "play: " << script_path << ": " << err << "\n";
        return 2;
    }
    s.name = std::filesystem::path(script_path).stem().string();
    const std::string out_dir = out_dir_in.empty() ? ("play-out/" + s.name) : out_dir_in;
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    const std::string pref = out_dir + "/pref";
    std::filesystem::remove_all(pref, ec);
    std::filesystem::create_directories(pref, ec);
    const std::string pref_abs = std::filesystem::absolute(pref).string() + "/";
    SDL_setenv("RA2_PREF_DIR", pref_abs.c_str(), 1);
    // SDL_setenv on a MinGW/Windows build writes the Win32 environment block
    // only; settings_file_path() / the save dir read the CRT copy (getenv),
    // so without this the isolation silently fell back to %APPDATA%.
#ifdef _WIN32
    _putenv_s("RA2_PREF_DIR", pref_abs.c_str());
#else
    setenv("RA2_PREF_DIR", pref_abs.c_str(), 1);
#endif

    // `set movie_dir <dir>` (relative to the script's folder): the movie folder
    // a sweep uses when the player did not set RA2_MOVIE_DIR (movies-w6 plays
    // the repo's test .bik), so the Play Movies row is live in a standard run.
    for (const ra2play::Cmd& c : s.cmds)
        if (c.op == "set" && c.args.size() >= 2 && ra2play_drv::lower(c.args[0]) == "movie_dir" &&
            !std::getenv("RA2_MOVIE_DIR")) {
            std::filesystem::path md(c.args[1]);
            if (md.is_relative()) md = std::filesystem::path(script_path).parent_path() / md;
            const std::string mds = std::filesystem::absolute(md, ec).lexically_normal().string();
#ifdef _WIN32
            _putenv_s("RA2_MOVIE_DIR", mds.c_str());
#else
            setenv("RA2_MOVIE_DIR", mds.c_str(), 1);
#endif
        }

    // L2 debug: `set humans 0,1` (these skirmish slots are human houses) and
    // `set local_house N` (the scene plays house N).
    for (const ra2play::Cmd& c : s.cmds) {
        if (c.op != "set" || c.args.size() < 2) continue;
        const std::string k = ra2play_drv::lower(c.args[0]);
        if (k == "local_house") g_debug_local_house = std::atoi(c.args[1].c_str());
        if (k == "humans") {
            g_debug_humans.clear();
            std::string v = c.args[1];
            for (std::size_t i = 2; i < c.args.size(); ++i) v += "," + c.args[i];
            std::size_t p = 0;
            while (p <= v.size()) {
                const std::size_t q = v.find(',', p);
                const std::string t = v.substr(p, q == std::string::npos ? std::string::npos : q - p);
                if (!t.empty()) g_debug_humans.push_back(std::atoi(t.c_str()));
                if (q == std::string::npos) break;
                p = q + 1;
            }
        }
    }
    g_pres_play_log = true;   // W5-E: <out>/pres.log + the snd()/eva() probe counters
    g_pres_log.clear();
    g_pres_audio_counts.clear();
    ra2play_drv::Runner r(std::move(s), out_dir);
    int rc = 0;
    try {
        const std::string dir = env_install_dir();
        Ini rules_ini = load_ini_from_ra2md(dir, "rulesmd.ini");
        Rules rules = Rules::load(rules_ini);
        const char* vis = std::getenv("RA2_PLAY_VISIBLE");
        ra2engine::Platform plat =
            ra2engine::Platform::create("RA2 --play", 1280, 720, !(vis && std::atoi(vis) != 0));
        ra2engine::TextureCache tc(plat.renderer());
        MenuArtStore store;
        load_menu_art(dir, /*no_audio=*/true, store);
        ra2engine::Settings user = load_user_settings();   // from the isolated pref dir
        g_user_settings = &user;
        g_menu_rules = &rules;
        g_menu_art = &store.art;
        // W2-CA-19: `set campaign <MAP> <GDI|Nod> [difficulty]` starts that
        // mission's loading screen directly (missions 2-12 without winning
        // every earlier one); otherwise the real shell's main menu.
        Scene* first = nullptr;
        for (const ra2play::Cmd& c : r.script().cmds)
            if (c.op == "set" && c.args.size() >= 3 && ra2play_drv::lower(c.args[0]) == "campaign")
                first = new CampaignLoadScene(/*no_audio=*/true,
                                              campaign_config(c.args[1], c.args[2],
                                                              c.args.size() > 3 ? std::atoi(c.args[3].c_str()) : 1));
        if (!first) first = new MainMenuScene(rules, /*no_audio=*/true, store.art);
        run_scene_driver(plat, tc, first, &r, r.dt(), &r);
        g_menu_rules = nullptr;
        g_menu_art = nullptr;
        g_user_settings = nullptr;
        rc = r.finish("driver returned");
    } catch (const std::exception& ex) {
        g_menu_rules = nullptr;
        g_menu_art = nullptr;
        g_user_settings = nullptr;
        std::cout << "PLAY FAIL(hard) exception: " << ex.what() << "\n";
        r.finish(std::string("exception: ") + ex.what());
        rc = 3;
    }
    return rc;
}
