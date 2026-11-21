// W5-F: performance overlay / perf log (R-170, R-171, R-172) and raw input
// record / replay (R-184). Textually included at the end of app_robust.inc
// (anonymous namespace, before run_scene_driver). Interactive-only: no
// byte-identity gate and no --play hash reads anything here (the overlay is
// off unless asked for, and recording only writes a file).
//
// PERF OVERLAY. Off by default. Turned on by any of:
//   * desktop: --fps on the command line, RA2_PERF=1, or RA2.INI
//     [Engine] ShowFPS=yes;
//   * Android: `adb shell setprop debug.ra2.perf 1` before starting the app;
//   * in a running game: Ctrl+Shift+F (keyboard) or a THREE-FINGER TAP.
// It shows, averaged over the last second: rendered frames/s, logic
// ticks/s (15 = full speed at GameSpeed 4 "1x"), and the milliseconds per
// frame spent in input, update (simulation + per-frame tail), render
// (building the draw list) and present (GPU flush / vsync wait), plus the
// worst frame. While it is on, the same numbers go to the log every 5 s as
//   [perf] fps=.. logic=../s input=..ms update=..ms render=..ms present=..ms worst=..ms
// (logcat tag SDL/APP on Android) so a tester can paste them into a report.
// RA2_PERF=log (or debug.ra2.perf=log) logs without drawing the overlay.
//
// INPUT RECORD / REPLAY (desktop). `--record <file>` (or RA2_RECORD=<file>)
// writes every input event the scene driver hands to a scene, plus each
// frame's time step, to a text file. `--replay <file>` runs the menu with
// those events and time steps instead of the mouse / keyboard / wall clock,
// so the same session plays back; add `--hidden` to replay without a window.
// Both print `[replay] end frame=N scene=S probe_hash=H` when the run ends,
// and the two hashes match when the replay reproduced the session. Same data
// files and RA2.INI are assumed (GameSpeed, difficulty). [Engine affordance:
// the original has no input recorder.]

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

struct PerfMeter {
    bool overlay = false;     // draw the overlay
    bool log = false;         // print [perf] lines every 5 s
    // current 1-second window
    Uint64 win_start = 0;
    int frames = 0;
    int logic = 0;
    double t_input = 0, t_update = 0, t_render = 0, t_present = 0, worst = 0;
    // 5-second log window
    Uint64 log_start = 0;
    int log_frames = 0, log_logic = 0;
    double l_input = 0, l_update = 0, l_render = 0, l_present = 0, l_worst = 0;
    // last completed 1-second window (what the overlay shows)
    double fps = 0, lps = 0, a_input = 0, a_update = 0, a_render = 0, a_present = 0, a_worst = 0;
};
PerfMeter g_perf;
int g_perf_logic_ticks = 0;   // GameScene's logic loop counts its ticks here

std::string perf_ini_value(const char* key) {
    const std::string path = data_dir_ini_path();
    if (path.empty()) return {};
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    const ra2engine::IniDoc doc = ra2engine::IniDoc::parse(ss.str());
    const std::optional<std::string> v = doc.get("Engine", key);
    return v ? *v : std::string();
}

void perf_apply_setting(const std::string& v) {
    if (v.empty()) return;
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(v[0])));
    if (c == 'l') { g_perf.log = true; return; }   // "log": numbers only
    if (c == '1' || c == 'y' || c == 't' || c == 'o') { g_perf.overlay = true; g_perf.log = true; }
}

// Called once by run_menu (after --fps was parsed into g_perf).
void perf_init() {
    perf_apply_setting(perf_ini_value("ShowFPS"));
#ifdef __ANDROID__
    char prop[PROP_VALUE_MAX] = {0};
    if (__system_property_get("debug.ra2.perf", prop) > 0) perf_apply_setting(prop);
#else
    if (const char* e = std::getenv("RA2_PERF")) perf_apply_setting(e);
#endif
    if (g_perf.overlay || g_perf.log)
        SDL_Log("[perf] overlay=%s log=%s (toggle: Ctrl+Shift+F or a three-finger tap)",
                g_perf.overlay ? "on" : "off", g_perf.log ? "on" : "off");
}

void perf_toggle() {
    g_perf.overlay = !g_perf.overlay;
    g_perf.log = g_perf.overlay;
    SDL_Log("[perf] overlay %s", g_perf.overlay ? "on" : "off");
}

// One finished frame: the four phase times in ms.
void perf_frame(double in_ms, double up_ms, double ren_ms, double pre_ms) {
    if (!g_perf.overlay && !g_perf.log) return;
    const Uint64 now = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    const int ticks = g_perf_logic_ticks;
    g_perf_logic_ticks = 0;
    PerfMeter& P = g_perf;
    if (P.win_start == 0) P.win_start = P.log_start = now;
    const double total = in_ms + up_ms + ren_ms + pre_ms;
    ++P.frames; P.logic += ticks;
    P.t_input += in_ms; P.t_update += up_ms; P.t_render += ren_ms; P.t_present += pre_ms;
    P.worst = std::max(P.worst, total);
    ++P.log_frames; P.log_logic += ticks;
    P.l_input += in_ms; P.l_update += up_ms; P.l_render += ren_ms; P.l_present += pre_ms;
    P.l_worst = std::max(P.l_worst, total);
    const double win = (now - P.win_start) / freq;
    if (win >= 1.0) {
        const double n = std::max(1, P.frames);
        P.fps = P.frames / win;
        P.lps = P.logic / win;
        P.a_input = P.t_input / n; P.a_update = P.t_update / n;
        P.a_render = P.t_render / n; P.a_present = P.t_present / n;
        P.a_worst = P.worst;
        P.frames = P.logic = 0;
        P.t_input = P.t_update = P.t_render = P.t_present = P.worst = 0;
        P.win_start = now;
    }
    const double lwin = (now - P.log_start) / freq;
    if (P.log && lwin >= 5.0) {
        const double n = std::max(1, P.log_frames);
        SDL_Log("[perf] fps=%.1f logic=%.1f/s input=%.2fms update=%.2fms render=%.2fms present=%.2fms "
                "worst=%.1fms canvas=%dx%d",
                P.log_frames / lwin, P.log_logic / lwin, P.l_input / n, P.l_update / n, P.l_render / n,
                P.l_present / n, P.l_worst, g_ui_w, g_ui_h);
        P.log_frames = P.log_logic = 0;
        P.l_input = P.l_update = P.l_render = P.l_present = P.l_worst = 0;
        P.log_start = now;
    }
}

// Bottom-left, above the touch mode banner's zone (the bottom 60 px) so it
// never covers the EVA / objective lines at the top of the battlefield; W6-01-16:
// right of the touch command column's two 52-px columns (x 4..108), which it
// used to cover.
void perf_draw(SDL_Renderer* ren, int W, int H) {
    if (!g_perf.overlay) return;
    char line[160];
    std::snprintf(line, sizeof line, "FPS %.0f  LOGIC %.1f/S  UPD %.1f  REN %.1f  PRES %.1f  MAX %.0f MS",
                  g_perf.fps, g_perf.lps, g_perf.a_update, g_perf.a_render, g_perf.a_present, g_perf.a_worst);
    const int tw = static_cast<int>(std::strlen(line)) * 6 * 2;
    (void)W;
    const int x = 118, y = H - 86;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0xB0);
    const SDL_Rect bg{x - 6, y - 3, tw + 12, 20};
    SDL_RenderFillRect(ren, &bg);
    const bool slow = g_perf.fps > 0 && g_perf.fps < 25.0;
    draw_text(ren, line, x, y, 2, slow ? 255 : 140, slow ? 150 : 240, slow ? 110 : 140);
}

// ---------------------------------------------------------------------------
// R-184: input record / replay.
std::string g_record_path;   // --record <file> / RA2_RECORD
std::string g_replay_path;   // --replay <file>
bool g_replay_hidden = false;

struct InputRecorder {
    std::ofstream out;
    std::vector<ra2engine::InputEvent> frame_events;
    bool open(const std::string& path) {
        out.open(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << "RA2REC 1 " << RA2_VERSION << "\n";
        out << "# F <frame> <dt seconds>, then that frame's events: E kind x y button flags t key vk dx dy x0 y0 text\n";
        return true;
    }
    void event(const ra2engine::InputEvent& e) { frame_events.push_back(e); }
    void frame(int f, double dt) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "F %d %.17g\n", f, dt);
        out << buf;
        for (const ra2engine::InputEvent& e : frame_events) {
            const int flags = (e.shift ? 1 : 0) | (e.ctrl ? 2 : 0) | (e.alt ? 4 : 0) | (e.touch ? 8 : 0) |
                              (e.system_back ? 16 : 0) | (e.hold ? 32 : 0);
            std::string hex;
            for (const char* p = e.text; *p; ++p) {
                char h[3];
                std::snprintf(h, sizeof h, "%02x", static_cast<unsigned char>(*p));
                hex += h;
            }
            out << "E " << static_cast<int>(e.kind) << ' ' << e.x << ' ' << e.y << ' ' << static_cast<int>(e.button)
                << ' ' << flags << ' ' << e.t_ms << ' ' << e.key << ' ' << e.vk << ' ' << e.dx << ' ' << e.dy << ' '
                << e.x0 << ' ' << e.y0 << ' ' << (hex.empty() ? "-" : hex) << "\n";
        }
        frame_events.clear();
    }
};

// Plays a recording back: frame f's events and time step.
struct InputReplay : ra2engine::EventSource {
    struct Frame { double dt = 0; std::vector<ra2engine::InputEvent> events; };
    std::vector<Frame> frames;
    int cur = -1;          // the frame being polled
    std::size_t next = 0;  // next event in it
    bool load(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::string line;
        if (!std::getline(in, line) || line.rfind("RA2REC 1", 0) != 0) return false;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ls(line);
            char tag = 0;
            ls >> tag;
            if (tag == 'F') {
                int f = 0;
                Frame fr;
                ls >> f >> fr.dt;
                if (f != static_cast<int>(frames.size())) return false;
                frames.push_back(std::move(fr));
            } else if (tag == 'E' && !frames.empty()) {
                ra2engine::InputEvent e;
                int kind = 0, button = 0, flags = 0;
                std::string hex;
                ls >> kind >> e.x >> e.y >> button >> flags >> e.t_ms >> e.key >> e.vk >> e.dx >> e.dy >> e.x0 >> e.y0 >> hex;
                if (!ls) return false;
                e.kind = static_cast<ra2engine::InputEvent::Kind>(kind);
                e.button = static_cast<ra2engine::InputEvent::Button>(button);
                e.shift = flags & 1; e.ctrl = flags & 2; e.alt = flags & 4;
                e.touch = flags & 8; e.system_back = flags & 16; e.hold = flags & 32;
                if (hex != "-")
                    for (std::size_t i = 0; i + 1 < hex.size() && i / 2 + 1 < sizeof e.text; i += 2)
                        e.text[i / 2] = static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
                frames.back().events.push_back(e);
            }
        }
        return !frames.empty();
    }
    void begin_frame(int f) { cur = f; next = 0; }
    bool done() const { return cur >= static_cast<int>(frames.size()); }
    double dt() const { return cur >= 0 && cur < static_cast<int>(frames.size()) ? frames[cur].dt : 1.0 / 60.0; }
    bool poll(ra2engine::InputEvent& ev) override {
        if (cur < 0 || cur >= static_cast<int>(frames.size())) return false;
        const Frame& fr = frames[cur];
        if (next >= fr.events.size()) return false;
        ev = fr.events[next++];
        return true;
    }
};

// The end-of-run fingerprint both runs print: the scene's integer probe.
template <class S> void replay_report(const char* what, int frame, S* scene) {
    std::map<std::string, long long> pr;
    if (scene) scene->probe(pr);
    std::uint64_t h = 1469598103934665603ull;
    for (const auto& [k, v] : pr) {
        for (char c : k) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; }
        for (int i = 0; i < 8; ++i) { h ^= static_cast<unsigned char>(static_cast<unsigned long long>(v) >> (8 * i)); h *= 1099511628211ull; }
    }
    char hb[200];
    std::snprintf(hb, sizeof hb, "[%s] end frame=%d scene=%s probe_hash=%016llx keys=%zu", what, frame,
                  scene ? scene->name() : "none", static_cast<unsigned long long>(h), pr.size());
    SDL_Log("%s", hb);
    std::cerr << hb << "\n";
}

// ---------------------------------------------------------------------------
// NET L1 (R-184 reworked): COMMAND-LOG record / replay. `--record <file>`
// writes the game scene's config, crate seed and canvas, then every command
// the scene applied with its logic tick, then "END <tick> <state hash>".
// `--replay <file>` (an RA2CMD file) rebuilds the scene from that config and
// re-applies the log tick by tick (no input, no menus): the same format a
// lockstep peer applies, so it is portable across devices. The raw input
// recorder above stays reachable as `--record-input` (RA2REC files replay
// with `--replay` as before).
std::string g_cmd_record_path;   // --record <file> / RA2_RECORD
std::int64_t g_crate_seed_override = -1;   // replay: the recorded skirmish crate seed
std::uint32_t g_crate_seed_used = 0;       // the seed enable_p5_features drew

struct CmdRecorder {
    std::ofstream out;
    bool open(const std::string& path, const ra2engine::SkirmishConfig& cfg, std::uint32_t seed, int cw, int ch) {
        out.open(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << "RA2CMD 1 " << RA2_VERSION << "\n";
        out << "# CFG <hex SkirmishConfig>, SEED <crate seed>, CANVAS <w> <h>, then C lines (command_to_line), END <tick> <hash>\n";
        ra2engine::SkirmishConfig c = cfg;
        const std::vector<std::uint8_t> b = ra2engine::save::to_bytes(c);
        out << "CFG ";
        char hx[3];
        for (std::uint8_t v : b) { std::snprintf(hx, sizeof hx, "%02x", v); out << hx; }
        out << "\nSEED " << seed << "\nCANVAS " << cw << ' ' << ch << "\n";
        return true;
    }
    void command(const ra2engine::Command& c) { out << ra2engine::command_to_line(c) << "\n"; }
    // MR2 (RA2_REC_CHECK=N): a state-hash checkpoint every N ticks, so a
    // replay that diverges names the first differing tick ("H <tick> <hash>").
    void check(std::uint32_t tick, std::uint64_t hash) {
        char b[64];
        std::snprintf(b, sizeof b, "H %u %016llx\n", tick, static_cast<unsigned long long>(hash));
        out << b;
    }
    void end(std::uint32_t tick, std::uint64_t hash) {
        char b[64];
        std::snprintf(b, sizeof b, "END %u %016llx\n", tick, static_cast<unsigned long long>(hash));
        out << b;
        out.flush();
    }
};

struct CmdLog {
    ra2engine::SkirmishConfig cfg;
    std::uint32_t seed = 0;
    int canvas_w = 1280, canvas_h = 720;
    std::vector<ra2engine::Command> cmds;
    std::uint32_t end_tick = 0;
    std::string end_hash;
    std::vector<std::pair<std::uint32_t, std::string>> checks;   // MR2: H lines
    bool load(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        std::string line;
        if (!std::getline(in, line) || line.rfind("RA2CMD 1", 0) != 0) return false;
        bool have_cfg = false;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (line.rfind("CFG ", 0) == 0) {
                std::vector<std::uint8_t> b;
                for (std::size_t i = 4; i + 1 < line.size(); i += 2)
                    b.push_back(static_cast<std::uint8_t>(std::stoi(line.substr(i, 2), nullptr, 16)));
                have_cfg = ra2engine::save::from_bytes(b, cfg);
            } else if (line.rfind("SEED ", 0) == 0) {
                seed = static_cast<std::uint32_t>(std::stoul(line.substr(5)));
            } else if (line.rfind("CANVAS ", 0) == 0) {
                std::istringstream ls(line.substr(7));
                ls >> canvas_w >> canvas_h;
            } else if (line.rfind("END ", 0) == 0) {
                std::istringstream ls(line.substr(4));
                ls >> end_tick >> end_hash;
            } else if (line.rfind("H ", 0) == 0) {
                std::istringstream ls(line.substr(2));
                std::uint32_t t = 0;
                std::string hh;
                if (ls >> t >> hh) checks.emplace_back(t, hh);
            } else if (line[0] == 'C') {
                ra2engine::Command c;
                if (!ra2engine::command_from_line(line, c)) return false;
                cmds.push_back(std::move(c));
            }
        }
        return have_cfg;
    }
};
inline bool is_cmd_log(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::string line;
    return in && std::getline(in, line) && line.rfind("RA2CMD", 0) == 0;
}
