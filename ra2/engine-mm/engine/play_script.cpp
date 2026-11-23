#include "play_script.hpp"

#include <cstdlib>
#include <set>
#include <sstream>

namespace ra2engine {
namespace play {

namespace {
const std::set<std::string>& known_ops() {
    static const std::set<std::string> k = {"wait", "click", "select", "drag", "move", "key",
                                            "shot", "assert", "until", "cheat", "expect", "end"};
    return k;
}
bool to_int(const std::string& s, long long& v) {
    if (s.empty()) return false;
    char* e = nullptr;
    v = std::strtoll(s.c_str(), &e, 10);
    return e && *e == '\0';
}
}  // namespace

bool parse_script(const std::string& text, std::vector<Command>& out, std::string& err) {
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream ls(line);
        Command c;
        c.line = n;
        if (!(ls >> c.op)) continue;
        std::string a;
        while (ls >> a) c.args.push_back(a);
        if (!known_ops().count(c.op)) {
            err = "line " + std::to_string(n) + ": unknown command '" + c.op + "'";
            return false;
        }
        long long v = 0;
        if ((c.op == "wait") && (c.args.size() != 1 || !to_int(c.args[0], v) || v < 0)) {
            err = "line " + std::to_string(n) + ": wait needs one non-negative integer";
            return false;
        }
        if (c.op == "assert" && c.args.size() != 3) {
            err = "line " + std::to_string(n) + ": assert needs '<key> <op> <int>'";
            return false;
        }
        if (c.op == "until" && (c.args.size() != 4 || !to_int(c.args[0], v))) {
            err = "line " + std::to_string(n) + ": until needs 'N <key> <op> <int>'";
            return false;
        }
        if ((c.op == "click" || c.op == "drag" || c.op == "move" || c.op == "key" ||
             c.op == "shot" || c.op == "select" || c.op == "cheat" || c.op == "expect") &&
            c.args.empty()) {
            err = "line " + std::to_string(n) + ": " + c.op + " needs arguments";
            return false;
        }
        out.push_back(c);
    }
    return true;
}

bool eval_assert(const std::vector<std::string>& expr, const Probe& p, bool& result,
                 std::string& err) {
    if (expr.size() != 3) {
        err = "expression needs three tokens";
        return false;
    }
    long long rhs = 0;
    if (!to_int(expr[2], rhs)) {
        err = "right-hand side '" + expr[2] + "' is not an integer";
        return false;
    }
    const auto it = p.find(expr[0]);
    const long long lhs = (it == p.end()) ? 0 : it->second;
    const std::string& op = expr[1];
    if (op == "==") result = lhs == rhs;
    else if (op == "!=") result = lhs != rhs;
    else if (op == "<") result = lhs < rhs;
    else if (op == "<=") result = lhs <= rhs;
    else if (op == ">") result = lhs > rhs;
    else if (op == ">=") result = lhs >= rhs;
    else {
        err = "unknown operator '" + op + "'";
        return false;
    }
    return true;
}

std::string probe_line(const Probe& p) {
    std::string s;
    for (const auto& [k, v] : p) {
        if (!s.empty()) s += ' ';
        s += k + "=" + std::to_string(v);
    }
    return s;
}

uint64_t fnv1a(const std::string& s, uint64_t h) {
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

bool parse_unit_selector(const std::string& s, int& house, std::string& image, int& n) {
    const std::size_t a = s.find(':');
    if (a == std::string::npos || a == 0) return false;
    long long h = 0;
    if (!to_int(s.substr(0, a), h)) return false;
    const std::size_t b = s.find(':', a + 1);
    image = s.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
    if (image.empty()) return false;
    long long k = 0;
    if (b != std::string::npos && !to_int(s.substr(b + 1), k)) return false;
    house = static_cast<int>(h);
    n = static_cast<int>(k);
    return true;
}

}  // namespace play
}  // namespace ra2engine

// ===== Agent G: the --play script language (namespace ra2play) =====

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace ra2play {
namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Strip a '#' comment that is not inside double quotes.
std::string strip_comment(const std::string& s) {
    bool q = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"') q = !q;
        else if (s[i] == '#' && !q) return s.substr(0, i);
    }
    return s;
}

// Whitespace split; a "quoted run" is one token (quotes stripped).
std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i >= s.size()) break;
        if (s[i] == '"') {
            const std::size_t e = s.find('"', i + 1);
            out.push_back(s.substr(i + 1, (e == std::string::npos ? s.size() : e) - i - 1));
            i = (e == std::string::npos) ? s.size() : e + 1;
        } else {
            std::size_t e = i;
            while (e < s.size() && !std::isspace(static_cast<unsigned char>(s[e]))) ++e;
            out.push_back(s.substr(i, e - i));
            i = e;
        }
    }
    return out;
}

bool to_int(const std::string& s, int& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    const long x = std::strtol(s.c_str(), &end, 10);
    if (*end) return false;
    v = static_cast<int>(x);
    return true;
}

bool to_num(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return *end == 0;
}

const std::set<std::string>& known_ops() {
    static const std::set<std::string> k = {
        "set",  "wait", "click", "drag",   "move",    "key",   "hold", "type", "view",
        "place", "shot", "assert", "until", "require", "let",  "cheat", "log", "end",
        "expect", "dump", "cycle", "if", "touch", "waituntil", "lifecycle", "ime"};
    return k;
}

// P3: `repeat N` ... `endrepeat` (nestable) is expanded textually before
// parsing: the body runs N times exactly as if it were written out N times
// (long real-order micro loops without multi-megabyte scripts). Returns at the
// matching endrepeat when `nested`, else at the end of the text.
bool expand_repeats(const std::vector<std::string>& lines, std::size_t& i, bool nested,
                    std::string& out, std::string& err) {
    while (i < lines.size()) {
        const std::string t = lower(trim(strip_comment(lines[i])));
        ++i;
        if (t == "endrepeat") {
            if (nested) return true;
            err = "line " + std::to_string(i) + ": endrepeat without repeat";
            return false;
        }
        if (t.rfind("repeat ", 0) == 0) {
            int n = 0;
            if (!to_int(trim(t.substr(7)), n) || n < 0) {
                err = "line " + std::to_string(i) + ": repeat needs a non-negative count";
                return false;
            }
            std::string body;
            if (!expand_repeats(lines, i, true, body, err)) return false;
            for (int k = 0; k < n; ++k) out += body;
            continue;
        }
        out += lines[i - 1];
        out += '\n';
    }
    if (nested) { err = "repeat without endrepeat"; return false; }
    return true;
}

}  // namespace

bool parse_script(const std::string& text_in, Script& out, std::string& err) {
    out.cmds.clear();
    std::string text;
    {
        std::vector<std::string> lines;
        std::istringstream src(text_in);
        for (std::string l; std::getline(src, l);) lines.push_back(l);
        std::size_t i = 0;
        if (!expand_repeats(lines, i, false, text, err)) return false;
    }
    std::istringstream in(text);
    std::string line;
    int ln = 0;
    while (std::getline(in, line)) {
        ++ln;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = trim(strip_comment(line));
        if (t.empty() || t[0] == ';') continue;
        Cmd c;
        c.line = ln;
        c.raw = t;
        // Split off trailing @tags (they never appear inside expressions).
        std::vector<std::string> words = split_words(t);
        while (!words.empty() && words.back().size() > 1 && words.back()[0] == '@') {
            c.tags.insert(c.tags.begin(), words.back().substr(1));
            words.pop_back();
        }
        if (words.empty()) continue;
        c.op = lower(words[0]);
        if (!known_ops().count(c.op)) {
            err = "line " + std::to_string(ln) + ": unknown command '" + words[0] + "'";
            return false;
        }
        c.args.assign(words.begin() + 1, words.end());
        // The expression text = everything after the op (and N for until),
        // minus the tags, re-joined from the raw words (quotes kept).
        auto rejoin = [&](std::size_t from) {
            std::string e;
            // Re-tokenise the raw line keeping quotes so string literals survive.
            std::vector<std::string> raw;
            {
                std::size_t i = 0;
                const std::string& s = t;
                while (i < s.size()) {
                    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
                    if (i >= s.size()) break;
                    std::size_t e2 = i;
                    if (s[i] == '"') {
                        e2 = s.find('"', i + 1);
                        e2 = (e2 == std::string::npos) ? s.size() : e2 + 1;
                    } else {
                        while (e2 < s.size() && !std::isspace(static_cast<unsigned char>(s[e2]))) ++e2;
                    }
                    raw.push_back(s.substr(i, e2 - i));
                    i = e2;
                }
            }
            const std::size_t stop = raw.size() - c.tags.size();
            for (std::size_t k = from; k < stop; ++k) e += (e.empty() ? "" : " ") + raw[k];
            return e;
        };
        auto need = [&](std::size_t n) {
            if (c.args.size() < n) {
                err = "line " + std::to_string(ln) + ": '" + c.op + "' needs " + std::to_string(n) +
                      " argument(s)";
                return false;
            }
            return true;
        };
        if (c.op == "wait") {
            if (!need(1) || !to_int(c.args[0], c.n) || c.n < 0) {
                if (err.empty()) err = "line " + std::to_string(ln) + ": wait N";
                return false;
            }
        } else if (c.op == "until" || c.op == "waituntil") {
            if (!need(2) || !to_int(c.args[0], c.n) || c.n < 0) {
                if (err.empty()) err = "line " + std::to_string(ln) + ": until N EXPR";
                return false;
            }
            c.expr = rejoin(2);
        } else if (c.op == "if") {
            // if EXPR then CMD ARGS...   (CMD may not block: no until/cycle/if)
            std::size_t th = 0;
            while (th < c.args.size() && c.args[th] != "then") ++th;
            if (th == 0 || th + 1 >= c.args.size()) {
                err = "line " + std::to_string(ln) + ": if EXPR then CMD";
                return false;
            }
            c.expr = rejoin(1);
            const std::size_t cut = c.expr.find(" then ");
            std::string rest = c.expr.substr(cut + 6);
            c.expr = c.expr.substr(0, cut);
            for (const std::string& t : c.tags) rest += " @" + t;
            Script one;
            std::string e2;
            if (!parse_script(rest, one, e2) || one.cmds.size() != 1) {
                err = "line " + std::to_string(ln) + ": if ... then: " + e2;
                return false;
            }
            const std::string& so = one.cmds[0].op;
            // R-183: `if EXPR then end` is allowed (a script that does not
            // apply to this install ends early, e.g. robust-missing).
            if (so == "until" || so == "waituntil" || so == "cycle" || so == "if") {
                err = "line " + std::to_string(ln) + ": 'if' cannot guard '" + so + "'";
                return false;
            }
            one.cmds[0].line = ln;
            c.sub.push_back(one.cmds[0]);
        } else if (c.op == "cycle") {
            if (!need(2)) return false;
            c.expr = rejoin(2);
        } else if (c.op == "assert" || c.op == "require") {
            if (!need(1)) return false;
            c.expr = rejoin(1);
        } else if (c.op == "let") {
            if (c.args.size() < 3 || c.args[1] != "=") {
                err = "line " + std::to_string(ln) + ": let NAME = OPERAND";
                return false;
            }
            c.expr = rejoin(3);
        } else if (c.op == "hold") {
            if (!need(2) || !to_int(c.args[1], c.n) || c.n < 1) {
                if (err.empty()) err = "line " + std::to_string(ln) + ": hold KEY N";
                return false;
            }
        } else if (c.op == "click" || c.op == "key" || c.op == "shot" || c.op == "set" ||
                   c.op == "type" || c.op == "cheat" || c.op == "move" || c.op == "view" ||
                   c.op == "drag" || c.op == "place" || c.op == "expect" || c.op == "touch" ||
                   c.op == "lifecycle" || c.op == "ime") {
            if (!need(1)) return false;
        }
        c.raw = rejoin(0);   // the command without its @tags
        out.cmds.push_back(std::move(c));
    }
    return true;
}

// ---------------------------------------------------------------------------
bool resolve_operand(const std::string& tok, const ProbeData& p,
                     const std::map<std::string, Value>& vars, bool lhs, Value& out) {
    out = Value{};
    if (tok.empty()) return false;
    double d = 0.0;
    if (to_num(tok, d)) { out.is_num = true; out.num = d; return true; }
    if (tok.size() >= 2 && tok.front() == '"' && tok.back() == '"') {
        out.is_num = false;
        out.str = tok.substr(1, tok.size() - 2);
        return true;
    }
    if (tok[0] == '$') {
        // $name, or $name+N / $name-N (numeric offset, no spaces).
        std::size_t op = tok.find_first_of("+-", 1);
        auto it = vars.find(tok.substr(1, op == std::string::npos ? std::string::npos : op - 1));
        if (it == vars.end()) return false;
        out = it->second;
        if (op != std::string::npos) {
            double k = 0.0;
            if (!out.is_num || !to_num(tok.substr(op + 1), k)) return false;
            out.num += tok[op] == '+' ? k : -k;
        }
        return true;
    }
    if (auto it = p.n.find(tok); it != p.n.end()) { out.is_num = true; out.num = it->second; return true; }
    if (auto it = p.s.find(tok); it != p.s.end()) { out.is_num = false; out.str = it->second; return true; }
    // Agent B's integer probe keys are dotted ("units.0.GI", "sel.moving") and
    // read 0 when absent (B's contract) -- keep that for them.
    if (lhs && tok.find('.') != std::string::npos && tok.find('(') == std::string::npos &&
        tok[0] != '"') {
        out.is_num = true;
        out.num = 0.0;
        return true;
    }
    const std::size_t par = tok.find('(');
    if (par != std::string::npos && p.zero_families.count(tok.substr(0, par))) {
        out.is_num = true;
        out.num = 0.0;
        return true;
    }
    if (!lhs) { out.is_num = false; out.str = tok; return true; }   // bare word literal
    return false;
}

namespace {

std::string show(const Value& v) {
    if (!v.is_num) return "\"" + v.str + "\"";
    char b[64];
    std::snprintf(b, sizeof b, "%.6g", v.num);
    return b;
}

Eval eval_clause(const std::vector<std::string>& w, const ProbeData& p,
                 const std::map<std::string, Value>& vars, std::string& detail) {
    static const std::set<std::string> ops = {"==", "!=", "<", "<=", ">", ">="};
    if (w.size() == 1) {
        Value v;
        if (!resolve_operand(w[0], p, vars, true, v)) { detail = "missing-probe:" + w[0]; return Eval::Missing; }
        detail = w[0] + "=" + show(v);
        const bool t = v.is_num ? v.num != 0.0 : !v.str.empty();
        return t ? Eval::True : Eval::False;
    }
    if (w.size() != 3 || !ops.count(w[1])) {
        detail = "malformed clause";
        return Eval::Error;
    }
    Value a, b;
    if (!resolve_operand(w[0], p, vars, true, a)) { detail = "missing-probe:" + w[0]; return Eval::Missing; }
    if (!resolve_operand(w[2], p, vars, false, b)) { detail = "missing-probe:" + w[2]; return Eval::Missing; }
    detail = w[0] + "=" + show(a) + " " + w[1] + " " + show(b);
    const std::string& op = w[1];
    bool r = false;
    if (a.is_num && b.is_num) {
        const double x = a.num, y = b.num;
        r = op == "==" ? std::fabs(x - y) < 1e-9 : op == "!=" ? std::fabs(x - y) >= 1e-9
          : op == "<" ? x < y : op == "<=" ? x <= y : op == ">" ? x > y : x >= y;
    } else {
        const std::string x = a.is_num ? show(a) : a.str, y = b.is_num ? show(b) : b.str;
        if (op == "==") r = x == y;
        else if (op == "!=") r = x != y;
        else { detail += " (ordering on a string)"; return Eval::Error; }
    }
    return r ? Eval::True : Eval::False;
}

}  // namespace

Eval eval_expr(const std::string& expr, const ProbeData& p,
               const std::map<std::string, Value>& vars, std::string& detail) {
    // Tokenise keeping "quoted strings" as single tokens WITH quotes.
    std::vector<std::string> toks;
    {
        std::size_t i = 0;
        while (i < expr.size()) {
            while (i < expr.size() && std::isspace(static_cast<unsigned char>(expr[i]))) ++i;
            if (i >= expr.size()) break;
            std::size_t e = i;
            if (expr[i] == '"') {
                e = expr.find('"', i + 1);
                e = (e == std::string::npos) ? expr.size() : e + 1;
            } else {
                while (e < expr.size() && !std::isspace(static_cast<unsigned char>(expr[e]))) ++e;
            }
            toks.push_back(expr.substr(i, e - i));
            i = e;
        }
    }
    if (toks.empty()) { detail = "empty expression"; return Eval::Error; }
    detail.clear();
    Eval acc = Eval::True;
    std::string join = "and";
    std::vector<std::string> cur;
    auto flush = [&]() -> bool {
        std::string d;
        const Eval e = eval_clause(cur, p, vars, d);
        detail += (detail.empty() ? "" : " " + join + " ") + d;
        cur.clear();
        if (e == Eval::Missing || e == Eval::Error) { acc = e; return false; }
        const bool a = acc == Eval::True, b = e == Eval::True;
        acc = (join == "and" ? (a && b) : (a || b)) ? Eval::True : Eval::False;
        return true;
    };
    bool first = true;
    for (const std::string& t : toks) {
        if (t == "and" || t == "or") {
            if (cur.empty()) { detail = "dangling '" + t + "'"; return Eval::Error; }
            if (first) {
                std::string d;
                const Eval e = eval_clause(cur, p, vars, d);
                detail = d;
                cur.clear();
                if (e == Eval::Missing || e == Eval::Error) return e;
                acc = e;
                first = false;
            } else if (!flush()) {
                return acc;
            }
            join = t;
            continue;
        }
        cur.push_back(t);
    }
    if (cur.empty()) { detail = "dangling operator"; return Eval::Error; }
    if (first) {
        std::string d;
        const Eval e = eval_clause(cur, p, vars, d);
        detail = d;
        return e;
    }
    flush();
    return acc;
}

std::string probe_dump(const ProbeData& p) {
    std::string out;
    char b[64];
    for (const auto& [k, v] : p.n) {
        std::snprintf(b, sizeof b, "%.6g", v);
        out += k + "=" + b + "\n";
    }
    for (const auto& [k, v] : p.s) out += k + "=\"" + v + "\"\n";
    return out;
}

std::uint64_t probe_hash(const ProbeData& p, std::uint64_t h) {
    const std::string d = probe_dump(p);
    for (unsigned char c : d) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace ra2play
