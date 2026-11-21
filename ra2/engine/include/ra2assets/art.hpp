#pragma once
#include "ra2assets/ini.hpp"
#include <string>

namespace ra2assets {

// One [SequenceName] entry parsed from artmd.ini, e.g. GISequence's
// FireUp=164,6,6 -> {start=164, count=6, facing_mult=6}. Field 4+ (facing
// labels like "S"/"E", sound tags, etc.) are intentionally ignored -- YAGNI
// until the sim needs them. Defaults {0,1,0} mirror an absent/empty entry
// (a single frame at index 0, no facing variants).
struct ArtSequenceEntry {
    int start = 0;
    int count = 1;
    int facing_mult = 0;
};

// Thin, minimal reader for the artmd.ini fields the engine needs to resolve
// an infantry image's animation sequence: [image] Sequence= (the name of the
// [SequenceName] section) and that section's Start,Count,FacingMult entries
// (Ready=, Walk=, FireUp=, Die1=, ...). Still deliberately NOT a general
// art.ini model -- fields land only when the sim needs them (YAGNI).
//
// Art wraps an already-parsed Ini by reference (not by copy) to stay "thin"
// -- the caller must keep the Ini alive for at least as long as the Art
// object built from it. Mirrors Rules's load/by-reference lifetime contract
// (see rules.hpp).
class Art {
public:
    static Art load(const Ini& ini);

    // [image] Sequence=, the name of the [SequenceName] section describing
    // that image's animation frames, or "" if the image/key is absent.
    std::string sequence_name(const std::string& image) const;

    // Looks up `image`'s Sequence= section, then reads `name` (e.g. "Walk",
    // "FireUp", "Die1") from it as Start,Count,FacingMult. Tolerates extra
    // trailing comma-separated fields (facing labels, sound tags) and
    // embedded spaces around fields. Returns the {0,1,0} default if the
    // image, its sequence section, or the entry key is absent.
    ArtSequenceEntry entry(const std::string& image, const std::string& name) const;

    // [image] Voxel=, true iff the value is "yes"/"true" (case-insensitive);
    // false if absent. Distinguishes voxel-rendered units (vehicles) from
    // SHP-rendered ones.
    bool voxel(const std::string& image) const;

    // [image] Remapable=, true iff the value is "yes"/"true" (case-
    // insensitive); false if absent. Marks the image's house-color remap
    // (owner tint) as applicable.
    bool remapable(const std::string& image) const;

    // [image] PrimaryFireFLH=F,L,H (turret-relative Forward/Lateral/Height
    // muzzle offset) parsed into flh[0..2]. Returns false and leaves flh
    // untouched if the image/key is absent or doesn't have exactly 3
    // int-parseable comma-separated fields.
    bool fire_flh(const std::string& image, int flh[3]) const;

    // [image] DockingOffset<index>=X,Y,Z (leptons, relative to the building's
    // centre coord) [RE: roster2.md TL;DR 9]. An inline ';' comment and
    // surrounding blanks are stripped (artmd [GAAIRC] carries "0,-128,0 ;...").
    // Returns false and leaves xyz untouched if absent / not exactly 3 ints.
    bool docking_offset(const std::string& image, int index, int xyz[3]) const;

    // [image] Foundation=WxH (case-insensitive 'x'), parsed into fw/fh.
    // Returns false and leaves fw/fh untouched if the image/key is absent or
    // the value isn't a parseable "<int>x<int>" pair.
    bool foundation(const std::string& image, int& fw, int& fh) const;

    // [image] Buildup=, or "" if absent.
    std::string buildup(const std::string& image) const;

    // [image] Cameo=, or "" if absent.
    std::string cameo(const std::string& image) const;

    // W6-04-04: [image] DamageLevels= (a wall OverlayType's damage stages,
    // GAWALL/NAWALL 3, sandbags/fences 2); 1 if absent.
    int damage_levels(const std::string& image) const;

    // [image] NewTheater=, true iff the value is "yes"/"true" (case-
    // insensitive); false if absent.
    bool new_theater(const std::string& image) const;

    // W3-FIX-A: [image] Palette= (a palette base name, e.g. "lib"), or "".
    // The value is trimmed and an inline ';' comment dropped.
    std::string palette(const std::string& image) const;

    // W3-FIX-A: [image] Theater=yes (the art takes the theater extension).
    bool theater(const std::string& image) const;

private:
    explicit Art(const Ini& ini) : ini_(&ini) {}
    const Ini* ini_;
};

} // namespace ra2assets
