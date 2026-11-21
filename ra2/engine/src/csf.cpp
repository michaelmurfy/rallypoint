#include "ra2assets/csf.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace ra2assets {

// On-disk CSF layout (little-endian, packed). Byte-verified against the
// install's ra2.csf (491,826 bytes, version 3, 4562 labels -- every label of the
// real file walks cleanly with exactly this structure); game-loader RE
// evidence in docs/re/csf.md.
//
//   file header, 24 bytes:
//     char magic[4] = " FSC"   @0   (bytes 0x20 0x46 0x53 0x43)
//     u32  version             @4   (3 in ra2.csf; not validated here)
//     u32  num_labels          @8
//     u32  num_strings         @12  (ignored)
//     u32  unused              @16  (ignored)
//     u32  language            @20  (ignored)
//   then num_labels sequential label records:
//     char magic[4] = " LBL"; u32 num_values; u32 label_len;
//     label_len bytes of ASCII label (NOT NUL-terminated)
//   each label is followed by num_values value records:
//     char magic[4] = " RTS" (or "WRTS"); u32 val_len (UTF-16 code units);
//     val_len*2 bytes UTF-16LE with every byte XOR 0xFF (== bitwise NOT of
//     each 16-bit unit); a "WRTS" record additionally carries
//     u32 extra_len + extra_len ASCII bytes (skipped).
static constexpr size_t kFileHeader  = 24;
static constexpr size_t kLabelHeader = 12;  // " LBL" + num_values + label_len
static constexpr size_t kValueHeader = 8;   // magic + val_len

static uint32_t r32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static std::string upper_ascii(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        c = (char)std::toupper((unsigned char)c);
    return out;
}

CsfStrings CsfStrings::load(const uint8_t* data, size_t len) {
    CsfStrings cs;
    if (len < kFileHeader)
        throw std::runtime_error("csf: file smaller than header (" +
                                 std::to_string(len) + " bytes)");
    if (!(data[0]==0x20 && data[1]==0x46 && data[2]==0x53 && data[3]==0x43))
        throw std::runtime_error("csf: bad file magic (want \" FSC\")");
    const uint32_t num_labels = r32(data + 8);
    // Capped reserve: num_labels is attacker-controlled input.
    cs.table_.reserve(std::min<uint32_t>(num_labels, 65536u));

    size_t pos = kFileHeader;                  // invariant: pos <= len
    for (uint32_t li = 0; li < num_labels; li++) {
        if (kLabelHeader > len - pos)
            throw std::runtime_error("csf: label record " + std::to_string(li) +
                " header at " + std::to_string(pos) + " exceeds file");
        if (!(data[pos]==0x20 && data[pos+1]==0x4C && data[pos+2]==0x42 && data[pos+3]==0x4C))
            throw std::runtime_error("csf: bad label magic at offset " +
                std::to_string(pos) + " (label " + std::to_string(li) + ")");
        const uint32_t num_values = r32(data + pos + 4);
        const uint32_t label_len  = r32(data + pos + 8);
        pos += kLabelHeader;
        if (label_len > len - pos)
            throw std::runtime_error("csf: label " + std::to_string(li) +
                " name (" + std::to_string(label_len) + " bytes at " +
                std::to_string(pos) + ") exceeds file");
        std::string label((const char*)data + pos, label_len);
        pos += label_len;

        for (uint32_t vi = 0; vi < num_values; vi++) {
            if (kValueHeader > len - pos)
                throw std::runtime_error("csf: value record header for label \"" +
                    label + "\" at " + std::to_string(pos) + " exceeds file");
            const uint8_t* m = data + pos;
            bool wide = false;
            if (m[0]==0x57 && m[1]==0x52 && m[2]==0x54 && m[3]==0x53) {        // "WRTS"
                wide = true;
            } else if (!(m[0]==0x20 && m[1]==0x52 && m[2]==0x54 && m[3]==0x53)) { // " RTS"
                throw std::runtime_error("csf: bad value magic at offset " +
                    std::to_string(pos) + " (label \"" + label + "\", value " +
                    std::to_string(vi) + ")");
            }
            const uint32_t val_len = r32(data + pos + 4);
            pos += kValueHeader;
            const size_t val_bytes = (size_t)val_len * 2;
            if (val_bytes > len - pos)
                throw std::runtime_error("csf: value " + std::to_string(vi) +
                    " of label \"" + label + "\" (" + std::to_string(val_bytes) +
                    " bytes at " + std::to_string(pos) + ") exceeds file");
            if (vi == 0) {
                // UTF-16LE with every byte XOR 0xFF (= ~ per 16-bit unit).
                std::u16string text;
                text.reserve(val_len);
                for (uint32_t u = 0; u < val_len; u++) {
                    const uint16_t lo = (uint16_t)(data[pos + 2*(size_t)u]     ^ 0xFF);
                    const uint16_t hi = (uint16_t)(data[pos + 2*(size_t)u + 1] ^ 0xFF);
                    text.push_back((char16_t)(lo | (hi << 8)));
                }
                cs.table_.emplace(upper_ascii(label), std::move(text));
            }
            pos += val_bytes;

            if (wide) {
                // "WRTS" trailing extra ASCII payload: skipped (only the
                // UTF-16 text is in scope), but bounds-walked to stay in
                // sync with the next record.
                if (4 > len - pos)
                    throw std::runtime_error("csf: WRTS extra length at " +
                        std::to_string(pos) + " exceeds file (label \"" +
                        label + "\")");
                const uint32_t extra_len = r32(data + pos);
                pos += 4;
                if (extra_len > len - pos)
                    throw std::runtime_error("csf: WRTS extra data (" +
                        std::to_string(extra_len) + " bytes at " +
                        std::to_string(pos) + ") exceeds file (label \"" +
                        label + "\")");
                pos += extra_len;
            }
        }
    }
    return cs;
}

const std::u16string* CsfStrings::find(const std::string& label) const {
    auto it = table_.find(upper_ascii(label));
    return it == table_.end() ? nullptr : &it->second;
}

} // namespace ra2assets
