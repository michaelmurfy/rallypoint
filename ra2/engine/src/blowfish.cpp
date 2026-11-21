#include "ra2assets/blowfish.hpp"
#include "blowfish_tables.inc"   // P_INIT[18], S_INIT[4][256] — pi digits, public constants

namespace ra2assets {

uint32_t Blowfish::f(uint32_t x) const {
    uint32_t a = S_[0][(x >> 24) & 0xff];
    uint32_t b = S_[1][(x >> 16) & 0xff];
    uint32_t c = S_[2][(x >> 8) & 0xff];
    uint32_t d = S_[3][x & 0xff];
    return ((a + b) ^ c) + d;
}

void Blowfish::set_key(const uint8_t* key, size_t len) {
    for (int i = 0; i < 18; i++) P_[i] = P_INIT[i];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 256; j++) S_[i][j] = S_INIT[i][j];

    size_t k = 0;
    for (int i = 0; i < 18; i++) {
        uint32_t d = 0;
        for (int j = 0; j < 4; j++) { d = (d << 8) | key[k]; k = (k + 1) % len; }
        P_[i] ^= d;
    }
    uint32_t l = 0, r = 0;
    auto enc = [&](uint32_t& L, uint32_t& R) {
        for (int i = 0; i < 16; i += 2) {
            L ^= P_[i];   R ^= f(L);
            R ^= P_[i+1]; L ^= f(R);
        }
        uint32_t t = L; L = R ^ P_[17]; R = t ^ P_[16];
    };
    for (int i = 0; i < 18; i += 2) { enc(l, r); P_[i] = l; P_[i+1] = r; }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 256; j += 2) { enc(l, r); S_[i][j] = l; S_[i][j+1] = r; }
}

static uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
static void wr32(uint8_t* p, uint32_t v) {
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

void Blowfish::encrypt_block(const uint8_t in[8], uint8_t out[8]) const {
    uint32_t L = be32(in), R = be32(in + 4);
    for (int i = 0; i < 16; i += 2) {
        L ^= P_[i];   R ^= f(L);
        R ^= P_[i+1]; L ^= f(R);
    }
    uint32_t t = L; L = R ^ P_[17]; R = t ^ P_[16];
    wr32(out, L); wr32(out + 4, R);
}

void Blowfish::decrypt_block(const uint8_t in[8], uint8_t out[8]) const {
    uint32_t L = be32(in), R = be32(in + 4);
    for (int i = 16; i > 0; i -= 2) {
        L ^= P_[i+1]; R ^= f(L);
        R ^= P_[i];   L ^= f(R);
    }
    uint32_t t = L; L = R ^ P_[0]; R = t ^ P_[1];
    wr32(out, L); wr32(out + 4, R);
}

void Blowfish::decrypt(uint8_t* data, size_t len) const {
    for (size_t o = 0; o + 8 <= len; o += 8) decrypt_block(data + o, data + o);
}

} // namespace ra2assets
