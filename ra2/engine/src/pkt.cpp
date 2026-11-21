#include "ra2assets/pkt.hpp"
#include "ra2assets/ini.hpp"
#include <algorithm>
#include <cctype>

namespace ra2assets {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) ++b;
    while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
    return s.substr(b, e - b);
}

// Split a GameMode= CSV into trimmed, lowercased tags; empty fields dropped.
std::vector<std::string> split_modes(const std::string& csv) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= csv.size()) {
        const size_t comma = csv.find(',', pos);
        const std::string tok =
            trim(comma == std::string::npos ? csv.substr(pos) : csv.substr(pos, comma - pos));
        if (!tok.empty()) out.push_back(lower(tok));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

} // namespace

bool PktMapEntry::has_mode(const std::string& tag) const {
    const std::string t = lower(trim(tag));
    return std::find(game_modes.begin(), game_modes.end(), t) != game_modes.end();
}

PktFile PktFile::parse(const std::string& ini_text) {
    PktFile pkt;
    Ini ini = Ini::parse(ini_text);
    // [MultiMaps] values are map BASE NAMES; append ".map" and look them up in
    // the mix chain (maps.md §2). Ini::section preserves file order, and
    // full-line ';' comments (the pkt's ";169=" inactive rows) were already
    // dropped at parse -- so this walk yields exactly the ACTIVE list.
    const auto& index = ini.section("MultiMaps");
    pkt.entries_.reserve(index.size());
    for (const auto& kv : index) {
        const std::string base = trim(kv.second);
        if (base.empty()) continue;
        PktMapEntry e;
        e.index_key = kv.first;
        e.base_name = base;
        e.file_name = lower(base) + ".map";
        // Ini section lookup is case-insensitive, so [XMP01T4] resolves for a
        // "xmp01t4" index value and vice versa. A missing section leaves the
        // metadata defaulted -- the caller's existence filter is what drops it.
        e.description = trim(ini.get(base, "Description", ""));
        e.game_modes  = split_modes(ini.get(base, "GameMode", ""));
        e.min_players = ini.get_int(base, "MinPlayers", 0);
        e.max_players = ini.get_int(base, "MaxPlayers", 0);
        e.cd          = ini.get_int(base, "CD", 0);
        pkt.entries_.push_back(std::move(e));
    }
    return pkt;
}

} // namespace ra2assets
