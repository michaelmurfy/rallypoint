#pragma once
// Net L0 -- the original game's numeric primitives, bit-exact and cross-platform.
//
// [RE: docs/gap/net/03-sim-bitexact.md §2.1-§2.2, D1/D2/D9; gamemd.exe RE build
//  SHA-1 7BE4E550..., private Ghidra copy D:\ra2-re\l0, disassembly dis1.txt]
//
// gamemd.exe never calls the CRT sqrt/sin/cos/atan2 (zero FSQRT/FSIN/FCOS/FPATAN
// in .text). Every transcendental is a Westwood "FastMath" lookup into float32
// tables in initialised .data, and every double->int conversion is the CRT
// `_ftol` (FUN_007C4D00). This header ports those routines instruction by
// instruction, reading the tables copied verbatim from the binary
// (engine/gen/orig_math_tables.inc, produced by tools/extract_orig_math_tables.py).
//
// Determinism: the routines use only IEEE float/double + - * /, float<->double
// conversion and integer ops. With -ffp-contract=off (CMakeLists.txt) these are
// bit-identical on x86-64 SSE2 (desktop MinGW, Android x86_64) and arm64, so the
// SIM must call om:: instead of libm (enforced by tests/test_orig_math.cpp's
// banned-call scan). x87 note: the original evaluates under the Win32 default
// control word (PC=53, round-to-nearest) and _ftol's own 0x0E7F (PC=53,
// truncate), so each x87 op rounds to double exactly as the SSE2/NEON op does
// (outside the denormal range, which the sim never reaches).
//
// Results are returned as double: the original leaves the float table value in
// ST0, which a double holds exactly.
#include <cstdint>
#include <cstring>

namespace ra2engine {
namespace om {

// _ftol, FUN_007C4D00 (0x7C4D00): FISTP qword under CW 0x0E7F (truncate toward
// zero); callers keep EAX, so the int64 result WRAPS modulo 2^32. A value
// outside int64 or NaN gives the x87 "integer indefinite" 0x8000000000000000,
// whose low half is 0. Never UB, identical on every platform (unlike a C++
// static_cast<int>(double), which is UB out of range: x86 gives 0x80000000,
// arm64 saturates).
inline std::int32_t ftol(double x) {
    if (x >= -9223372036854775808.0 && x < 9223372036854775808.0) {
        const std::int64_t v = static_cast<std::int64_t>(x);
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(v)));
    }
    return 0;
}
inline std::int32_t ftol(float x) { return ftol(static_cast<double>(x)); }

// W9-05-13: the sim's std::lround replacement. std::lround returns `long`:
// 32-bit on Windows, 64-bit on Android, so a result beyond int32 (or a NaN)
// differed between the two builds (MinGW UCRT returns 0; bionic returns the
// 64-bit value, which the int cast then wraps). This rounds half away from
// zero exactly like std::lround and reproduces the Windows result everywhere:
// NaN, or a rounded value outside int32, gives 0.
inline std::int32_t lround(double x) {
    if (!(x >= -2147483649.0 && x <= 2147483648.0)) return 0;   // NaN / far out of range
    const std::int64_t t = static_cast<std::int64_t>(x);          // truncation, in range
    const double frac = x - static_cast<double>(t);               // exact
    const std::int64_t r = t + (frac >= 0.5 ? 1 : frac <= -0.5 ? -1 : 0);
    if (r < -2147483647LL - 1 || r > 2147483647LL) return 0;
    return static_cast<std::int32_t>(r);
}
// W9-05-13: a plain (int) truncation of a double that is defined for every
// input -- static_cast<int>(double) is UB out of range (x86 CVTTSD2SI gives
// 0x80000000, arm64 FCVTZS saturates, NaN -> 0). This is the x86 result on
// every platform: NaN / out of int32 range -> INT32_MIN.
inline std::int32_t itrunc(double x) {
    if (!(x > -2147483649.0 && x < 2147483648.0)) return -2147483647 - 1;
    return static_cast<std::int32_t>(x);
}
// W9-05-13: the ONE canonical bit pattern of a double for everything two
// peers compare (the save / full sync hash io_scalar AND the per-frame
// net_frame_crc): every NaN is the quiet NaN 0x7FF8000000000000 (an SSE
// default NaN is 0xFFF8..., an arm64 one 0x7FF8..., payloads vary); every
// other value keeps its exact IEEE bits (-0 stays -0: IEEE produces it the
// same way on every platform, and atan2 / division tell it from +0).
inline std::uint64_t canonical_bits(double d) {
    std::uint64_t b = 0x7FF8000000000000ull;
    if (d == d) std::memcpy(&b, &d, sizeof b);
    return b;
}
// Integral arguments are a type error: an integer->int cast is a plain
// static_cast, not _ftol (this also lets the sim's ftol sweep be compiler-checked).
template <class T> std::int32_t ftol(T) = delete;

// FastMath sqrt, FUN_004CAB60 (550 refs): 0 for 0/NaN; |x| (x * -1.0 when
// negative); rounded to float32; mantissa index = exponent parity | top 13
// mantissa bits into the int32[16384] table at 0x86403C; exponent halved.
// Max relative error 6.1e-5 (03 §2.2).
double sqrt(double x);

// FastMath sin / cos, FUN_004CABD0 / FUN_004CAC20: n = ftol(x * 2607.5945f);
// index = (n/2) mod 8192 (+0x800 for cos), +1 when n is odd (half-step
// rounding); float32[10241] table at 0x84E004. The float-argument twins
// FUN_004CB070 / FUN_004CB0C0 give identical results for (double)float.
double sin(double rad);
double cos(double rad);

// The raw 0x84E004 entry k (sin(k * 2pi/8192) as stored), k clamped to [0, 10240].
float sin_table(int k);

// FastMath tan, FUN_004CAC70: ftol(x * 4096/2pi) mod 4096, table 0x85C024.
double tan(double rad);

// FastMath asin / acos, FUN_004CACA0 / FUN_004CACD0: idx = ftol((x+1)*-2048),
// value = table[-idx] (0x858014, asin(j/2048-1)); acos = pi/2 - asin.
// Outside [-1-1/2048, 1+1/2048] the original reads past the table; the port
// clamps the index (documented deviation, never reached by valid inputs).
double asin(double x);
double acos(double x);

// FastMath atan, FUN_004CAD00: idx = |ftol(x / step)| (step = float at
// 0x864038), table 0x860034[4097], pi/2 beyond it; sign from (float)x.
double atan(double x);

// FastMath atan2, FUN_004CAD50 (y, x) -- also the float twin FUN_004CB2F0:
// both arguments rounded to float32 first; x == 0 -> 0 / +pi/2 / -pi/2 by the
// sign of y; else t = atan-table(|ftol((y/x)/step)|), pi - t when x < 0, and
// negated when y < 0.
double atan2(double y, double x);

// --- compositions (not single original routines) -----------------------------
// Distance the way the original measures it: FastSqrt of the double sum of
// squares (e.g. CoordStruct distance 0x41C380 etc. -> FUN_004CAB60).
inline double hypot(double dx, double dy) { return om::sqrt(dx * dx + dy * dy); }
inline double hypot3(double dx, double dy, double dz) { return om::sqrt(dx * dx + dy * dy + dz * dz); }
// b^n for an integer n by repeated multiplication (deterministic; replaces
// std::pow(b, int) in the sim). n < 0 -> 1 / b^|n|.
double ipow(double b, int n);

}  // namespace om
}  // namespace ra2engine
