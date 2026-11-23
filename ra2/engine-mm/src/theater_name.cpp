#include "ra2assets/theater_name.hpp"
#include <cctype>

namespace ra2assets {

static std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::vector<TheaterCandidate> theater_candidates(const std::string& image,
                                                 char theater_letter,
                                                 const std::string& iso_mix,
                                                 const std::string& theater_mix,
                                                 const std::string& ext) {
    std::vector<TheaterCandidate> out;
    if (image.empty()) return out;
    std::string base = lower(image);
    // [RE-GATE: building-render.md §1] munge only when name[0] in {g,n,c,y}
    // and name[1] in {a,t} (FUN_005F8EA0's pattern check); candidate NAMES
    // are [themed, generic-'g'] (FUN_005F8F00's one 'G' retry), each searched
    // across the walk-order mixes; non-matching names stay literal.
    auto push_name = [&](const std::string& name) {
        out.push_back({iso_mix, name + ext});
        out.push_back({theater_mix, name + ext});
        out.push_back({"generic.mix", name + ext});
    };
    bool munge = base.size() >= 2 &&
                 (base[0] == 'g' || base[0] == 'n' || base[0] == 'c' || base[0] == 'y') &&
                 (base[1] == 'a' || base[1] == 't');
    if (munge) {
        std::string themed = base;
        themed[1] = (char)std::tolower((unsigned char)theater_letter);
        std::string generic = base;
        generic[1] = 'g';
        push_name(themed);
        push_name(generic);   // real theater letters T/A/U never equal 'g'
    } else {
        push_name(base);
    }
    return out;
}

// [RE: maps.md §6 table + §9 RE-GATE items 1-4]. The TEMPERATE row reproduces
// EXACTLY the literals main.cpp::load_assets/load_ore_art/load_building_assets
// hardcode today ('T', isotemp.mix, temperat.mix, temperat.ini, isotem.pal,
// temperat.pal, unittem.pal, "tem") -- that identity is what keeps every
// existing candidate walk byte-identical (maps.md §7 rule 4).
static const TheaterChain kChains[] = {
    {"TEMPERATE", 'T', "isotemp.mix", "temperat.mix", "temperat.ini",
     "isotem.pal", "temperat.pal", "unittem.pal", "tem", false, true},
    // [RE-GATE APPLIED: maps.md §9.4] SNOW/URBAN take the *md* control INI --
    // that is what Task 1 proved resolves (snowmd.ini 164/164, urbanmd.ini
    // 116/116). The non-md snow.ini/urban.ini differ in size and were NOT the
    // files the probe validated. TEMPERATE keeps today's "temperat.ini" literal
    // for gate safety (§7 rule 4); its md twin is byte-size-identical (25269).
    {"SNOW",      'A', "isosnow.mix", "snow.mix",     "snowmd.ini",
     "isosno.pal", "snow.pal",     "unitsno.pal", "sno", false, true},
    {"URBAN",     'U', "isourb.mix",  "urban.mix",    "urbanmd.ini",
     "isourb.pal", "urban.pal",    "uniturb.pal", "urb", false, true},
    // [RE-GATE APPLIED: maps.md §9.1-9.4] YR theaters. Letters N/D/L are
    // UNEXERCISED -- §9.1 proved only SNOW has dedicated building art (letter
    // 'A'); every other theater resolves through the existing 'G' generic
    // fallback into generic.mix, so a wrong letter here cannot bite. Art-mix
    // and palette names below are all CONFIRMED present (§9.2/§9.3).
    {"NEWURBAN",  'N', "isoubn.mix",  "urbann.mix",   "urbannmd.ini",
     "isoubn.pal", "urbann.pal",   "unitubn.pal", "ubn", true,  true},
    {"DESERT",    'D', "isodes.mix",  "desert.mix",   "desertmd.ini",
     "isodes.pal", "desert.pal",   "unitdes.pal", "des", true,  true},
    // LUNAR: campaign-only. maps.md §3 -- 0 multiplayer maps exist, so its
    // chain is unverified (§9.4) and it is rejected by the browser.
    {"LUNAR",     'L', "isolun.mix",  "lunar.mix",    "lunarmd.ini",
     "isolun.pal", "lunar.pal",    "unitlun.pal", "lun", true,  false},
};

const TheaterChain* theater_chain(const std::string& theater_name) {
    std::string up;
    up.reserve(theater_name.size());
    for (char c : theater_name) up.push_back((char)std::toupper((unsigned char)c));
    for (const TheaterChain& c : kChains)
        if (up == c.name) return &c;
    return nullptr;
}

} // namespace ra2assets
