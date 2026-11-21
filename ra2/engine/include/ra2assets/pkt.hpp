#pragma once
#include <string>
#include <vector>

namespace ra2assets {

// One map row of a Westwood ".pkt" map manifest [RE: maps.md §2]. The stock
// map list is NOT "every entry in the mix": it is this packet file, an INI with
// a [MultiMaps] index (1=XMP29U2, 2=XMP21S2, ...) and one section per map:
//     [XMP01T4]
//     Description=DESC:MP01DU     ; a CSF string label, NOT a display name
//     CD=2
//     MinPlayers=2
//     MaxPlayers=4
//     GameMode=standard           ; CSV of mode tags
// YR (our target) uses langmd.mix/missionsmd.pkt (21,142 B, entry 480B82E4);
// RA2 uses missions.pkt. NOTE (maps.md §2): MinPlayers/MaxPlayers is the
// LOBBY-facing cap and CAN disagree with the map's [Waypoints] start count
// (6 of 161 maps do) -- the waypoints are the placeable positions.
struct PktMapEntry {
    std::string index_key;    // "1"           -- the [MultiMaps] key
    std::string base_name;    // "XMP01T4"     -- verbatim [MultiMaps] value
    std::string file_name;    // "xmp01t4.map" -- lowercased base_name + ".map"
    std::string description;  // "DESC:MP01DU" -- CSF label; "" if absent
    std::vector<std::string> game_modes;   // lowercased, trimmed GameMode= tags
    int min_players = 0;
    int max_players = 0;
    int cd = 0;
    // Case-insensitive membership test over game_modes (e.g. has_mode("standard")).
    bool has_mode(const std::string& tag) const;
};

class PktFile {
public:
    // Parse a whole .pkt file image (INI text). Never throws on content: a
    // [MultiMaps] value with no matching section still yields an entry (with
    // empty description/modes and zero counts) -- dropping unusable maps is the
    // CALLER's job via the mandatory existence filter (maps.md §2). Returns an
    // empty entry list if there is no [MultiMaps] section.
    static PktFile parse(const std::string& ini_text);

    // Entries in [MultiMaps] file order (the order the original browser lists).
    const std::vector<PktMapEntry>& entries() const { return entries_; }

private:
    std::vector<PktMapEntry> entries_;
};

} // namespace ra2assets
