#include "ra2assets/pcx.hpp"
#include <cstring>
#include <stdexcept>

namespace ra2assets {
namespace {
uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
}  // namespace

PcxImage decode_pcx(const uint8_t* d, std::size_t len) {
    if (len < 128) throw std::runtime_error("pcx: shorter than header");
    if (d[0] != 0x0A) throw std::runtime_error("pcx: bad manufacturer (!=0x0A)");
    const uint8_t encoding = d[2];      // 1 = RLE
    const uint8_t bpp = d[3];           // bits per pixel per plane
    if (encoding != 1) throw std::runtime_error("pcx: not RLE-encoded");
    const int xmin = rd16(d + 4), ymin = rd16(d + 6);
    const int xmax = rd16(d + 8), ymax = rd16(d + 10);
    const uint8_t planes = d[65];
    const int bpl = rd16(d + 66);       // bytes per scanline per plane (may pad)
    if (bpp != 8 || planes != 1)
        throw std::runtime_error("pcx: only 8-bit single-plane supported");
    PcxImage img;
    img.w = xmax - xmin + 1;
    img.h = ymax - ymin + 1;
    if (img.w <= 0 || img.h <= 0) throw std::runtime_error("pcx: bad dimensions");
    img.indices.assign(static_cast<size_t>(img.w) * img.h, 0);

    // RLE body starts at 128. Each scanline decodes bpl bytes; the first `w`
    // are the pixels (the rest is padding, discarded).
    std::size_t pos = 128;
    for (int row = 0; row < img.h; ++row) {
        int x = 0;
        while (x < bpl) {
            if (pos >= len) throw std::runtime_error("pcx: truncated RLE body");
            uint8_t b = d[pos++];
            int count = 1;
            if ((b & 0xC0) == 0xC0) {           // run: low 6 bits = count
                count = b & 0x3F;
                if (pos >= len) throw std::runtime_error("pcx: truncated RLE run");
                b = d[pos++];
            }
            for (int k = 0; k < count && x < bpl; ++k, ++x)
                if (x < img.w) img.indices[static_cast<size_t>(row) * img.w + x] = b;
        }
    }

    // Trailer palette: last 769 bytes = 0x0C marker + 256*3 RGB (FULL 8-bit,
    // NOT 6-bit VGA -- do NOT route through Palette::load, which does v<<2).
    if (len < 769) throw std::runtime_error("pcx: no trailer palette");
    const uint8_t* pal = d + (len - 769);
    if (pal[0] != 0x0C) throw std::runtime_error("pcx: trailer palette marker != 0x0C");
    const uint8_t* rgb = pal + 1;
    for (int i = 0; i < 256; ++i)
        img.palette[i] = Rgba{rgb[i * 3 + 0], rgb[i * 3 + 1], rgb[i * 3 + 2], 255};
    return img;
}

std::vector<uint8_t> PcxImage::rgba() const {
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < indices.size(); ++i) {
        const Rgba& c = palette[indices[i]];
        out[i * 4 + 0] = c.r; out[i * 4 + 1] = c.g; out[i * 4 + 2] = c.b; out[i * 4 + 3] = 255;
    }
    return out;
}

}  // namespace ra2assets
