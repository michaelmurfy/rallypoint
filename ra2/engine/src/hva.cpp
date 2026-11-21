#include "ra2assets/hva.hpp"
#include <cstring>
#include <stdexcept>
#include <string>

namespace ra2assets {

// On-disk HVA layout (little-endian, packed). Byte-verified against the real
// htnk.hva (88 bytes = 16 path + 4 frame_count + 4 section_count + 16 name +
// 48 matrix) and cross-checked against the game's own loader (docs/re/vxl.md):
//   0   char path[16]                          (content ignored)
//   16  u32 frame_count
//   20  u32 section_count
//   24  char section_names[section_count][16]
//   ..  f32 matrices[frames][sections][3][4]   frame-major, rows row-major
static constexpr size_t kHvaHeader  = 24;
static constexpr size_t kNameSize   = 16;
static constexpr size_t kMatrixSize = 48;   // 12 * f32

static uint32_t r32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static float rf32(const uint8_t* p) {
    uint32_t v = r32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
static std::string name16(const uint8_t* p) {
    size_t n = 0;
    while (n < kNameSize && p[n] != 0) n++;
    return std::string(reinterpret_cast<const char*>(p), n);
}

HvaModel load_hva(const uint8_t* data, size_t len) {
    if (data == nullptr || len < kHvaHeader)
        throw std::runtime_error("hva: file smaller than header (" +
                                 std::to_string(len) + " bytes)");
    const uint32_t frames   = r32(data + 16);
    const uint32_t sections = r32(data + 20);
    if (frames == 0 || sections == 0)
        throw std::runtime_error("hva: zero frame or section count (frames=" +
            std::to_string(frames) + ", sections=" + std::to_string(sections) + ")");
    if (frames > 1000000 || sections > 1000000)
        throw std::runtime_error("hva: implausible counts (frames=" +
            std::to_string(frames) + ", sections=" + std::to_string(sections) + ")");
    const uint64_t expect = (uint64_t)kHvaHeader + (uint64_t)kNameSize * sections +
                            (uint64_t)kMatrixSize * frames * sections;
    if (expect != (uint64_t)len)
        throw std::runtime_error("hva: size mismatch: header implies " +
            std::to_string(expect) + " bytes, file has " + std::to_string(len));

    HvaModel m;
    m.frame_count = (int)frames;
    m.section_names.reserve(sections);
    for (uint32_t s = 0; s < sections; s++)
        m.section_names.push_back(name16(data + kHvaHeader + (size_t)s * kNameSize));

    // Matrices are FRAME-major on disk (all sections of frame 0, then frame 1,
    // ...); each 3x4 matrix is row-major. See docs/re/vxl.md for the
    // frame-vs-section-major evidence trail.
    const size_t mat0 = kHvaHeader + (size_t)kNameSize * sections;
    m.frames.assign(frames, std::vector<HvaSectionXform>(sections));
    for (uint32_t f = 0; f < frames; f++)
        for (uint32_t s = 0; s < sections; s++) {
            const uint8_t* p = data + mat0 + ((size_t)f * sections + s) * kMatrixSize;
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 4; c++)
                    m.frames[f][s].transform[r][c] = rf32(p + (size_t)(r * 4 + c) * 4);
        }
    return m;
}

} // namespace ra2assets
