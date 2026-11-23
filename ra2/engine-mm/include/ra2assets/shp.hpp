#pragma once
#include <cstdint>
#include <vector>
#include <stdexcept>
#include "ra2assets/pal.hpp"
namespace ra2assets {
class ShpFile {
public:
    static ShpFile load(std::vector<uint8_t> bytes);
    int count() const { return (int)frames_.size(); }
    int width() const { return cx_; }
    int height() const { return cy_; }
    struct Frame { int x,y,w,h; };
    Frame frame_info(int i) const {
        if (i < 0 || i >= (int)frames_.size()) throw std::runtime_error("shp: frame index out of range");
        auto&f=frames_[i]; return {f.x,f.y,f.w,f.h};
    }
    std::vector<uint8_t> decode_indexed(int i) const;
    static std::vector<Rgba> palettize(const std::vector<uint8_t>& idx, const Palette& pal);
private:
    struct FrameHdr { int x,y,w,h; uint32_t flags; uint32_t offset; };
    std::vector<uint8_t> bytes_;
    std::vector<FrameHdr> frames_;
    int cx_=0, cy_=0;
};
}
