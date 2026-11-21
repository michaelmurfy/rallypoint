#pragma once
#include "ra2assets/ini.hpp"
#include <string>
#include <vector>

namespace ra2assets {

// One sound-event entry parsed from soundmd.ini, e.g. [GIAttack]'s
// Sounds=igiat1a igiat1b igiat1c, Volume=65, VShift=15, Control=random
// interrupt. Absent fields fall back to the file's own [Defaults] section
// (seeded once by Sounds::load), mirroring the real engine's sound-event
// ctor (docs/re/audio-playback.md: FUN_004063b0 zero-inits then writes the
// hard defaults, and the per-event parser only overwrites keys that are
// actually present). Field defaults below match the real soundmd.ini
// [Defaults] section so an entirely-defaults SoundEntry (e.g. for an unknown
// sound name) already carries the right values even without a parsed Ini.
struct SoundEntry {
    std::vector<std::string> sounds;   // variant clip names, leading '$'/'#' stripped
    int volume = 80;
    std::string priority = "normal";   // lower-cased
    int limit = 5;
    double range = 10.0;               // cells
    int min_volume = 50;
    std::vector<std::string> control;  // lower-cased tokens ("random","interrupt",...)
    std::vector<std::string> type;     // lower-cased tokens ("normal","screen",...)
    int vshift = 0;
    int fshift_lo = 0, fshift_hi = 0;
    // W5-E [RE: FUN_0074f260 per-event parser / FUN_004055c0 predelay]:
    // Delay= min max in milliseconds (one value = both), read for PREDELAY /
    // AMBIENT events.
    int delay_lo = 0, delay_hi = 0;
    // W7-12-24 [RE: FUN_0074F260 ReadInteger "Attack" / "Decay" (+0x138 /
    // +0x13C)]: how many leading / trailing samples are the attack / decay
    // pool (0 = unset; FUN_00406570 forces 1 when the Control bit is set).
    int attack_n = 0, decay_n = 0;
};

// Thin, minimal reader for soundmd.ini's sound-event sections (Task 2 of the
// Audio Playback phase). Still deliberately NOT a general sound.ini model --
// fields land only when the sim needs them (YAGNI), same posture as Rules
// and Art.
//
// Sounds wraps an already-parsed Ini by reference (not by copy) to stay
// "thin" -- the caller must keep the Ini alive for at least as long as the
// Sounds object built from it. Mirrors Rules/Art's load/by-reference
// lifetime contract (see rules.hpp / art.hpp).
class Sounds {
public:
    // Parses [Defaults] once (Volume/Limit/MinVolume/Range/Priority/Type)
    // into the shared fallback entry every other section inherits from.
    static Sounds load(const Ini& ini);

    // True if `name` (case-insensitive) is a section in the wrapped Ini.
    bool has(const std::string& name) const;

    // Parses [name] into a SoundEntry, with any absent key falling back to
    // the [Defaults]-seeded value. If `name` itself is absent, returns the
    // [Defaults] entry verbatim (empty `sounds`/`control`).
    SoundEntry entry(const std::string& name) const;

private:
    explicit Sounds(const Ini& ini);
    const Ini* ini_;
    SoundEntry defaults_;
};

} // namespace ra2assets
