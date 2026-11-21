#include "ra2assets/sounds.hpp"
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace ra2assets {
namespace {
std::vector<std::string> split_ws(const std::string& v) {
    std::vector<std::string> out;
    std::istringstream ss(v);
    std::string tok;
    while (ss >> tok) out.push_back(tok);
    return out;
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

Sounds::Sounds(const Ini& ini) : ini_(&ini) {}

Sounds Sounds::load(const Ini& ini) {
    Sounds s(ini);
    // Seed the shared defaults from [Defaults] (the file's own mechanism --
    // "the default ratings" per soundmd.ini's header comment). Absent
    // section/keys leave the C++ member initializers (which mirror the real
    // file's values) in place.
    SoundEntry& d = s.defaults_;
    d.volume = ini.get_int("Defaults", "Volume", d.volume);
    d.limit = ini.get_int("Defaults", "Limit", d.limit);
    d.min_volume = ini.get_int("Defaults", "MinVolume", d.min_volume);
    d.range = std::atof(ini.get("Defaults", "Range", "10").c_str());
    d.priority = lower(ini.get("Defaults", "Priority", d.priority));
    const std::string ty = ini.get("Defaults", "Type", "");
    if (!ty.empty()) {
        d.type.clear();
        for (const std::string& t : split_ws(ty)) d.type.push_back(lower(t));
    }
    // The header struct's `type` member stays neutral (empty) so it doesn't
    // silently imply a value with no Ini behind it; when [Defaults] itself
    // has no Type= (or the Ini has no [Defaults] at all), fall back to the
    // real soundmd.ini's own [Defaults] Type= value here instead.
    if (d.type.empty()) d.type = {"normal", "screen", "unshroud"};
    return s;
}

bool Sounds::has(const std::string& name) const { return ini_->has(name); }

SoundEntry Sounds::entry(const std::string& name) const {
    SoundEntry e = defaults_;
    if (!ini_->has(name)) return e;
    for (const std::string& raw : split_ws(ini_->get(name, "Sounds", "")))
        // gamemd's add_sound strips a leading '$' OR '#' (docs/re/
        // audio-playback.md, High confidence).
        e.sounds.push_back((raw[0] == '$' || raw[0] == '#') ? raw.substr(1) : raw);
    e.volume = ini_->get_int(name, "Volume", e.volume);
    e.limit = ini_->get_int(name, "Limit", e.limit);
    e.min_volume = ini_->get_int(name, "MinVolume", e.min_volume);
    const std::string rg = ini_->get(name, "Range", "");
    if (!rg.empty()) e.range = std::atof(rg.c_str());
    e.priority = lower(ini_->get(name, "Priority", e.priority));
    e.vshift = ini_->get_int(name, "VShift", e.vshift);
    const std::vector<std::string> fs = split_ws(ini_->get(name, "FShift", ""));
    if (fs.size() >= 2) {
        e.fshift_lo = std::atoi(fs[0].c_str());
        e.fshift_hi = std::atoi(fs[1].c_str());
    }
    const std::vector<std::string> dl = split_ws(ini_->get(name, "Delay", ""));
    if (!dl.empty()) {
        e.delay_lo = std::atoi(dl[0].c_str());
        e.delay_hi = dl.size() >= 2 ? std::atoi(dl[1].c_str()) : e.delay_lo;
    }
    for (const std::string& t : split_ws(ini_->get(name, "Control", "")))
        e.control.push_back(lower(t));
    e.attack_n = ini_->get_int(name, "Attack", 0);   // W7-12-24
    e.decay_n = ini_->get_int(name, "Decay", 0);
    const std::string ty = ini_->get(name, "Type", "");
    if (!ty.empty()) {
        e.type.clear();
        for (const std::string& t : split_ws(ty)) e.type.push_back(lower(t));
    }
    return e;
}
}  // namespace ra2assets
