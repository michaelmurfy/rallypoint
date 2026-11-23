#!/usr/bin/env python3
"""Generate engine/orig_math.cpp's lookup tables (Rallypoint).

Writes the same names, sizes and formats the FastMath routines in
engine/orig_math.cpp read, but every value is OUR OWN computation from the
mathematical definition, not a copy of any game's data:

  kOrigSinTab  float32[10241]  sin(k * 2pi / 8192)          (cos = entry k + 0x800)
  kOrigAsinTab float32[4097]   asin(j / 2048 - 1)
  kOrigTanTab  float32[4097]   tan(k * 2pi / 4096)          (poles: FLT_MAX)
  kOrigAtanTab float32[4097]   atan(i * step), step = 100 / 4096
  kOrigSqrtTab int32[16384]    mantissa bits of sqrt(m * 2^p) for the first m of
                               each bucket (index = p << 13 | top 13 mantissa bits),
                               so exact squares such as 25 or 100 stay exact

All arithmetic is Python integers (fixed point, 2^-160) and every value is
rounded to float32 / float64 by round-half-even on exact integers, so the
output is identical on every machine. The values are correctly rounded (the
2^-160 margin never decides a rounding here), which makes them differ from the
original game's tables in the last bits: a game simulated with them is
identical between our builds, not bit-identical with gamemd.exe.

usage: gen_math_tables.py <out.inc>
"""
import sys

P = 160                      # fixed-point fraction bits
ONE = 1 << P


def isqrt(n):
    if n < 0:
        raise ValueError(n)
    if n == 0:
        return 0
    x = 1 << ((n.bit_length() + 1) // 2)
    while True:
        y = (x + n // x) // 2
        if y >= x:
            return x
        x = y


def fx_div(a, b):            # fixed / fixed, rounded to nearest
    return (2 * a * ONE + b) // (2 * b)


def fx_mul(a, b):
    return (a * b + (ONE >> 1)) >> P


def atan_small(x):           # |x| <= ~0.2, fixed point
    x2 = fx_mul(x, x)
    term, s, n = x, 0, 0
    while term:
        s += term // (2 * n + 1) if n % 2 == 0 else -(term // (2 * n + 1))
        term = fx_mul(term, x2)
        n += 1
    return s


def atan_fx(x):              # x >= 0, fixed point
    if x > ONE:
        return PI_FX // 2 - atan_fx(fx_div(ONE, x))
    for _ in range(2):       # atan(x) = 2 atan(x / (1 + sqrt(1 + x^2)))
        x = fx_div(x, ONE + isqrt(ONE * ONE + x * x))
    return 4 * atan_small(x)


def atan_inv(n):             # atan(1/n), integer n
    x = ONE // n
    s, k, sign = 0, 1, 1
    n2 = n * n
    while x:
        s += sign * (x // k)
        x //= n2
        k += 2
        sign = -sign
    return s


PI_FX = 4 * (4 * atan_inv(5) - atan_inv(239))


def sin_series(x):           # |x| <= pi/4
    x2 = fx_mul(x, x)
    term, s, n = x, 0, 1
    while term:
        s += term
        term = -fx_mul(term, x2) // ((n + 1) * (n + 2))
        n += 2
    return s


def cos_series(x):
    x2 = fx_mul(x, x)
    term, s, n = ONE, 0, 0
    while term:
        s += term
        term = -fx_mul(term, x2) // ((n + 1) * (n + 2))
        n += 2
    return s


def sin_turn(k, steps):      # sin(2 pi k / steps), exact at the quadrant points
    k %= steps
    q, r = divmod(k * 8, steps)   # octant
    if r == 0 and q % 2 == 0:
        return [0, ONE, 0, -ONE][q // 2]
    # angle within the quadrant, folded to [0, pi/4] by the cofunction
    quad, rem = divmod(4 * k, steps)          # rem / steps quarter turns
    a = (PI_FX * rem) // (2 * steps)
    if 2 * rem <= steps:
        s = sin_series(a)
        c = cos_series(a)
    else:
        b = PI_FX // 2 - a
        s = cos_series(b)
        c = sin_series(b)
    return [s, c, -s, -c][quad]


def f32_bits(num, den):      # round num/den to float32, half to even
    if num == 0:
        return 0
    sign = 0x80000000 if (num < 0) != (den < 0) else 0
    num, den = abs(num), abs(den)
    e = num.bit_length() - den.bit_length()
    if (num << max(0, -e)) < (den << max(0, e)):
        e -= 1
    sh = 23 - e
    n2, d2 = (num << sh, den) if sh >= 0 else (num, den << -sh)
    q, r = divmod(n2, d2)
    if 2 * r > d2 or (2 * r == d2 and q & 1):
        q += 1
    if q == 1 << 24:
        q >>= 1
        e += 1
    assert -126 <= e <= 127, e
    return sign | ((e + 127) << 23) | (q - (1 << 23))


def f64_bits(num, den):
    if num == 0:
        return 0
    sign = 1 << 63 if (num < 0) != (den < 0) else 0
    num, den = abs(num), abs(den)
    e = num.bit_length() - den.bit_length()
    if (num << max(0, -e)) < (den << max(0, e)):
        e -= 1
    sh = 52 - e
    n2, d2 = (num << sh, den) if sh >= 0 else (num, den << -sh)
    q, r = divmod(n2, d2)
    if 2 * r > d2 or (2 * r == d2 and q & 1):
        q += 1
    if q == 1 << 53:
        q >>= 1
        e += 1
    return sign | ((e + 1023) << 52) | (q - (1 << 52))


FLT_MAX_BITS = 0x7F7FFFFF
ATAN_STEP = (100, 4096)      # our own choice of table range: atan over [0, 100]


def tables():
    sin = [f32_bits(sin_turn(k, 8192), ONE) for k in range(10241)]
    tan = []
    for k in range(4097):
        s, c = sin_turn(k, 4096), sin_turn(k + 1024, 4096)
        tan.append(FLT_MAX_BITS if c == 0 else f32_bits(s, c))
    asin = []
    for j in range(4097):
        y = (j - 2048) * ONE // 2048                     # exact: 2048 divides 2^P
        if abs(y) == ONE:
            v = PI_FX // 2 if y > 0 else -(PI_FX // 2)
        else:
            t = atan_fx(fx_div(abs(y), isqrt(ONE * ONE - y * y)))
            v = t if y >= 0 else -t
        asin.append(f32_bits(v, ONE))
    atan = [f32_bits(atan_fx(i * ATAN_STEP[0] * ONE // ATAN_STEP[1]), ONE) for i in range(4097)]
    sqrt = []
    for i in range(16384):
        par, top = i >> 13, i & 8191
        n = ((1 << 23) + (top << 10)) << (23 + par)          # bucket start, * 2^46
        r = isqrt(n)
        if n - r * r > r:                                    # round to nearest
            r += 1
        sqrt.append(r - (1 << 23))
    return sin, asin, tan, atan, sqrt


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    sin, asin, tan, atan, sqrt = tables()
    half_pi = PI_FX // 2
    scalars = [
        ("std::uint32_t", "kOrigAtanStepBits", f32_bits(*ATAN_STEP), "atan table step: 100 / 4096"),
        ("std::uint32_t", "kOrigSinScaleBits", f32_bits(16384 * ONE, 2 * PI_FX), "sin: x * 16384 / 2pi"),
        ("std::uint32_t", "kOrigCosScaleBits", f32_bits(16384 * ONE, 2 * PI_FX), "cos: x * 16384 / 2pi"),
        ("std::uint64_t", "kOrigTanScaleBits", f64_bits(4096 * ONE, 2 * PI_FX), "tan: x * 4096 / 2pi"),
        ("std::uint32_t", "kOrigAsinScaleBits", f32_bits(-2048, 1), "asin/acos: (x + 1) * -2048"),
        ("std::uint32_t", "kOrigOneFBits", f32_bits(1, 1), "1.0f"),
        ("std::uint64_t", "kOrigHalfPiDBits", f64_bits(half_pi, ONE), "pi/2 (double)"),
        ("std::uint64_t", "kOrigPiDBits", f64_bits(PI_FX, ONE), "pi (double)"),
        ("std::uint32_t", "kOrigHalfPiFBits", f32_bits(half_pi, ONE), "pi/2 (float)"),
        ("std::uint32_t", "kOrigNegHalfPiFBits", f32_bits(-half_pi, ONE), "-pi/2 (float)"),
        ("std::uint64_t", "kOrigNegOneDBits", f64_bits(-1, 1), "-1.0"),
        ("std::uint32_t", "kOrigFtolCw", 0x0E7F, "x87 control word: truncate, 53-bit precision"),
    ]
    out = ["// GENERATED by tools/gen_math_tables.py at configure time -- DO NOT EDIT.",
           "// Our own correctly rounded values, computed with exact integer arithmetic;",
           "// not data from any game. Included ONLY by engine/orig_math.cpp.", ""]
    for ctype, name, v, what in scalars:
        suffix = "ull" if ctype.endswith("64_t") else "u"
        out.append("constexpr %s %s = 0x%0*x%s;   // %s" % (ctype, name, 16 if suffix == "ull" else 8, v, suffix, what))
    out.append("")
    for name, ctype, vals in (("kOrigSinTab", "std::uint32_t", sin), ("kOrigAsinTab", "std::uint32_t", asin),
                              ("kOrigTanTab", "std::uint32_t", tan), ("kOrigAtanTab", "std::uint32_t", atan),
                              ("kOrigSqrtTab", "std::int32_t", sqrt)):
        out.append("alignas(64) const %s %s[%d] = {" % (ctype, name, len(vals)))
        for i in range(0, len(vals), 8):
            fmt = "%d" if ctype == "std::int32_t" else "0x%08xu"
            out.append("    " + ", ".join(fmt % v for v in vals[i:i + 8]) + ",")
        out.append("};")
        out.append("")
    text = "\n".join(out)
    try:
        if open(argv[1], newline="\n").read() == text:
            return 0                      # unchanged: keep the timestamp, no rebuild
    except OSError:
        pass
    with open(argv[1], "w", newline="\n") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
