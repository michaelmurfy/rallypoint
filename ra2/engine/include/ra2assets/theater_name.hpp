#pragma once
#include <string>
#include <vector>

namespace ra2assets {

// A single candidate for resolving a NewTheater art name: try `file` inside
// nested mix `mix` (both lowercase, mix name relative to ra2.mix).
struct TheaterCandidate {
    std::string mix;   // e.g. "generic.mix"
    std::string file;  // e.g. "ggcnst.shp"
};

// NewTheater=yes filename resolution [RE-GATE APPLIED: building-render.md §1,
// recovered from FUN_005F8860/FUN_005F8EA0/FUN_005F8F00]. The munge applies
// ONLY when name[0] is one of {g,n,c,y} AND name[1] is one of {a,t} (case-
// insensitive): name[1] is replaced by the theater letter ('T' temperate,
// 'A' snow, 'U' urban); the lookup is a GLOBAL search over the loaded mixes;
// on miss, ONE retry with name[1]='G' (the generic art). There is NO third
// literal candidate for munged names (snow's letter IS 'A', so a literal
// spelling like "gacnst" is already the snow-themed name). Names that fail
// the pattern check get the literal name only.
// `image` is the artmd image/section name (e.g. "GACNST"); `ext` includes the
// dot (".shp"). iso_mix/theater_mix define the engine's walk ORDER over its
// open nested mixes (the binary searches all loaded mixes; we search these
// three per name): for each candidate NAME in order [themed, generic-'g']
// (or just [literal] for non-matching names), emit
//   { iso_mix, name+ext }, { theater_mix, name+ext }, { "generic.mix", name+ext }
// i.e. 6 entries for munged names, 3 for literal ones. Pinned real
// locations this must reproduce: ggcnst->generic.mix, gacnst->snow.mix,
// gtcnstmk->isotemp.mix.
// theater_mix: "temperat.mix" | "snow.mix" | "urban.mix".
std::vector<TheaterCandidate> theater_candidates(const std::string& image,
                                                 char theater_letter,
                                                 const std::string& iso_mix,
                                                 const std::string& theater_mix,
                                                 const std::string& ext);

// One theater's complete asset chain [RE: maps.md §6 + §9 RE-GATE items 1-4].
// Everything a generic theater loader needs, following the exact naming
// pattern the temperate chain already uses. `yr_only` theaters (NEWURBAN/
// DESERT/LUNAR) live under ra2md.mix + localmd.mix/cachemd.mix instead of
// ra2.mix + local.mix/cache.mix. `playable` is false for LUNAR: maps.md §3
// confirms NO LUNAR multiplayer map exists on this install.
// All string members are static storage -- safe to hold as const char*.
//
// [RE-GATE APPLIED: maps.md §9.4] `iso_mix` is the BASE mix and is
// AUTHORITATIVE. The same-named `*md` variants (isotemmd.mix, isosnomd.mix,
// isourbmd.mix, isoubnmd.mix, isodesmd.mix) are OVERLAYS: each resolves 0 of a
// real map's tiles and lacks even Clear01 at index 0, so a loader that opens
// only the md variant renders an EMPTY MAP.
struct TheaterChain {
    const char* name;         // "TEMPERATE"    -- [Map] Theater= value
    char        letter;       // NewTheater art letter for theater_candidates
    const char* iso_mix;      // "isotemp.mix"  -- tile templates (BASE, authoritative)
    const char* art_mix;      // "temperat.mix" -- building/overlay art
    const char* control_ini;  // "temperat.ini" -- [TileSetNNNN] config
    const char* iso_pal;      // "isotem.pal"   -- iso TILE palette
    const char* theater_pal;  // "temperat.pal" -- ore/gem overlay palette
    const char* unit_pal;     // "unittem.pal"  -- shared unit palette
    const char* ext;          // "tem"          -- tile extension, NO dot
    bool        yr_only;      // true -> ra2md.mix/localmd.mix/cachemd.mix
    bool        playable;     // false -> reject (LUNAR: 0 MP maps, §3)
};

// Case-insensitive lookup of a [Map] Theater= value. nullptr if unknown.
const TheaterChain* theater_chain(const std::string& theater_name);

} // namespace ra2assets
