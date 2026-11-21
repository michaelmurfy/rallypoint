#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace ra2assets {

// Reader for Westwood CSF string tables (the loose ra2.csf / ra2md.csf in
// the install root). Maps ASCII labels ("GUI:Cancel", "Name:HTNK", ...) to
// UTF-16 display strings. Decode only -- no UI (Phases 10/11 consume this).
//
// Scope decisions (Foundation Batch spec, sub-project 2):
//  * Only the FIRST value of a multi-value label is stored; the remaining
//    value records are walked (to keep the parse in sync) and discarded.
//  * A label with num_values == 0 is legal-looking on disk but has no text:
//    it is walked and NOT inserted -- find() returns nullptr for it and
//    count() excludes it. (Storing an empty string instead would be
//    indistinguishable from a genuine empty first value.)
//  * Labels are matched case-insensitively (the game resolves "gui:cancel"
//    and "GUI:Cancel" identically): keys are normalized to ASCII uppercase
//    at insert and at lookup. RE evidence in docs/re/csf.md; if the game
//    ever proves case-sensitive, drop the normalization there and here
//    together.
//  * Duplicate labels keep the FIRST occurrence.
//  * "WRTS" values carry trailing extra ASCII data; it is skipped.
class CsfStrings {
public:
    // Parses a whole CSF file image. Throws std::runtime_error with a
    // context message on any structural error (bad magic, record running
    // past the end of the buffer, ...).
    static CsfStrings load(const uint8_t* data, size_t len);

    // Case-insensitive lookup. Returns nullptr when the label is absent.
    const std::u16string* find(const std::string& label) const;

    // Number of labels stored (labels with at least one value).
    size_t count() const { return table_.size(); }

private:
    // label (uppercased) -> first value (extra/secondary values ignored for
    // this milestone)
    std::unordered_map<std::string, std::u16string> table_;
};

} // namespace ra2assets
