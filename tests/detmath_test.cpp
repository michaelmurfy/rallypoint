// DetMath must be accurate, correct at the edges, and give the *same bits* on every platform. The golden hash below
// was produced on arm64 (clang); CI runs this test on x86-64 (gcc) as well, so a platform difference fails there.
#include "detmath.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static uint64_t g_hash = 1469598103934665603ull;
static void mix(double v)
{
	uint64_t b;
	memcpy(&b, &v, sizeof b);
	for (int i = 0; i < 8; ++i)
	{
		g_hash ^= (b >> (8 * i)) & 0xff;
		g_hash *= 1099511628211ull;
	}
}

static bool close(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

int main()
{
	// accuracy against the C library
	for (double x = -50.0; x <= 50.0; x += 0.0137)
	{
		assert(close(DetMath::sin(x), std::sin(x), 5e-15));
		assert(close(DetMath::cos(x), std::cos(x), 5e-15));
		mix(DetMath::sin(x));
		mix(DetMath::cos(x));
	}
	for (double y = -6.0; y <= 6.0; y += 0.21)
		for (double x = -6.0; x <= 6.0; x += 0.19)
		{
			if (x == 0.0 && y == 0.0)
				continue;
			assert(close(DetMath::atan2(y, x), std::atan2(y, x), 5e-15));
			mix(DetMath::atan2(y, x));
		}
	for (double x = -1.0; x <= 1.0; x += 0.0071)
	{
		assert(close(DetMath::asin(x), std::asin(x), 5e-15));
		assert(close(DetMath::acos(x), std::acos(x), 5e-15));
		mix(DetMath::asin(x));
		mix(DetMath::acos(x));
	}
	for (double x = -30.0; x <= 30.0; x += 0.113)
	{
		assert(std::fabs(DetMath::exp(x) / std::exp(x) - 1.0) < 1e-14);
		mix(DetMath::exp(x));
	}
	for (double x = 0.001; x < 1.0e6; x *= 1.19)
	{
		assert(close(DetMath::log(x), std::log(x), 1e-13));
		mix(DetMath::log(x));
	}
	for (double x = -1.2; x <= 1.2; x += 0.05)
		if (std::fabs(std::cos(x)) > 0.1)
			assert(std::fabs(DetMath::tan(x) / std::tan(x) - 1.0) < 1e-13);

	// the game's float flavours
	assert(close(DetMath::sinf(1.0f), (float)std::sin(1.0), 1e-7));
	assert(close(DetMath::atan2f(1.0f, 1.0f), 0.78539816339744830962, 1e-7));
	assert(DetMath::pow(7.0, 2.0) == 49.0 && DetMath::pow(2.0, 10.0) > 1023.999999 && DetMath::pow(2.0, 10.0) < 1024.000001);
	assert(DetMath::pow(-2.0, 3.0) < 0.0 && std::isnan(DetMath::pow(-2.0, 0.5)));

	// edges: signed zero, axes, infinities, NaN, out of range
	assert(DetMath::atan2(0.0, 1.0) == 0.0 && !std::signbit(DetMath::atan2(0.0, 1.0)));
	assert(std::signbit(DetMath::atan2(-0.0, 1.0)));
	assert(close(DetMath::atan2(0.0, -1.0), 3.14159265358979323846, 1e-15));
	assert(close(DetMath::atan2(-1.0, 0.0), -1.57079632679489661923, 1e-15));
	assert(close(DetMath::atan2(1.0, INFINITY), 0.0, 0.0) && close(DetMath::atan2(INFINITY, 1.0), 1.5707963267948966, 1e-15));
	assert(std::isnan(DetMath::sin(NAN)) && std::isnan(DetMath::cos(INFINITY)) && std::isnan(DetMath::asin(1.5)) && std::isnan(DetMath::acos(-1.0001)));
	assert(DetMath::asin(1.0) > 1.5707963 && DetMath::acos(1.0) == 0.0 && close(DetMath::acos(-1.0), 3.14159265358979323846, 1e-15));
	assert(DetMath::sin(0.0) == 0.0 && DetMath::cos(0.0) == 1.0 && DetMath::exp(0.0) == 1.0 && DetMath::log(1.0) == 0.0);
	assert(std::isinf(DetMath::log(0.0)) && std::isnan(DetMath::log(-1.0)) && std::isinf(DetMath::exp(800.0)) && DetMath::exp(-800.0) == 0.0);
	assert(close(DetMath::sin(1.0e7), std::sin(1.0e7), 1e-9)); // beyond the exact-reduction range: still sane

	// the float sin and cos are integer-only: the two angles where Windows and macOS once disagreed, accuracy to one ulp,
	// and a hash of the bits that CI checks on x86 as well
	{
		const uint32_t knownBits[2] = { 0x3F490FDBu, 0xC028AB2Bu };
		const uint32_t knownSin[2] = { 0x3F3504F3u, 0xBEF838F5u }, knownCos[2] = { 0x3F3504F3u, 0xBF5FE715u };
		for (int i = 0; i < 2; ++i)
		{
			float x, sv = 0, cv = 0;
			memcpy(&x, &knownBits[i], 4);
			sv = DetMath::sinf(x);
			cv = DetMath::cosf(x);
			uint32_t sb, cb;
			memcpy(&sb, &sv, 4);
			memcpy(&cb, &cv, 4);
			assert(sb == knownSin[i] && cb == knownCos[i]);
		}
		uint64_t fh = 1469598103934665603ull;
		uint32_t lcg = 12345u;
		for (int i = 0; i < 200000; ++i)
		{
			lcg = lcg * 1664525u + 1013904223u;
			const float x = ((int)(lcg >> 8) - 8388608) / 8388608.0f * (i % 3 == 0 ? 0.01f : i % 3 == 1 ? 12.0f : 900.0f);
			const float s = DetMath::sinf(x), c = DetMath::cosf(x);
			const long double ts = sinl((long double)x), tc = cosl((long double)x);
			int e;
			frexpf((float)ts, &e);
			assert(fabsl((long double)s - ts) <= 1.01L * ldexpl(1.0L, e - 24));
			frexpf((float)tc, &e);
			assert(fabsl((long double)c - tc) <= 1.01L * ldexpl(1.0L, e - 24));
			uint32_t sb, cb;
			memcpy(&sb, &s, 4);
			memcpy(&cb, &c, 4);
			for (uint32_t v : { sb, cb }) { fh ^= v; fh *= 1099511628211ull; }
		}
		printf("float sin/cos hash %016llx\n", (unsigned long long)fh);
		fflush(stdout);
		assert(fh == 0xca74d6f85213d93eull);
	}

	// the same bits everywhere
	printf("detmath hash %016llx\n", (unsigned long long)g_hash);
	fflush(stdout);
	assert(g_hash == 0xa1d5e79e976d9668ull);
	puts("detmath_test ok");
}
