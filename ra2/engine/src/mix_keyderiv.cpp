// mix_keyderiv.cpp
//
// INDEPENDENT CLEAN-ROOM IMPLEMENTATION of Westwood's MIX Blowfish key
// derivation. This file was written from scratch from (a) the public,
// freely usable RSA public-key algorithm (modular exponentiation of a
// big integer with exponent e = 65537), (b) the publicly documented Westwood
// MIX key-source framing, and (c) black-box input->output test vectors
// captured from our own game data. It is NOT derived from, and does not copy
// the structure, decomposition, naming, or expression of, the XCC Utilities
// (GPLv3) `mix_decode.cpp`, OpenRA's crypto sources, or any prior port. The
// only value taken from published references is the RSA public-key constant
// below, which is interoperability DATA (a fixed key baked into the shipped
// game), not source code.
//
// Algorithm, as implemented here:
//   * The public key is a base64 string that decodes to a tiny DER-style
//     blob: an INTEGER (tag 0x02, one length byte) whose 40 big-endian
//     content bytes are the RSA modulus n (a 319-bit number). The public
//     exponent is the standard e = 0x10001.
//   * The 80-byte key source is split into two 40-byte blocks. Each block is
//     read as a little-endian integer m, transformed by c = m^e mod n, and
//     the low 39 bytes of c (little-endian) are emitted. The two blocks'
//     outputs are concatenated and the first 56 bytes form the Blowfish key.
//
// The implementation is fully reentrant: it holds no file-scope mutable
// state, so no external locking is required.

#include "ra2assets/mix.hpp"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

namespace ra2assets::detail {
namespace {

// ---------------------------------------------------------------------------
// Minimal arbitrary-precision unsigned integer.
//
// Represented as a vector of 32-bit limbs, least-significant limb first, kept
// normalized (no trailing zero limbs; the empty vector denotes zero). Only the
// operations needed for RSA modular exponentiation are provided.
// ---------------------------------------------------------------------------
using Big = std::vector<uint32_t>;

void normalize(Big& a) {
    while (!a.empty() && a.back() == 0) a.pop_back();
}

// Returns -1 if a<b, 0 if a==b, +1 if a>b. Operands must be normalized.
int compare(const Big& a, const Big& b) {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

size_t bit_length(const Big& a) {
    if (a.empty()) return 0;
    uint32_t top = a.back();
    size_t bits = 0;
    while (top) { top >>= 1; ++bits; }
    return (a.size() - 1) * 32 + bits;
}

bool test_bit(const Big& a, size_t bit) {
    size_t limb = bit / 32;
    if (limb >= a.size()) return false;
    return (a[limb] >> (bit % 32)) & 1u;
}

// r = (r << 1) | low_bit
void shift_in_bit(Big& r, uint32_t low_bit) {
    uint32_t carry = low_bit & 1u;
    for (size_t i = 0; i < r.size(); ++i) {
        uint32_t next_carry = r[i] >> 31;
        r[i] = (r[i] << 1) | carry;
        carry = next_carry;
    }
    if (carry) r.push_back(carry);
}

// r -= b, requires r >= b.
void subtract_from(Big& r, const Big& b) {
    uint64_t borrow = 0;
    for (size_t i = 0; i < r.size(); ++i) {
        uint64_t rhs = (i < b.size() ? b[i] : 0u) + borrow;
        uint64_t cur = static_cast<uint64_t>(r[i]) - rhs;
        r[i] = static_cast<uint32_t>(cur);
        borrow = (cur >> 32) & 1u;  // 1 when the subtraction underflowed
    }
    normalize(r);
}

// Schoolbook product a * b.
Big multiply(const Big& a, const Big& b) {
    if (a.empty() || b.empty()) return Big{};
    Big r(a.size() + b.size(), 0u);
    for (size_t i = 0; i < a.size(); ++i) {
        uint64_t carry = 0;
        uint64_t ai = a[i];
        for (size_t j = 0; j < b.size(); ++j) {
            uint64_t cur = static_cast<uint64_t>(r[i + j]) + ai * b[j] + carry;
            r[i + j] = static_cast<uint32_t>(cur);
            carry = cur >> 32;
        }
        // carry < 2^32 here, and r[i + b.size()] is untouched until now.
        r[i + b.size()] = static_cast<uint32_t>(carry);
    }
    normalize(r);
    return r;
}

// x mod m, by bitwise long division (m must be non-zero).
Big modulo(const Big& x, const Big& m) {
    Big r;
    for (size_t i = bit_length(x); i-- > 0;) {
        shift_in_bit(r, test_bit(x, i) ? 1u : 0u);
        if (compare(r, m) >= 0) subtract_from(r, m);  // r < 2m before this
    }
    normalize(r);
    return r;
}

// base^exp mod m, square-and-multiply.
Big mod_pow(const Big& base, uint32_t exp, const Big& m) {
    Big result{1u};
    Big b = modulo(base, m);
    while (exp) {
        if (exp & 1u) result = modulo(multiply(result, b), m);
        exp >>= 1;
        if (exp) b = modulo(multiply(b, b), m);
    }
    normalize(result);
    return result;
}

// ---------------------------------------------------------------------------
// Byte <-> Big conversions.
// ---------------------------------------------------------------------------
Big big_from_little_endian(const uint8_t* p, size_t len) {
    Big a((len + 3) / 4, 0u);
    for (size_t i = 0; i < len; ++i) {
        a[i / 4] |= static_cast<uint32_t>(p[i]) << (8 * (i % 4));
    }
    normalize(a);
    return a;
}

Big big_from_big_endian(const uint8_t* p, size_t len) {
    Big a((len + 3) / 4, 0u);
    for (size_t i = 0; i < len; ++i) {
        size_t weight = len - 1 - i;  // byte position from the least end
        a[weight / 4] |= static_cast<uint32_t>(p[i]) << (8 * (weight % 4));
    }
    normalize(a);
    return a;
}

// Writes exactly `count` little-endian bytes of `a`, zero-padded at the top.
void big_to_little_endian(const Big& a, uint8_t* out, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        size_t limb = i / 4;
        uint32_t v = (limb < a.size()) ? a[limb] : 0u;
        out[i] = static_cast<uint8_t>((v >> (8 * (i % 4))) & 0xFFu);
    }
}

// ---------------------------------------------------------------------------
// Public key.
// ---------------------------------------------------------------------------
// Published Westwood MIX public key (interoperability data, standard base64).
constexpr char kPublicKey[] =
    "AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V";

int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;  // padding / stray characters are ignored
}

std::vector<uint8_t> base64_decode(const char* s) {
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int bits = 0;
    for (const char* p = s; *p; ++p) {
        int v = base64_value(*p);
        if (v < 0) continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFFu));
        }
    }
    return out;
}

// Parses the RSA modulus from the published public key. The decoded blob is a
// DER INTEGER: tag byte 0x02, then a single length byte, then that many
// big-endian content bytes.
Big public_modulus() {
    std::vector<uint8_t> blob = base64_decode(kPublicKey);
    if (blob.size() < 2 || blob[0] != 0x02) return Big{};
    size_t len = blob[1];
    if (blob.size() < 2 + len) len = blob.size() - 2;
    return big_from_big_endian(blob.data() + 2, len);
}

constexpr uint32_t kPublicExponent = 0x10001u;  // 65537
constexpr size_t kBlockIn = 40;                 // bytes consumed per RSA block
constexpr size_t kBlockOut = 39;                // bytes emitted per RSA block
constexpr size_t kKeyBytes = 56;                // Blowfish key length

}  // namespace

size_t westwood_blowfish_key(const uint8_t src[80], uint8_t out_key[56]) {
    const Big n = public_modulus();
    size_t written = 0;
    for (int block = 0; block < 2 && written < kKeyBytes; ++block) {
        Big m = big_from_little_endian(src + block * kBlockIn, kBlockIn);
        Big c = mod_pow(m, kPublicExponent, n);
        uint8_t chunk[kBlockOut];
        big_to_little_endian(c, chunk, kBlockOut);
        for (size_t i = 0; i < kBlockOut && written < kKeyBytes; ++i) {
            out_key[written++] = chunk[i];
        }
    }
    return written;
}

}  // namespace ra2assets::detail
