#pragma once
// G0 verification harness (--play): the pure, SDL-free half -- the script
// parser, the assertion evaluator over a scene probe, and the play_hash.
// The driver half (scripted events through the REAL scene tree) lives in
// engine/play_driver.inc, included by main.cpp.
//
// Script format: one command per line, '#' starts a comment, blank lines are
// ignored. Every INPUT command (click/drag/key/move/select) ends the current
// frame, so the scene sees its events and gets one update/render before the
// next command runs (a page/scene transition is therefore always settled).
//
//   wait N                         advance N frames
//   click x y [right]              click window pixel (x, y)
//   click button <Name...>         a shell/lobby/game button by its label
//   click cell cx cy [right]       the centre of map cell (cx, cy)
//   click cameo <ID>               a sidebar cameo by rules id (switches tab)
//   click unit H:IMAGE[:n] [right] the n-th (0-based) alive unit of house H
//   select unit H:IMAGE[:n]        = click unit (left)
//   drag x1 y1 x2 y2               left-drag in window pixels
//   drag cell cx1 cy1 cx2 cy2      left-drag between two cells
//   move x y                       pointer move only
//   key <name> [ctrl] [shift] [alt]  SDL key name ("Escape", "S", "1", "F10")
//   shot <file.png>                capture the frame (relative to --play-out)
//   assert <key> <op> <int>        op: == != < <= > >=  (probe key, see below)
//   until N <key> <op> <int>       wait up to N frames for the condition
//   cheat <args...>                scene cheat (GameScene: kill_house H)
//   expect quit                    a later Quit/exit is the expected result
//   end                            stop; PASS if no assertion failed
//
// Probe keys are scene-defined integers (e.g. "scene.game", "menu", "sel",
// "units.0", "units.0.GI", "credits"); a missing key evaluates as 0.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ra2engine {
namespace play {

struct Command {
    std::string op;                 // "wait", "click", ...
    std::vector<std::string> args;  // whitespace-split remainder
    int line = 0;                   // 1-based source line
};

using Probe = std::map<std::string, long long>;

// Parse a script. Unknown ops are a parse error (err = "line N: ..."), so a
// typo never silently passes.
bool parse_script(const std::string& text, std::vector<Command>& out, std::string& err);

// Evaluate "<key> <op> <int>" (three tokens) against a probe. Returns false
// on a malformed expression (err set); `result` is the comparison outcome.
bool eval_assert(const std::vector<std::string>& expr, const Probe& p, bool& result,
                 std::string& err);

// One probe as a stable text line: "key=value key=value ..." (map order).
std::string probe_line(const Probe& p);

// 64-bit FNV-1a, chained (play_hash folds every printed play_state line).
uint64_t fnv1a(const std::string& s, uint64_t h = 1469598103934665603ULL);

// Parse "H:IMAGE[:n]" (unit selector). Returns false when malformed.
bool parse_unit_selector(const std::string& s, int& house, std::string& image, int& n);

}  // namespace play
}  // namespace ra2engine

// ===========================================================================
// Agent G: the `--play` script LANGUAGE (namespace ra2play) -- a superset of
// the command set above (also accepts its forms), with expressions over a
// typed probe, @G-ID tags, until/cycle/if/let, and missing-key reporting.
// The front end that runs it is engine/play_driver.inc; the format is
// documented in scripts/play/README.md; tests in tests/test_play_lang.cpp.
// ===========================================================================
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ra2play {

// One script line, tokenised. `op` is the first word (lower-cased); `args` the
// remaining words (a "double quoted" run is one arg, quotes stripped); `tags`
// collects the trailing `@G-20`/`@owner:A` annotations; `expr` is the raw
// expression text for assert/until/require/let.
struct Cmd {
    int line = 0;
    std::string op;
    std::vector<std::string> args;
    std::vector<std::string> tags;
    std::string expr;
    int n = 0;          // until N / wait N / hold KEY N
    std::string raw;    // the source line (trimmed, without the comment)
    std::vector<Cmd> sub;   // `if EXPR then CMD`: the guarded command (size 1)
};

struct Script {
    std::string name;
    std::vector<Cmd> cmds;
};

// Parse a whole script. Returns false and sets `err` ("line N: ...") on the
// first malformed line. Unknown ops are a parse error (typos must not silently
// pass a run).
bool parse_script(const std::string& text, Script& out, std::string& err);

// ---- probe -----------------------------------------------------------------
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    int cx() const { return x + w / 2; }
    int cy() const { return y + h / 2; }
};

// The flat state a scene reports. Keys are `name` or `name(arg,arg)`. A key
// whose family (the part before '(') is in `zero_families` reads 0 when absent
// (e.g. `units(1,E2)` with no E2 alive); any other absent key is MISSING --
// the run reports `missing-probe:<key>` instead of crashing, so scripts written
// for a later phase degrade gracefully on an older tree.
struct ProbeData {
    std::map<std::string, double> n;
    std::map<std::string, std::string> s;
    std::set<std::string> zero_families;
    void set(const std::string& k, double v) { n[k] = v; }
    void set_s(const std::string& k, const std::string& v) { s[k] = v; }
    void add(const std::string& k, double v) { n[k] += v; }
};

// What a Scene hands the harness each time it is asked (Scene::play_probe).
// `targets` are named clickable rects in window pixels ("button:Skirmish",
// "start", "cameo:GAPOWR", "tab:2", "menu:SAVE GAME", "dialog:action"). The
// hooks are optional (null when the scene has no world).
struct Probe : ProbeData {
    std::string scene = "unknown";
    std::map<std::string, Rect> targets;
    // Map cell centre -> window pixel (false when off the playfield view).
    std::function<bool(double cx, double cy, int& sx, int& sy)> cell_to_screen;
    // Scroll the view so cell (cx,cy) is centred (camera pan, as the arrow keys would).
    std::function<void(double cx, double cy)> center_on;
    // "H:TYPE[:n]" -> the n-th (0-based) live unit / building of that house+type.
    std::function<bool(const std::string& spec, double& cx, double& cy)> find_unit;
    std::function<bool(const std::string& spec, double& cx, double& cy)> find_building;
    // P4: the building's foundation (origin + size), so a click can fall back
    // to another of its cells when the centre cannot be brought on screen.
    std::function<bool(const std::string& spec, int& ox, int& oy, int& fw, int& fh)> find_building_rect;
    // The nearest valid origin cell for the currently armed placement.
    std::function<bool(int& cx, int& cy)> auto_place;
    // P4: `place near X Y` -- the same search anchored on cell (ax,ay) instead of
    // the player's first building (a human places next to the yard they look at).
    std::function<bool(int ax, int ay, int& cx, int& cy)> auto_place_near;
    // Named map points ("bridge", "bridge_near", "bridge_far", "garrison", "hut").
    std::function<bool(const std::string& name, double& cx, double& cy)> find_point;
    // Dev cheats: returns "" on success or an error string.
    std::function<std::string(const std::vector<std::string>& args)> cheat;
};

// ---- expressions -----------------------------------------------------------
struct Value {
    bool is_num = true;
    double num = 0.0;
    std::string str;
};

enum class Eval { True, False, Missing, Error };

// `expr` := clause { ("and"|"or") clause }   (left to right, no precedence)
// clause := operand OP operand | operand        (bare operand: non-zero / non-empty)
// OP     := == != < <= > >=
// operand:= number | "string" | $var | probe-key | bare word
// A bare word that is not a probe key is a string literal on the right-hand
// side and MISSING on the left. `detail` gets a human-readable account
// ("credits=10000 > 10000").
Eval eval_expr(const std::string& expr, const ProbeData& p,
               const std::map<std::string, Value>& vars, std::string& detail);

// Resolve one operand (exposed for `let`). Returns false when missing.
bool resolve_operand(const std::string& tok, const ProbeData& p,
                     const std::map<std::string, Value>& vars, bool lhs, Value& out);

// Deterministic FNV-1a over a probe dump (sorted keys; numbers printed %.6g).
std::uint64_t probe_hash(const ProbeData& p, std::uint64_t seed);
std::string probe_dump(const ProbeData& p);

}  // namespace ra2play
