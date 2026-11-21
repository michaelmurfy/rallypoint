#pragma once
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ra2assets {

// Minimal INI reader for RA2/YR rules and map text. RA2 INIs are
// case-insensitive for section and key names (values are NOT normalized).
// Duplicate keys within a section use INI "override" semantics for get()
// (last value wins) while section() preserves every occurrence in file
// order -- required by [IsoMapPack5]-style numbered keys ("1=,2=,...")
// whose values must be concatenated in the order they appear.
class Ini {
public:
    // Parse INI text (accepts bytes too via std::string(begin,end)). Accepts
    // both CRLF and LF line endings. Lines whose first non-whitespace
    // character is ';' or '#' are treated as full-line comments and
    // ignored. Every entry line is cut at its first ';' (an inline comment,
    // exactly as the game's INIClass::Load does -- docs/re/ini-reader.md), and
    // an entry whose key or value is empty after that is dropped (as the game
    // drops it). Section names, keys, and values are trimmed of surrounding
    // whitespace; section names and keys are additionally normalized to a
    // canonical (upper) case for storage and lookup.
    static Ini parse(const std::string& text);

    // True if `section` (case-insensitive) was seen while parsing.
    bool has(const std::string& section) const;

    // Last value stored for `key` (case-insensitive) within `section`
    // (case-insensitive), or `def` if the section or key is absent.
    std::string get(const std::string& section, const std::string& key, const std::string& def = "") const;

    // Like get(), but parses a leading (optionally signed) integer from the
    // value. Returns `def` if the key/section is absent or the value has no
    // parseable leading integer.
    int get_int(const std::string& section, const std::string& key, int def = 0) const;

    // Ordered key/value list for a section (case-insensitive lookup),
    // preserving file order and every duplicate-key occurrence -- needed
    // for IsoMapPack5 numbered-key concatenation. Returns a reference to a
    // static empty vector if the section is absent (never throws).
    const std::vector<std::pair<std::string, std::string>>& section(const std::string& name) const;

    // Canonical (upper-cased) names of every section seen, in file order.
    std::vector<std::string> section_names() const;

private:
    using KVList = std::vector<std::pair<std::string, std::string>>;

    // Sections in insertion (file) order; name is the canonical (upper)
    // form. `index_` maps that same canonical name to its slot in
    // `sections_` for O(1) has()/get()/section() lookups.
    std::vector<std::pair<std::string, KVList>> sections_;
    std::unordered_map<std::string, size_t> index_;
};

} // namespace ra2assets
