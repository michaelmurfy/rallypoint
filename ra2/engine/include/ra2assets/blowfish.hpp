#pragma once
#include <cstdint>
#include <cstddef>

namespace ra2assets {
class Blowfish {
public:
    void set_key(const uint8_t* key, size_t len);
    void encrypt_block(const uint8_t in[8], uint8_t out[8]) const;
    void decrypt_block(const uint8_t in[8], uint8_t out[8]) const;
    void decrypt(uint8_t* data, size_t len) const;
private:
    uint32_t P_[18];
    uint32_t S_[4][256];
    uint32_t f(uint32_t x) const;
};
} // namespace ra2assets
