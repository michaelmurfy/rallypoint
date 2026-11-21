#include "ra2assets/vxl.hpp"
#include <cstring>
#include <stdexcept>
#include <string>

namespace ra2assets {

// Fixed on-disk sizes of the VXL structures (little-endian, packed).
// Byte-verified against the real htnk.vxl (34,550 bytes):
//   802 + 1*28 + body 33,628 + 1*92 = 34,550 exactly.
// Cross-checked against the game's own loader (docs/re/vxl.md) and, off-repo,
// XCC misc/cc_structures.h (t_vxl_header / t_vxl_section_header /
// t_vxl_section_tailer).
//   file header (802): char magic[16]="Voxel Animation"; u32 palette_count(=1);
//     u32 section_header_count; u32 section_tailer_count; u32 body_size;
//     u8 palette_remap_start; u8 palette_remap_end; u8 palette[768] (6-bit RGB)
//   section header (28): char name[16]; u32 limb_number; u32 unknown1; u32 unknown2
//   section tailer (92): u32 span_start_ofs; u32 span_end_ofs; u32 span_data_ofs
//     (all relative to the body region); f32 det_scale; f32 transform[3][4]
//     (row-major); f32 min_bounds[3]; f32 max_bounds[3]; u8 size_x; u8 size_y;
//     u8 size_z; u8 normals_mode (2=TS, 4=RA2)
static constexpr size_t kFileHeader    = 802;
static constexpr size_t kPaletteOfs    = 34;
static constexpr size_t kSectionHeader = 28;
static constexpr size_t kSectionTailer = 92;
static const char kMagic[16] = "Voxel Animation";   // 15 chars + NUL

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
    while (n < 16 && p[n] != 0) n++;
    return std::string(reinterpret_cast<const char*>(p), n);
}

// Walk one section's voxel columns. Column i covers (x = i % size_x,
// y = i / size_x); its data at body + span_data_ofs + start[i] is a run of
// {skip, count, count*(color,normal), trailing-duplicate-count} packets along
// +z, and 0xFFFFFFFF in the start table marks an EMPTY column. This is the
// walk shape of the game's own loader (docs/re/vxl.md; cross-checked off-repo
// against XCC Cvxl_file). A column may end on a bare skip reaching size_z.
// Every byte is bounds-checked against the body region.
static void decode_spans(const uint8_t* body, size_t body_size, size_t sec_index,
                         uint32_t span_start_ofs, uint32_t span_data_ofs,
                         VxlSection& sec) {
    const size_t ncols = (size_t)sec.size_x * (size_t)sec.size_y;
    auto next_byte = [&](size_t& off, const char* what) -> uint8_t {
        if (off >= body_size)
            throw std::runtime_error("vxl: section " + std::to_string(sec_index) +
                " " + what + " at body offset " + std::to_string(off) +
                " exceeds body (" + std::to_string(body_size) + " bytes)");
        return body[off++];
    };
    for (size_t i = 0; i < ncols; i++) {
        const uint32_t start = r32(body + span_start_ofs + i * 4);
        if (start == 0xFFFFFFFFu)
            continue;                          // empty column
        const uint64_t data_off = (uint64_t)span_data_ofs + start;
        if (data_off >= (uint64_t)body_size)
            throw std::runtime_error("vxl: section " + std::to_string(sec_index) +
                " column " + std::to_string(i) + " span start " +
                std::to_string(start) + " exceeds body (" +
                std::to_string(body_size) + " bytes)");
        const int x = (int)(i % (size_t)sec.size_x);
        const int y = (int)(i / (size_t)sec.size_x);
        size_t off = (size_t)data_off;
        int z = 0;
        while (z < sec.size_z) {
            z += next_byte(off, "span skip byte");
            if (z >= sec.size_z)
                break;                         // column may end on a skip
            const int count = next_byte(off, "span voxel count");
            for (int j = 0; j < count; j++) {
                if (z >= sec.size_z)
                    throw std::runtime_error("vxl: section " + std::to_string(sec_index) +
                        " column " + std::to_string(i) +
                        " voxel run exceeds size_z " + std::to_string(sec.size_z));
                const uint8_t color  = next_byte(off, "voxel color");
                const uint8_t normal = next_byte(off, "voxel normal");
                VxlVoxel& v = sec.voxels[(size_t)x + (size_t)sec.size_x *
                                         ((size_t)y + (size_t)sec.size_y * (size_t)z)];
                v.color = color;
                v.normal = normal;
                v.present = true;
                z++;
            }
            next_byte(off, "span trailing count byte");  // consume + bounds-check
        }
    }
}

VxlModel load_vxl(const uint8_t* data, size_t len) {
    if (data == nullptr || len < kFileHeader)
        throw std::runtime_error("vxl: file smaller than 802-byte header (" +
                                 std::to_string(len) + " bytes)");
    if (std::memcmp(data, kMagic, 16) != 0)
        throw std::runtime_error("vxl: bad magic (expected \"Voxel Animation\")");
    const uint32_t palette_count = r32(data + 16);
    if (palette_count != 1)
        throw std::runtime_error("vxl: palette_count " +
            std::to_string(palette_count) + " != 1");
    const uint32_t n_headers = r32(data + 20);
    const uint32_t n_tailers = r32(data + 24);
    const uint32_t body_size = r32(data + 28);
    if (n_headers != n_tailers)
        throw std::runtime_error("vxl: section header count " +
            std::to_string(n_headers) + " != tailer count " + std::to_string(n_tailers));
    if (n_headers == 0)
        throw std::runtime_error("vxl: zero sections");
    // Plausibility cap on the attacker-controlled section count, mirroring the
    // guards in load_hva / AudioIdx::load: reject absurd counts up front with a
    // clear error instead of falling through to the size check or reserving a
    // huge section vector. Real models have a handful of sections.
    if (n_headers > 1000000u)
        throw std::runtime_error("vxl: implausible section count " +
                                 std::to_string(n_headers) + " (max 1000000)");
    const uint64_t expect = (uint64_t)kFileHeader +
                            (uint64_t)n_headers * kSectionHeader +
                            (uint64_t)body_size +
                            (uint64_t)n_tailers * kSectionTailer;
    if (expect != (uint64_t)len)
        throw std::runtime_error("vxl: size mismatch: header implies " +
            std::to_string(expect) + " bytes, file has " + std::to_string(len));

    VxlModel m;
    // 6-bit palette expanded exactly as the game does (<<2) by Palette::load.
    Palette pal = Palette::load(data + kPaletteOfs, 768);
    for (int i = 0; i < 256; i++) m.palette[i] = pal[i];

    const size_t body0 = kFileHeader + (size_t)n_headers * kSectionHeader;
    const uint8_t* body = data + body0;
    const size_t tail0 = body0 + (size_t)body_size;
    m.sections.reserve(n_headers);
    for (uint32_t si = 0; si < n_headers; si++) {
        const uint8_t* sh = data + kFileHeader + (size_t)si * kSectionHeader;
        const uint8_t* st = data + tail0 + (size_t)si * kSectionTailer;
        VxlSection sec;
        sec.name = name16(sh);
        const uint32_t span_start_ofs = r32(st + 0);
        const uint32_t span_end_ofs   = r32(st + 4);
        const uint32_t span_data_ofs  = r32(st + 8);
        sec.scale = rf32(st + 12);
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 4; c++)
                sec.transform[r][c] = rf32(st + 16 + (size_t)(r * 4 + c) * 4);
        for (int k = 0; k < 3; k++) {
            sec.min_bounds[k] = rf32(st + 64 + (size_t)k * 4);
            sec.max_bounds[k] = rf32(st + 76 + (size_t)k * 4);
        }
        sec.size_x = st[88];
        sec.size_y = st[89];
        sec.size_z = st[90];
        sec.normals_mode = st[91];

        const size_t ncols = (size_t)sec.size_x * (size_t)sec.size_y;
        if ((uint64_t)span_start_ofs + 4ull * ncols > (uint64_t)body_size)
            throw std::runtime_error("vxl: section " + std::to_string(si) +
                " span start table (" + std::to_string(4 * ncols) + " bytes at " +
                std::to_string(span_start_ofs) + ") exceeds body (" +
                std::to_string(body_size) + " bytes)");
        if ((uint64_t)span_end_ofs + 4ull * ncols > (uint64_t)body_size)
            throw std::runtime_error("vxl: section " + std::to_string(si) +
                " span end table (" + std::to_string(4 * ncols) + " bytes at " +
                std::to_string(span_end_ofs) + ") exceeds body (" +
                std::to_string(body_size) + " bytes)");
        if (span_data_ofs > body_size)
            throw std::runtime_error("vxl: section " + std::to_string(si) +
                " span data offset " + std::to_string(span_data_ofs) +
                " exceeds body (" + std::to_string(body_size) + " bytes)");

        sec.voxels.assign(ncols * (size_t)sec.size_z, VxlVoxel{0, 0, false});
        decode_spans(body, (size_t)body_size, si, span_start_ofs, span_data_ofs, sec);
        m.sections.push_back(std::move(sec));
    }
    return m;
}

} // namespace ra2assets
