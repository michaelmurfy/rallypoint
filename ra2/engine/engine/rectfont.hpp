#pragma once
#include <array>
#include <cstdint>
namespace ra2engine {
// 5x7 rect-font [RE: skirmish.md §UI, D5]: 7 rows per glyph, low 5 bits used
// (bit 4 = leftmost column), mirroring kDigits. Returns the row bitmap for a
// glyph: A-Z (upper), 0-9, space, and basic punctuation (':' '-' '.'). Unknown
// chars -> the blank glyph. PURE (no SDL); host-testable. NOT hashed.
const std::array<std::uint8_t, 7>& glyph_rows(char c);
}  // namespace ra2engine
