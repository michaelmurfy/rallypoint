// Net L0 -- port of the original gamemd.exe FastMath routines. See orig_math.hpp.
// Addresses: RE build SHA-1 7BE4E550..., disassembly of 0x4CAB60..0x4CAE01 and
// 0x7C4D00 (D:\ra2-re\l0-out\dis1.txt).
#include "orig_math.hpp"

#include <cstring>

namespace ra2engine {
namespace om {
namespace {

#include "gen/orig_math_tables.inc"

inline float f_of(std::uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
inline double d_of(std::uint64_t b) { double d; std::memcpy(&d, &b, 8); return d; }
inline std::uint32_t bits_of(float f) { std::uint32_t b; std::memcpy(&b, &f, 4); return b; }

// x87 FCOM/FCOMP set C3 for "equal OR unordered"; the original's "== 0" tests
// (TEST AH,0x40) therefore also take NaN down the zero path.
inline bool fcom_eq0(float v) { return !(v < 0.0f || v > 0.0f); }
inline bool fcom_eq0(double v) { return !(v < 0.0 || v > 0.0); }

// MSVC signed `n % m` (m a power of two) followed by the original's
// "if negative add m": the AND 0x8000xxxx / DEC / OR / INC / JNS / ADD idiom.
inline std::int32_t wrap_mod(std::int32_t n, std::int32_t m) {
    std::int32_t r = n % m;
    if (r < 0) r += m;
    return r;
}

double atan_lookup(double q) {
    // 0x4CAD0E..0x4CAD28: CALL _ftol; CDQ; XOR; SUB (abs); CMP EAX,0x1001; JGE.
    std::int32_t n = ftol(q);
    // abs() of INT_MIN stays negative: the original then reads table[INT_MIN]
    // (out of bounds); the port takes the beyond-table value instead.
    if (n == static_cast<std::int32_t>(0x80000000u)) return f_of(kOrigHalfPiFBits);
    if (n < 0) n = -n;
    return n < 0x1001 ? static_cast<double>(f_of(kOrigAtanTab[n])) : static_cast<double>(f_of(kOrigHalfPiFBits));
}

}  // namespace

double sqrt(double x) {
    // 0x4CAB60: FCOM 0.0f; equal (or unordered) -> return 0.0f.
    if (fcom_eq0(x)) return 0.0;
    // 0x4CAB7A: x <= 0 -> FMUL qword [0x7E3900] (-1.0).
    if (x <= 0.0) x = x * d_of(kOrigNegOneDBits);
    // 0x4CAB8D: FSTP dword -> round to float32.
    const std::uint32_t bits = bits_of(static_cast<float>(x));
    std::uint32_t m = bits & 0x7fffffu;
    const std::uint32_t e = (bits >> 23) - 0x7fu;                     // SHR 0x17; SUB 0x7F
    if (e & 1u) m |= 0x800000u;                                      // TEST AL,1; OR
    const std::int16_t e16 = static_cast<std::int16_t>(static_cast<std::uint16_t>(e & 0xffffu));
    const std::int32_t half = static_cast<std::int32_t>(static_cast<std::int16_t>(e16 >> 1));   // SAR AX,1; MOVSX
    const std::uint32_t out = static_cast<std::uint32_t>(kOrigSqrtTab[m >> 10]) +
                              (static_cast<std::uint32_t>(half + 0x7f) << 23);   // ADD 0x7F; SHL 0x17; ADD
    return static_cast<double>(f_of(out));
}

double sin(double rad) {
    // 0x4CABD0: FLD x; FMUL dword [0x8213C0]; CALL _ftol.
    const std::int32_t n = ftol(rad * static_cast<double>(f_of(kOrigSinScaleBits)));
    std::int32_t r = wrap_mod(n / 2, 0x2000);                         // CDQ; SUB; SAR 1; mod 8192
    if ((n & 1) && r < 0x1fff) ++r;                                  // half-step rounding
    return static_cast<double>(f_of(kOrigSinTab[r]));
}

double cos(double rad) {
    // 0x4CAC20: as sin with +0x800 (a quarter turn) folded into the wrap.
    const std::int32_t n = ftol(rad * static_cast<double>(f_of(kOrigCosScaleBits)));
    std::int32_t r = (n / 2) % 0x2000;
    r = r < 0 ? r + 0x2800 : r + 0x800;
    if ((n & 1) && r < 0x27ff) ++r;
    return static_cast<double>(f_of(kOrigSinTab[r]));
}

float sin_table(int k) {
    if (k < 0) k = 0;
    if (k > 10240) k = 10240;
    return f_of(kOrigSinTab[k]);
}

double tan(double rad) {
    // 0x4CAC70: FMUL qword [0x7E7970]; _ftol; mod 4096.
    const std::int32_t n = ftol(rad * d_of(kOrigTanScaleBits));
    return static_cast<double>(f_of(kOrigTanTab[wrap_mod(n, 0x1000)]));
}

namespace {
std::int32_t asin_index(double x) {
    // 0x4CACA0: FADD dword 1.0f; FMUL dword -2048.0f; _ftol; read base - 4*idx.
    std::int32_t j = -ftol((x + static_cast<double>(f_of(kOrigOneFBits))) * static_cast<double>(f_of(kOrigAsinScaleBits)));
    if (j < 0) j = 0;          // deviation: the original reads outside the table
    if (j > 4096) j = 4096;
    return j;
}
}  // namespace

double asin(double x) { return static_cast<double>(f_of(kOrigAsinTab[asin_index(x)])); }

double acos(double x) {
    // 0x4CACD0: ... FSUBR qword [0x7E1820] (pi/2 - asin).
    return d_of(kOrigHalfPiDBits) - static_cast<double>(f_of(kOrigAsinTab[asin_index(x)]));
}

double atan(double x) {
    // 0x4CAD00: FST dword (sign source); FDIV dword [0x864038]; lookup; FCHS if (float)x < 0.
    const float fx = static_cast<float>(x);
    double t = atan_lookup(x / static_cast<double>(f_of(kOrigAtanStepBits)));
    if (fx < 0.0f) t = -t;
    return t;
}

double atan2(double y, double x) {
    // 0x4CAD50: both args stored as float32 first.
    const float fy = static_cast<float>(y);
    const float fx = static_cast<float>(x);
    if (fcom_eq0(fx)) {
        if (fcom_eq0(fy)) return 0.0;
        // TEST AH,0x41 (C0|C3): y <= 0 -> -pi/2 else +pi/2.
        return fy <= 0.0f ? static_cast<double>(f_of(kOrigNegHalfPiFBits)) : static_cast<double>(f_of(kOrigHalfPiFBits));
    }
    // FLD (float)y; FDIV (float)x; FDIV step -- each rounded to double (PC=53).
    const double q = (static_cast<double>(fy) / static_cast<double>(fx)) / static_cast<double>(f_of(kOrigAtanStepBits));
    double t = atan_lookup(q);
    if (fx < 0.0f) t = d_of(kOrigPiDBits) - t;                       // FSUBR qword [0x7E34D0]
    if (fy < 0.0f || fy != fy) t = -t;                               // C0 also set when unordered
    return t;
}

double ipow(double b, int n) {
    double r = 1.0;
    const bool neg = n < 0;
    unsigned k = neg ? 0u - static_cast<unsigned>(n) : static_cast<unsigned>(n);
    for (; k; --k) r *= b;
    return neg ? 1.0 / r : r;
}

}  // namespace om
}  // namespace ra2engine
