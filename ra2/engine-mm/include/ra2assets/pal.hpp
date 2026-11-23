#pragma once
#include <cstdint>
#include <cstddef>

namespace ra2assets {
struct Rgba { uint8_t r,g,b,a; };
class Palette {
public:
    static Palette load(const uint8_t* data, size_t len);
    const Rgba& operator[](int i) const { return c_[i]; }
    // Non-const overload: lets a caller build a derived palette (e.g. the
    // Combat Phase's house-color remap, engine/world.cpp's pal_for_house)
    // from a copy of an existing one without a bespoke builder API. Added
    // alongside the const overload (classic const/non-const pair) rather
    // than a separate setter/constructor.
    Rgba& operator[](int i) { return c_[i]; }
private:
    Rgba c_[256];
};
}
