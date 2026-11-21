#include "ra2assets/art.hpp"
#include <cctype>
#include <cstdlib>

namespace ra2assets {

namespace {

// Case-insensitive "yes"/"true" check, mirroring the game's own tolerant
// boolean parsing (Voxel=yes, Voxel=Yes, Voxel=TRUE all count).
bool is_truthy(const std::string& v) {
    std::string lower = v;
    for (char& c : lower) c = (char)std::tolower((unsigned char)c);
    return lower == "yes" || lower == "true";
}

// Strict int parse: the whole (trimmed) field must be an optionally-signed
// decimal integer, no trailing garbage. Used by fire_flh(), which must
// reject malformed FLH= entries rather than silently defaulting fields to 0.
bool parse_int_strict(const std::string& field, int& out) {
    size_t b = field.find_first_not_of(" \t");
    if (b == std::string::npos) return false;
    size_t e = field.find_last_not_of(" \t");
    std::string s = field.substr(b, e - b + 1);
    if (s.empty()) return false;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size()) return false;
    out = (int)v;
    return true;
}

} // namespace

Art Art::load(const Ini& ini) { return Art(ini); }

std::string Art::sequence_name(const std::string& image) const {
    return ini_->get(image, "Sequence", "");
}

ArtSequenceEntry Art::entry(const std::string& image, const std::string& name) const {
    ArtSequenceEntry e;
    const std::string seq = sequence_name(image);
    if (seq.empty()) return e;
    const std::string v = ini_->get(seq, name, "");
    if (v.empty()) return e;
    // Parse up to three leading comma-separated integers; later fields
    // (facing labels, sound tags -- e.g. "Idle1=56,15,0,S") are ignored.
    int out[3] = {0, 1, 0};
    std::size_t pos = 0;
    for (int i = 0; i < 3 && pos <= v.size(); ++i) {
        const std::size_t comma = v.find(',', pos);
        const std::string field = v.substr(pos, comma == std::string::npos ? std::string::npos
                                                                           : comma - pos);
        out[i] = std::atoi(field.c_str());
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    e.start = out[0];
    e.count = out[1] > 0 ? out[1] : 1;
    e.facing_mult = out[2];
    return e;
}

bool Art::voxel(const std::string& image) const {
    return is_truthy(ini_->get(image, "Voxel", ""));
}

bool Art::remapable(const std::string& image) const {
    return is_truthy(ini_->get(image, "Remapable", ""));
}

bool Art::fire_flh(const std::string& image, int flh[3]) const {
    std::string v = ini_->get(image, "PrimaryFireFLH", "");
    if (v.empty()) return false;
    int parsed[3];
    size_t pos = 0;
    int count = 0;
    while (count < 4) {  // stop early if a 4th field would exist (malformed)
        size_t comma = v.find(',', pos);
        std::string field = v.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (count < 3) {
            if (!parse_int_strict(field, parsed[count])) return false;
        }
        ++count;
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    if (count != 3) return false;  // require exactly 3 fields, not fewer/more
    flh[0] = parsed[0];
    flh[1] = parsed[1];
    flh[2] = parsed[2];
    return true;
}

bool Art::docking_offset(const std::string& image, int index, int xyz[3]) const {
    std::string v = ini_->get(image, "DockingOffset" + std::to_string(index), "");
    const size_t semi = v.find(';');
    if (semi != std::string::npos) v = v.substr(0, semi);
    int parsed[3];
    int count = 0;
    size_t pos = 0;
    while (true) {
        const size_t comma = v.find(',', pos);
        std::string field = v.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        const size_t b = field.find_first_not_of(" 	");
        const size_t e = field.find_last_not_of(" 	");
        field = (b == std::string::npos) ? std::string() : field.substr(b, e - b + 1);
        if (count >= 3 || !parse_int_strict(field, parsed[count])) return false;
        ++count;
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    if (count != 3) return false;
    xyz[0] = parsed[0];
    xyz[1] = parsed[1];
    xyz[2] = parsed[2];
    return true;
}

bool Art::foundation(const std::string& image, int& fw, int& fh) const {
    std::string v = ini_->get(image, "Foundation", "");
    if (v.empty()) return false;
    size_t xpos = v.find_first_of("xX");
    if (xpos == std::string::npos) return false;
    int w, h;
    if (!parse_int_strict(v.substr(0, xpos), w)) return false;
    if (!parse_int_strict(v.substr(xpos + 1), h)) return false;
    fw = w;
    fh = h;
    return true;
}

std::string Art::buildup(const std::string& image) const {
    return ini_->get(image, "Buildup", "");
}

std::string Art::cameo(const std::string& image) const {
    return ini_->get(image, "Cameo", "");
}

int Art::damage_levels(const std::string& image) const {
    const int v = std::atoi(ini_->get(image, "DamageLevels", "1").c_str());
    return v > 0 ? v : 1;
}

bool Art::new_theater(const std::string& image) const {
    return is_truthy(ini_->get(image, "NewTheater", ""));
}

std::string Art::palette(const std::string& image) const {
    std::string v = ini_->get(image, "Palette", "");
    const std::size_t sc = v.find(';');
    if (sc != std::string::npos) v.erase(sc);
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back();
    std::size_t i = 0;
    while (i < v.size() && std::isspace(static_cast<unsigned char>(v[i]))) ++i;
    return v.substr(i);
}

bool Art::theater(const std::string& image) const {
    return is_truthy(ini_->get(image, "Theater", ""));
}

}  // namespace ra2assets
