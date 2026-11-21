#include "ra2assets/ufnt.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ra2assets {
namespace {
int32_t rd32(const uint8_t* p) {
    return static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24));
}
constexpr size_t kHeader = 0x1C;
constexpr size_t kTable = 65536 * 2;
}  // namespace

UFont UFont::load(std::vector<uint8_t> b) {
    if (b.size() < kHeader + kTable) throw std::runtime_error("ufnt: too short");
    if (std::memcmp(b.data(), "fonT", 4) != 0) throw std::runtime_error("ufnt: bad magic");
    UFont f;
    f.row_bytes_ = rd32(b.data() + 0x08);
    f.rows_ = rd32(b.data() + 0x0C);
    f.line_h_ = rd32(b.data() + 0x10);
    f.count_ = rd32(b.data() + 0x14);
    f.sym_bytes_ = rd32(b.data() + 0x18);
    if (f.row_bytes_ <= 0 || f.row_bytes_ > 8 || f.rows_ <= 0 || f.rows_ > 128 || f.count_ <= 0 ||
        f.sym_bytes_ != 1 + f.row_bytes_ * f.rows_)
        throw std::runtime_error("ufnt: inconsistent header");
    if (b.size() < kHeader + kTable + static_cast<size_t>(f.count_) * f.sym_bytes_)
        throw std::runtime_error("ufnt: truncated symbol data");
    f.data_ = std::move(b);
    return f;
}

const uint8_t* UFont::sym(uint32_t cp) const {
    if (cp > 0xFFFF || data_.empty()) return nullptr;
    const uint8_t* t = data_.data() + kHeader + cp * 2;
    const int ix = t[0] | (t[1] << 8);
    if (ix <= 0 || ix > count_) return nullptr;
    return data_.data() + kHeader + kTable + static_cast<size_t>(ix - 1) * sym_bytes_;
}

bool UFont::has(uint32_t cp) const { return sym(cp) != nullptr; }

int UFont::width(uint32_t cp) const {
    const uint8_t* s = sym(cp);
    return s ? s[0] : 0;
}

bool UFont::pixel(uint32_t cp, int x, int y) const {
    const uint8_t* s = sym(cp);
    if (!s || x < 0 || y < 0 || x >= s[0] || y >= rows_ || x >= row_bytes_ * 8) return false;
    const uint8_t* row = s + 1 + y * row_bytes_;
    return (row[x >> 3] >> (7 - (x & 7))) & 1;
}

std::vector<uint32_t> UFont::decode_utf8(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = static_cast<uint8_t>(s[i]);
        uint32_t cp = c;
        int n = 0;
        if (c >= 0xF0) { cp = c & 0x07; n = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
        ++i;
        for (int k = 0; k < n && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<uint8_t>(s[i]) & 0x3F);
        out.push_back(cp);
    }
    return out;
}

int UFont::text_width(const std::string& utf8) const {
    int best = 0, cur = 0;
    for (uint32_t cp : decode_utf8(utf8)) {
        if (cp == '\n') { best = std::max(best, cur); cur = 0; continue; }
        cur += width(cp);
    }
    return std::max(best, cur);
}

}  // namespace ra2assets
