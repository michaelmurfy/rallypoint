#include "ra2assets/vpl.hpp"
#include <cstring>
#include <stdexcept>
#include <string>

namespace ra2assets {

namespace {
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

Vpl Vpl::load(const uint8_t* data, size_t len) {
    if (len < 16) throw std::runtime_error("vpl: truncated header (" + std::to_string(len) + " bytes)");
    Vpl v;
    const uint32_t rs = rd32(data), re = rd32(data + 4), sc = rd32(data + 8);
    if (sc == 0 || sc > 256)
        throw std::runtime_error("vpl: implausible section_count " + std::to_string(sc));
    const size_t want = 16 + 768 + static_cast<size_t>(sc) * 256;
    if (len != want)
        throw std::runtime_error("vpl: size mismatch: have " + std::to_string(len) +
                                 ", layout wants " + std::to_string(want));
    v.remap_start = static_cast<uint8_t>(rs);
    v.remap_end = static_cast<uint8_t>(re);
    v.section_count = sc;
    v.palette = Palette::load(data + 16, 768);   // same 6-bit <<2 path as .pal
    v.tables.resize(sc);
    for (uint32_t i = 0; i < sc; ++i)
        std::memcpy(v.tables[i].data(), data + 16 + 768 + static_cast<size_t>(i) * 256, 256);
    return v;
}

}  // namespace ra2assets
