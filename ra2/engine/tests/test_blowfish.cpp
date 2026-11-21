#include "doctest.h"
#include "ra2assets/blowfish.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>

using ra2assets::Blowfish;

static void hex_to_bytes(const char* hex, uint8_t* out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned b; sscanf(hex + i * 2, "%2x", &b); out[i] = (uint8_t)b;
    }
}

TEST_CASE("blowfish known-answer vectors (Eric Young)") {
    struct KAT { const char* key; const char* clear; const char* cipher; };
    KAT kats[] = {
        {"0000000000000000", "0000000000000000", "4EF997456198DD78"},
        {"FFFFFFFFFFFFFFFF", "FFFFFFFFFFFFFFFF", "51866FD5B85ECB8A"},
        {"3000000000000000", "1000000000000001", "7D856F9A613063F2"},
        {"1111111111111111", "1111111111111111", "2466DD878B963C9D"},
        {"FEDCBA9876543210", "0123456789ABCDEF", "0ACEAB0FC6A0A28D"},
    };
    for (auto& k : kats) {
        uint8_t key[8], clr[8], exp[8], out[8];
        hex_to_bytes(k.key, key, 8);
        hex_to_bytes(k.clear, clr, 8);
        hex_to_bytes(k.cipher, exp, 8);
        Blowfish bf; bf.set_key(key, 8);
        bf.encrypt_block(clr, out);
        CHECK(std::memcmp(out, exp, 8) == 0);
        uint8_t back[8];
        bf.decrypt_block(exp, back);
        CHECK(std::memcmp(back, clr, 8) == 0);
    }
}

TEST_CASE("blowfish bulk decrypt processes whole blocks and leaves the tail untouched") {
    // Create a Blowfish with the KAT key
    uint8_t key[8];
    hex_to_bytes("FEDCBA9876543210", key, 8);
    Blowfish bf;
    bf.set_key(key, 8);

    // Build a 29-byte buffer: 3 plaintext blocks (24 bytes) + 5 tail bytes
    uint8_t plaintext[29];
    for (size_t i = 0; i < 24; i++) {
        plaintext[i] = (uint8_t)(i * 11 + 1);
    }
    // Tail bytes: 0xA0, 0xA1, 0xA2, 0xA3, 0xA4
    for (size_t i = 0; i < 5; i++) {
        plaintext[24 + i] = (uint8_t)(0xA0 + i);
    }

    // Encrypt each of the 3 blocks individually into a second buffer
    uint8_t encrypted[29];
    for (size_t i = 0; i < 3; i++) {
        bf.encrypt_block(&plaintext[i * 8], &encrypted[i * 8]);
    }
    // Copy the 5 tail bytes through unchanged
    std::memcpy(&encrypted[24], &plaintext[24], 5);

    // Call decrypt on the encrypted buffer (in place)
    bf.decrypt(encrypted, 29);

    // CHECK the first 24 bytes equal the original plaintext
    CHECK(std::memcmp(encrypted, plaintext, 24) == 0);
    // CHECK the last 5 bytes are still the original tail values
    CHECK(std::memcmp(&encrypted[24], &plaintext[24], 5) == 0);
}
