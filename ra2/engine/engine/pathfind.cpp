#include "pathfind.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <limits>
#include <queue>
#include <unordered_map>

namespace ra2engine {

namespace {

// 8 directions in a fixed, stable order: 4 cardinal first (E, W, S, N), then
// 4 diagonal (SE, NE, SW, NW).
// W7-11-08 [RE: A* FUN_00429A10, step cost FUN_004297B0, cost table
// DAT_0081770C, tie-break DAT_0081772C]: every step costs the Can_Enter
// result's cost (1 for a clear cell) PLUS a per-direction tie-break -- N .001,
// NE .005, E .002, SE .006, S .003, SW .007, W .004, NW .008 -- so a diagonal
// step costs the same as an orthogonal one (no sqrt2) and only the tie-break
// prefers the orthogonals. Scaled x1000 to integers: cost*1000 + tie-break.
constexpr int kDx[8]   = { 1, -1, 0, 0, 1, 1, -1, -1 };
constexpr int kDy[8]   = { 0, 0, 1, -1, 1, -1, 1, -1 };
constexpr int kTie[8]  = { 2, 4, 3, 1, 6, 5, 7, 8 };   // E W S N SE NE SW NW
constexpr long long kUnit = 1000;

// [RE: FUN_00429A10] the heuristic is the straight-line (Euclidean) distance
// in cells, x1000.
inline long long euclid_h(int x, int y, Cellxy goal) {
    const double dx = static_cast<double>(x - goal.x), dy = static_cast<double>(y - goal.y);
    return static_cast<long long>(om::ftol(om::sqrt(dx * dx + dy * dy) * static_cast<double>(kUnit)));
}

// Node kept in the open set's binary heap.
struct OpenNode {
    long long f;     // g + h
    long long h;     // heuristic (used only for tie-breaking)
    int cell;        // y*width+x (used only for tie-breaking, for full determinism)
};

// Min-heap by f, tie-broken by lower h, then by lower cell index -- makes the
// expansion order fully deterministic regardless of insertion order/heap
// implementation details.
struct OpenNodeGreater {
    bool operator()(const OpenNode& a, const OpenNode& b) const {
        if (a.f != b.f) return a.f > b.f;
        if (a.h != b.h) return a.h > b.h;
        return a.cell > b.cell;
    }
};

// W2-SK-16: per-thread scratch reused across calls with a generation stamp
// (was three full-grid allocations per call). A cell whose stamp != gen reads
// as g = inf, came_from = -1, open (not closed) -- identical semantics.
struct Scratch {
    std::vector<long long> g;
    std::vector<int> came;
    std::vector<std::uint32_t> stamp;
    std::vector<std::uint8_t> closed;
    std::uint32_t gen = 0;
    void prepare(int n) {
        if ((int)stamp.size() != n) {
            g.assign((size_t)n, 0);
            came.assign((size_t)n, -1);
            stamp.assign((size_t)n, 0);
            closed.assign((size_t)n, 0);
            gen = 0;
        }
        if (++gen == 0) {   // wrapped: reset every stamp once
            std::fill(stamp.begin(), stamp.end(), 0u);
            gen = 1;
        }
    }
};
thread_local Scratch t_scratch;

enum class Mode { Exact, Nearest };

// The one A* body. Exact: the historical find_path (empty when unreachable, or
// when a budget > 0 runs out). Nearest: when the goal is not reached, the path
// to the closed cell nearest the goal (lowest h, then lowest g, then index).
std::vector<Cellxy> astar(const std::vector<uint8_t>& passable, int width, int height,
                          Cellxy start, Cellxy goal, int max_expand, Mode mode,
                          const std::vector<uint8_t>* leak, const std::vector<std::uint16_t>* cost) {
    if (cost && cost->size() != passable.size()) cost = nullptr;
    if (leak && leak->size() != passable.size()) leak = nullptr;
    const int n = width * height;
    auto index_of = [&](int x, int y) { return y * width + x; };
    auto in_range = [&](int x, int y) { return x >= 0 && x < width && y >= 0 && y < height; };
    const int start_idx = index_of(start.x, start.y);
    const int goal_idx = index_of(goal.x, goal.y);
    const long long kInf = std::numeric_limits<long long>::max();
    auto octile_h = [&](int x, int y) { return euclid_h(x, y, goal); };
    Scratch& S = t_scratch;
    S.prepare(n);
    const std::uint32_t gen = S.gen;
    auto touch = [&](int c) {
        if (S.stamp[(size_t)c] != gen) {
            S.stamp[(size_t)c] = gen;
            S.g[(size_t)c] = kInf;
            S.came[(size_t)c] = -1;
            S.closed[(size_t)c] = 0;
        }
    };
    auto gval = [&](int c) { return S.stamp[(size_t)c] == gen ? S.g[(size_t)c] : kInf; };
    auto is_closed = [&](int c) { return S.stamp[(size_t)c] == gen && S.closed[(size_t)c] != 0; };

    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenNodeGreater> open;
    touch(start_idx);
    S.g[(size_t)start_idx] = 0;
    const long long h_start = octile_h(start.x, start.y);
    open.push(OpenNode{ h_start, h_start, start_idx });
    int best_cell = start_idx;
    long long best_h = h_start, best_g = 0;
    int expanded = 0;
    bool reached = false;
    while (!open.empty()) {
        OpenNode cur = open.top();
        open.pop();
        if (is_closed(cur.cell)) continue;   // stale entry
        if (cur.cell == goal_idx) { reached = true; break; }   // consistent heuristic -> optimal
        S.closed[(size_t)cur.cell] = 1;
        if (mode == Mode::Nearest) {
            const long long gc = S.g[(size_t)cur.cell];
            if (cur.h < best_h ||
                (cur.h == best_h && (gc < best_g || (gc == best_g && cur.cell < best_cell)))) {
                best_h = cur.h;
                best_g = gc;
                best_cell = cur.cell;
            }
        }
        if (max_expand > 0 && ++expanded >= max_expand) break;
        const int cx = cur.cell % width;
        const int cy = cur.cell / width;
        for (int dir = 0; dir < 8; ++dir) {
            const int nx = cx + kDx[dir];
            const int ny = cy + kDy[dir];
            if (!in_range(nx, ny)) continue;
            const int ncell = index_of(nx, ny);
            if (!passable[(size_t)ncell]) continue;
            if (is_closed(ncell)) continue;
            // Block diagonal corner-cutting: both orthogonal "corner" cells
            // must be passable for a diagonal move to be allowed.
            // W5-A: a corner cell blocked only by a wall overlay (`leak`) does
            // not block the diagonal -- walls touching only at their corners
            // leave a gap [RE-FALLBACK: the original's corner rule is untraced;
            // map evidence: ALL12's Tesla Reactor fence has exactly such a gap at
            // the ParaSEALS drop point, and the reactor (objective 1) is
            // unreachable otherwise].
            if (kDx[dir] != 0 && kDy[dir] != 0) {
                const int c1 = index_of(cx + kDx[dir], cy), c2 = index_of(cx, cy + kDy[dir]);
                if (!passable[(size_t)c1] && !(leak && (*leak)[(size_t)c1])) continue;
                if (!passable[(size_t)c2] && !(leak && (*leak)[(size_t)c2])) continue;
            }
            const long long step = (cost ? static_cast<long long>((*cost)[(size_t)ncell]) : 1) * kUnit + kTie[dir];
            const long long tentative_g = S.g[(size_t)cur.cell] + step;
            if (tentative_g < gval(ncell)) {
                touch(ncell);
                S.g[(size_t)ncell] = tentative_g;
                S.came[(size_t)ncell] = cur.cell;
                const long long h = octile_h(nx, ny);
                open.push(OpenNode{ tentative_g + h, h, ncell });
            }
        }
    }
    int end_cell;
    if (reached) end_cell = goal_idx;
    else if (mode == Mode::Exact) return {};
    else end_cell = best_cell;
    std::vector<Cellxy> path;
    for (int cell = end_cell; cell >= 0; cell = S.came[(size_t)cell]) {
        path.push_back(Cellxy{ cell % width, cell / width });
        if (cell == start_idx) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// R-210: is cell `c` an open gate for this mover?
bool gate_open(const PathTerrain* t, int c) {
    return t && t->open_cells && std::binary_search(t->open_cells->begin(), t->open_cells->end(), c);
}

// R-210: the height-aware A* over (cell, layer) states [RE: FUN_00429A10 keeps
// a ground and a bridge ("alt") plane of visit marks / distances and asks
// IsCellOccupied -> the height gate FUN_004D9B70 for every neighbour]. Same
// costs, heuristic, tie-breaks and corner rule as astar(); a state is
// cell + layer * n.
Scratch& terrain_scratch() {
    thread_local Scratch s;
    return s;
}

std::vector<Cellxy> astar_terrain(const std::vector<uint8_t>& passable, int width, int height,
                                  Cellxy start, Cellxy goal, int max_expand, Mode mode,
                                  const std::vector<uint8_t>* leak, const PathTerrain& t,
                                  const std::vector<std::uint16_t>* cost) {
    if (cost && cost->size() != passable.size()) cost = nullptr;
    if (leak && leak->size() != passable.size()) leak = nullptr;
    const int n = width * height;
    const bool decks = t.deck && t.deck->size() == passable.size();
    const int layers = decks ? 2 : 1;
    auto base_ok = [&](int c) { return passable[(size_t)c] != 0 || gate_open(&t, c); };
    auto deck_of = [&](int c) { return decks ? (*t.deck)[(size_t)c] : 0; };
    auto state_ok = [&](int c, int layer) {
        if (!base_ok(c)) return false;
        return layer == 0 ? deck_of(c) != 2 : deck_of(c) != 0;
    };
    auto in_range = [&](int x, int y) { return x >= 0 && x < width && y >= 0 && y < height; };
    const long long kInf = std::numeric_limits<long long>::max();
    auto octile_h = [&](int x, int y) { return euclid_h(x, y, goal); };
    const int start_cell = start.y * width + start.x;
    const int goal_cell = goal.y * width + goal.x;
    int start_layer = (decks && t.start_layer == 1 && deck_of(start_cell) != 0) ? 1 : 0;
    if (deck_of(start_cell) == 2) start_layer = 1;
    const int start_idx = start_cell + start_layer * n;
    Scratch& S = terrain_scratch();
    S.prepare(n * layers);
    const std::uint32_t gen = S.gen;
    auto touch = [&](int c) {
        if (S.stamp[(size_t)c] != gen) {
            S.stamp[(size_t)c] = gen;
            S.g[(size_t)c] = kInf;
            S.came[(size_t)c] = -1;
            S.closed[(size_t)c] = 0;
        }
    };
    auto gval = [&](int c) { return S.stamp[(size_t)c] == gen ? S.g[(size_t)c] : kInf; };
    auto is_closed = [&](int c) { return S.stamp[(size_t)c] == gen && S.closed[(size_t)c] != 0; };

    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenNodeGreater> open;
    touch(start_idx);
    S.g[(size_t)start_idx] = 0;
    const long long h_start = octile_h(start.x, start.y);
    open.push(OpenNode{ h_start, h_start, start_idx });
    int best_state = start_idx;
    long long best_h = h_start, best_g = 0;
    int expanded = 0;
    int reached_state = -1;
    while (!open.empty()) {
        OpenNode cur = open.top();
        open.pop();
        if (is_closed(cur.cell)) continue;   // stale entry
        const int cc = cur.cell % n, cl = cur.cell / n;
        if (cc == goal_cell) { reached_state = cur.cell; break; }
        S.closed[(size_t)cur.cell] = 1;
        if (mode == Mode::Nearest) {
            const long long gc = S.g[(size_t)cur.cell];
            if (cur.h < best_h ||
                (cur.h == best_h && (gc < best_g || (gc == best_g && cur.cell < best_state)))) {
                best_h = cur.h;
                best_g = gc;
                best_state = cur.cell;
            }
        }
        if (max_expand > 0 && ++expanded >= max_expand) break;
        const int cx = cc % width;
        const int cy = cc / width;
        for (int dir = 0; dir < 8; ++dir) {
            const int nx = cx + kDx[dir];
            const int ny = cy + kDy[dir];
            if (!in_range(nx, ny)) continue;
            const int ncell = ny * width + nx;
            if (!base_ok(ncell)) continue;
            if (kDx[dir] != 0 && kDy[dir] != 0) {
                const int c1 = cy * width + cx + kDx[dir], c2 = (cy + kDy[dir]) * width + cx;
                if (!base_ok(c1) && !(leak && (*leak)[(size_t)c1])) continue;
                if (!base_ok(c2) && !(leak && (*leak)[(size_t)c2])) continue;
            }
            const int nl = path_step_layer(t, width, cx, cy, cl, nx, ny);
            if (nl < 0 || nl >= layers || !state_ok(ncell, nl)) continue;
            const int nstate = ncell + nl * n;
            if (is_closed(nstate)) continue;
            const long long step = (cost ? static_cast<long long>((*cost)[(size_t)ncell]) : 1) * kUnit + kTie[dir];
            const long long tentative_g = S.g[(size_t)cur.cell] + step;
            if (tentative_g < gval(nstate)) {
                touch(nstate);
                S.g[(size_t)nstate] = tentative_g;
                S.came[(size_t)nstate] = cur.cell;
                const long long h = octile_h(nx, ny);
                open.push(OpenNode{ tentative_g + h, h, nstate });
            }
        }
    }
    int end_state;
    if (reached_state >= 0) end_state = reached_state;
    else if (mode == Mode::Exact) return {};
    else end_state = best_state;
    std::vector<Cellxy> path;
    for (int s = end_state; s >= 0; s = S.came[(size_t)s]) {
        const int c = s % n;
        path.push_back(Cellxy{ c % width, c / width });
        if (s == start_idx) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// R-171 (W5-F): a result memo for astar. astar is a PURE function of (the
// grid's bytes, width, height, start, goal, budget, mode) -- the scratch is
// internal -- so returning a remembered path for an identical query is
// byte-identical by construction. It matters for units whose goal is
// unreachable: step_combat re-requests their approach every logic frame and
// each request expanded the full 20000-node budget (the ALL03 opening spent
// 86% of its CPU here). Each memo is validated against a byte copy of the
// grid it was built on (memcmp per call, ~1 us), so any passability change
// (a building placed or destroyed, a bridge) drops the stale results.
// Net L0 (N4-11): every input is identified by CONTENT, never by address. The
// memo used to key the leak mask / level / deck grids by pointer (and the deck
// by a version counter), and it is thread_local, outliving a World: after a
// resync load_world, a rematch, or a freed-then-reused address, a pointer key
// could serve a path computed on other terrain -- and then the peer that
// loaded state would diverge from peers that did not. Now each memo keeps a
// byte copy of every grid its paths were built on (grid, leak, level, slope,
// deck) and drops all paths when any of them differs; the key holds only
// value fields.
struct MemoKey {
    int sx, sy, gx, gy, budget, mode;
    bool leak = false;      // a leak mask was in use (its bytes: PathMemo::leak)
    bool terrain = false;   // R-210 layer-aware search (bytes: PathMemo::level/slope/deck)
    bool deck = false;
    int tlayer = 0;
    std::uint64_t gates = 0;   // the open gates, by cell content
    bool operator==(const MemoKey& o) const {
        return sx == o.sx && sy == o.sy && gx == o.gx && gy == o.gy && budget == o.budget && mode == o.mode &&
               leak == o.leak && terrain == o.terrain && deck == o.deck && tlayer == o.tlayer && gates == o.gates;
    }
};
struct MemoKeyHash {
    std::size_t operator()(const MemoKey& k) const {
        std::uint64_t h = 1469598103934665603ull;
        for (int v : {k.sx, k.sy, k.gx, k.gy, k.budget, k.mode, (int)k.leak, (int)k.terrain, (int)k.deck, k.tlayer}) {
            h ^= static_cast<std::uint32_t>(v);
            h *= 1099511628211ull;
        }
        h ^= k.gates;
        h *= 1099511628211ull;
        return static_cast<std::size_t>(h);
    }
};
// A content snapshot of one optional grid.
template <class T> struct GridSnap {
    bool set = false;
    std::vector<T> bytes;
    // True when `v` (null = absent) matches the snapshot; adopts it when unset.
    bool same_or_adopt(const std::vector<T>* v) {
        if (!v) return true;   // absent inputs do not constrain the memo
        if (!set) { set = true; bytes = *v; return true; }
        return bytes.size() == v->size() &&
               (v->empty() || std::memcmp(bytes.data(), v->data(), v->size() * sizeof(T)) == 0);
    }
    void reset() { set = false; bytes.clear(); }
};
struct PathMemo {
    const void* owner = nullptr;   // the grid vector last seen (a lookup hint only)
    int width = 0, height = 0;
    std::vector<uint8_t> grid;
    GridSnap<uint8_t> leak, slope, deck;
    GridSnap<std::int8_t> level;
    std::unordered_map<MemoKey, std::vector<Cellxy>, MemoKeyHash> paths;
    void restart(const std::vector<uint8_t>& passable, int w, int h) {
        width = w;
        height = h;
        grid = passable;
        leak.reset(); slope.reset(); deck.reset(); level.reset();
        paths.clear();
    }
};
constexpr std::size_t kMemos = 4;          // land + water grids of up to two Worlds
constexpr std::size_t kMemoPaths = 8192;   // entries per grid before a flush
thread_local PathMemo t_memo[kMemos];
thread_local std::size_t t_memo_next = 0;

PathMemo& memo_for(const std::vector<uint8_t>& passable, int width, int height) {
    auto same = [&](const PathMemo& m) {
        return m.width == width && m.height == height && m.grid.size() == passable.size() &&
               (passable.empty() || std::memcmp(m.grid.data(), passable.data(), passable.size()) == 0);
    };
    for (PathMemo& m : t_memo)
        if (m.owner == &passable) {
            if (!same(m)) m.restart(passable, width, height);   // this grid changed: start over on its new bytes
            return m;
        }
    for (PathMemo& m : t_memo)
        if (m.owner && same(m)) { m.owner = &passable; return m; }
    PathMemo& m = t_memo[t_memo_next++ % kMemos];
    m.owner = &passable;
    m.restart(passable, width, height);
    return m;
}

// R-210: a usable terrain (null when absent or malformed -> the 2-D search).
const PathTerrain* terrain_of(const PathTerrain* t, const std::vector<uint8_t>& passable) {
    if (!t) return nullptr;
    if (!t->level || t->level->size() != passable.size()) {
        // Gates alone still need the terrain search (it reads open_cells).
        return (t->open_cells && !t->open_cells->empty()) ? t : nullptr;
    }
    return t;
}

std::uint64_t gates_hash(const PathTerrain* t) {
    if (!t || !t->open_cells) return 0;
    std::uint64_t h = 1469598103934665603ull;
    for (int c : *t->open_cells) {
        h ^= static_cast<std::uint32_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

std::vector<Cellxy> astar_memo(const std::vector<uint8_t>& passable, int width, int height, Cellxy start,
                               Cellxy goal, int max_expand, Mode mode, const std::vector<uint8_t>* leak,
                               const PathTerrain* t = nullptr, const std::vector<std::uint16_t>* cost = nullptr) {
    if (leak && leak->size() != passable.size()) leak = nullptr;
    // W7-11-08: an occupancy cost grid changes every frame -- never memoised.
    if (cost && cost->size() == passable.size())
        return t ? astar_terrain(passable, width, height, start, goal, max_expand, mode, leak, *t, cost)
                 : astar(passable, width, height, start, goal, max_expand, mode, leak, cost);
    PathMemo& m = memo_for(passable, width, height);
    // Net L0: the other inputs are validated by content too; any change drops
    // every remembered path of this grid (they are pure functions of it all).
    const bool same_terrain = m.leak.same_or_adopt(leak) &&
                              (!t || (m.level.same_or_adopt(t->level) && m.slope.same_or_adopt(t->slope) &&
                                      m.deck.same_or_adopt(t->deck)));
    if (!same_terrain) {
        m.restart(passable, width, height);
        m.leak.same_or_adopt(leak);
        if (t) {
            m.level.same_or_adopt(t->level);
            m.slope.same_or_adopt(t->slope);
            m.deck.same_or_adopt(t->deck);
        }
    }
    MemoKey k{start.x, start.y, goal.x, goal.y, max_expand, mode == Mode::Nearest ? 1 : 0};
    k.leak = leak != nullptr;
    if (t) {
        k.terrain = true;
        k.deck = t->deck != nullptr;
        k.tlayer = t->start_layer;
        k.gates = gates_hash(t);
    }
    const auto it = m.paths.find(k);
    if (it != m.paths.end()) return it->second;
    std::vector<Cellxy> r = t ? astar_terrain(passable, width, height, start, goal, max_expand, mode, leak, *t, nullptr)
                              : astar(passable, width, height, start, goal, max_expand, mode, leak, nullptr);
    if (m.paths.size() >= kMemoPaths) m.paths.clear();
    m.paths.emplace(k, r);
    return r;
}

bool basic_checks(const std::vector<uint8_t>& passable, int width, int height, Cellxy start,
                  Cellxy goal) {
    if (width <= 0 || height <= 0) return false;
    if (passable.size() != static_cast<size_t>(width) * static_cast<size_t>(height)) return false;
    auto in_range = [&](int x, int y) { return x >= 0 && x < width && y >= 0 && y < height; };
    return in_range(start.x, start.y) && in_range(goal.x, goal.y);
}

// A cell this mover may stand in at some layer (the grid, or an open gate).
bool cell_ok(const std::vector<uint8_t>& passable, const PathTerrain* t, int c) {
    return passable[(size_t)c] != 0 || gate_open(t, c);
}

}  // namespace

int path_step_layer(const PathTerrain& t, int width, int sx, int sy, int sl, int dx, int dy) {
    if (!t.level) return 0;
    const std::vector<std::int8_t>& lv = *t.level;
    const long long s = (long long)sy * width + sx, d = (long long)dy * width + dx;
    if (s < 0 || d < 0 || s >= (long long)lv.size() || d >= (long long)lv.size()) return -1;
    const int hs = lv[(size_t)s] + (sl == 1 ? 4 : 0);
    const int dk = (t.deck && (size_t)d < t.deck->size()) ? (*t.deck)[(size_t)d] : 0;
    if (dk != 2) {   // the ground of the next cell
        const int hd = lv[(size_t)d];
        if (hd == hs) return 0;
        // One level up or down only over a slope: the LOWER cell must be a
        // ramp [FUN_004D9B70: |diff| == 1 -> SlopeIndex (+0x11C) of the lower].
        if (sl != 1 && (hd - hs == 1 || hs - hd == 1)) {
            const long long lower = hd > hs ? s : d;
            if (t.slope && (size_t)lower < t.slope->size() && (*t.slope)[(size_t)lower] != 0) return 0;
        }
    }
    // A deck (Level + 4) entered at the same height: from the high ground at
    // its ends or from the next deck cell [FUN_004D9B70 |diff| == 4 cases].
    if (dk != 0 && lv[(size_t)d] + 4 == hs) return 1;
    return -1;
}

std::vector<Cellxy> find_path(const std::vector<uint8_t>& passable, int width, int height,
                              Cellxy start, Cellxy goal, const std::vector<uint8_t>* leak) {
    return find_path_t(passable, width, height, start, goal, leak, nullptr);
}

std::vector<Cellxy> find_path_bounded(const std::vector<uint8_t>& passable, int width, int height,
                                      Cellxy start, Cellxy goal, int max_expand,
                                      const std::vector<uint8_t>* leak) {
    return find_path_bounded_t(passable, width, height, start, goal, max_expand, leak, nullptr);
}

std::vector<Cellxy> find_path_nearest(const std::vector<uint8_t>& passable, int width, int height,
                                      Cellxy start, Cellxy goal, int max_expand,
                                      const std::vector<uint8_t>* leak) {
    return find_path_nearest_t(passable, width, height, start, goal, max_expand, leak, nullptr);
}

std::vector<Cellxy> find_path_t(const std::vector<uint8_t>& passable, int width, int height,
                                Cellxy start, Cellxy goal, const std::vector<uint8_t>* leak,
                                const PathTerrain* terrain) {
    if (!basic_checks(passable, width, height, start, goal)) return {};
    const PathTerrain* t = terrain_of(terrain, passable);
    if (!cell_ok(passable, t, start.y * width + start.x)) return {};
    if (!cell_ok(passable, t, goal.y * width + goal.x)) return {};
    if (start.x == goal.x && start.y == goal.y) return { start };
    return astar_memo(passable, width, height, start, goal, 0, Mode::Exact, leak, t);
}

std::vector<Cellxy> find_path_bounded_t(const std::vector<uint8_t>& passable, int width, int height,
                                        Cellxy start, Cellxy goal, int max_expand,
                                        const std::vector<uint8_t>* leak, const PathTerrain* terrain,
                                        const std::vector<std::uint16_t>* cost) {
    if (!basic_checks(passable, width, height, start, goal)) return {};
    const PathTerrain* t = terrain_of(terrain, passable);
    if (!cell_ok(passable, t, start.y * width + start.x)) return {};
    if (!cell_ok(passable, t, goal.y * width + goal.x)) return {};
    if (start.x == goal.x && start.y == goal.y) return { start };
    return astar_memo(passable, width, height, start, goal, max_expand, Mode::Exact, leak, t, cost);
}

std::vector<Cellxy> find_path_nearest_t(const std::vector<uint8_t>& passable, int width, int height,
                                        Cellxy start, Cellxy goal, int max_expand,
                                        const std::vector<uint8_t>* leak, const PathTerrain* terrain,
                                        const std::vector<std::uint16_t>* cost) {
    if (!basic_checks(passable, width, height, start, goal)) return {};
    const PathTerrain* t = terrain_of(terrain, passable);
    if (!cell_ok(passable, t, start.y * width + start.x)) return {};
    // Goal relaxation: an impassable goal becomes the nearest passable cell
    // (Chebyshev rings out to 16, Euclid-nearest per ring, scan order ties).
    if (!cell_ok(passable, t, goal.y * width + goal.x)) {
        bool found = false;
        for (int r = 1; r <= 16 && !found; ++r) {
            int best = -1;
            long long bd = 0;
            for (int y = goal.y - r; y <= goal.y + r; ++y)
                for (int x = goal.x - r; x <= goal.x + r; ++x) {
                    if (std::max(std::abs(x - goal.x), std::abs(y - goal.y)) != r) continue;
                    if (x < 0 || y < 0 || x >= width || y >= height) continue;
                    if (!cell_ok(passable, t, y * width + x)) continue;
                    const long long d = (long long)(x - goal.x) * (x - goal.x) +
                                        (long long)(y - goal.y) * (y - goal.y);
                    if (best < 0 || d < bd) { best = y * width + x; bd = d; }
                }
            if (best >= 0) { goal = Cellxy{ best % width, best / width }; found = true; }
        }
        if (!found) return { start };
    }
    if (start.x == goal.x && start.y == goal.y) return { start };
    return astar_memo(passable, width, height, start, goal, max_expand, Mode::Nearest, leak, t, cost);
}

}  // namespace ra2engine
