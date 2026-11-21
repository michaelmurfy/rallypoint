#include "ra2assets/ini.hpp"
#include <cctype>

// Straightforward line-oriented INI parser. RA2/YR rules and map files are
// plain-text INI: `[Section]` headers, `key=value` lines, and `;`/`#`
// full-line comments. See ini.hpp for the case-insensitivity / duplicate-key
// / ordering contract -- this file just implements it.
namespace ra2assets {

namespace {

// Trim ASCII whitespace (space/tab/CR/LF) from both ends. CR is included so
// a stray '\r' surviving a naive split still gets stripped defensively, even
// though callers already strip line-trailing '\r' explicitly below.
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Canonical-case normalization for section/key comparison and storage.
std::string to_upper(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)std::toupper((unsigned char)c);
    return out;
}

} // namespace

Ini Ini::parse(const std::string& text) {
    Ini ini;
    KVList* current = nullptr;

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        std::string line = (eol == std::string::npos) ? text.substr(pos) : text.substr(pos, eol - pos);
        // Handle CRLF: a line ending in "\r\n" still has the '\r' attached
        // after splitting on '\n' alone; strip it so values/keys never
        // retain a trailing carriage return.
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string trimmed = trim(line);
        if (!trimmed.empty() && trimmed[0] != ';' && trimmed[0] != '#') {
            if (trimmed[0] == '[') {
                size_t close = trimmed.find(']');
                std::string name = (close == std::string::npos) ? trimmed.substr(1) : trimmed.substr(1, close - 1);
                name = to_upper(trim(name));
                auto it = ini.index_.find(name);
                if (it == ini.index_.end()) {
                    ini.index_[name] = ini.sections_.size();
                    ini.sections_.push_back({name, KVList()});
                    current = &ini.sections_.back().second;
                } else {
                    // Section header repeated later in the file: append to
                    // the existing section rather than starting a new,
                    // shadowing one -- keeps section() ordering meaningful
                    // even for (unusual but legal) re-opened sections.
                    current = &ini.sections_[it->second].second;
                }
            } else if (current != nullptr) {
                // [RE: docs/re/ini-reader.md, INIClass::Load FUN_00525790] the
                // game cuts every entry line at its FIRST ';' (strchr + NUL)
                // before splitting on '=', so "Teleporter=yes;" and
                // "Primary=X ;comment" read "yes" / "X". It then drops a line
                // whose key or value trims to empty (strlen == 0 -> skipped),
                // so an empty "Key=" never shadows an earlier value.
                std::string body = trimmed;
                const size_t semi = body.find(';');
                if (semi != std::string::npos) body.erase(semi);
                size_t eq = body.find('=');
                if (eq != std::string::npos) {
                    std::string key = to_upper(trim(body.substr(0, eq)));
                    std::string value = trim(body.substr(eq + 1));
                    if (!key.empty() && !value.empty()) current->push_back({key, value});
                }
                // Lines with no '=' and no active section are silently
                // ignored (lenient parsing -- matches how RA2's own INI
                // reader tolerates stray/garbage lines).
            }
        }

        if (eol == std::string::npos) break;
        pos = eol + 1;
    }

    return ini;
}

bool Ini::has(const std::string& section) const {
    return index_.find(to_upper(section)) != index_.end();
}

std::string Ini::get(const std::string& section, const std::string& key, const std::string& def) const {
    auto it = index_.find(to_upper(section));
    if (it == index_.end()) return def;
    const KVList& kv = sections_[it->second].second;
    std::string ukey = to_upper(key);
    // Scan from the end so a duplicate key's LAST occurrence wins (INI
    // override semantics), while section() below still exposes every
    // occurrence in original order for callers that need that (IsoMapPack5).
    for (auto rit = kv.rbegin(); rit != kv.rend(); ++rit) {
        if (rit->first == ukey) return rit->second;
    }
    return def;
}

int Ini::get_int(const std::string& section, const std::string& key, int def) const {
    std::string v = get(section, key, std::string());
    if (v.empty()) return def;

    size_t i = 0;
    bool neg = false;
    if (v[i] == '+' || v[i] == '-') {
        neg = (v[i] == '-');
        i++;
    }
    if (i >= v.size() || !std::isdigit((unsigned char)v[i])) return def;

    long value = 0;
    while (i < v.size() && std::isdigit((unsigned char)v[i])) {
        value = value * 10 + (v[i] - '0');
        i++;
    }
    return (int)(neg ? -value : value);
}

const std::vector<std::pair<std::string, std::string>>& Ini::section(const std::string& name) const {
    static const std::vector<std::pair<std::string, std::string>> empty;
    auto it = index_.find(to_upper(name));
    if (it == index_.end()) return empty;
    return sections_[it->second].second;
}

std::vector<std::string> Ini::section_names() const {
    std::vector<std::string> names;
    names.reserve(sections_.size());
    for (const auto& s : sections_) names.push_back(s.first);
    return names;
}

} // namespace ra2assets
