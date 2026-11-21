#include "ra2assets/shp.hpp"
#include <stdexcept>
namespace ra2assets {
static uint16_t r16(const uint8_t*p){return p[0]|(p[1]<<8);}
static uint32_t r32(const uint8_t*p){return p[0]|(p[1]<<8)|(p[2]<<16)|(uint32_t(p[3])<<24);}

ShpFile ShpFile::load(std::vector<uint8_t> bytes) {
    ShpFile s; s.bytes_=std::move(bytes);
    const uint8_t* p=s.bytes_.data();
    if (s.bytes_.size()<8) throw std::runtime_error("shp: too small");
    /*zero*/ s.cx_=r16(p+2); s.cy_=r16(p+4);
    int frames=r16(p+6);
    size_t off=8;
    for (int i=0;i<frames;i++){
        if (off + 24 > s.bytes_.size()) throw std::runtime_error("shp: truncated frame headers");
        const uint8_t* f=p+off;
        FrameHdr h;
        h.x=(int16_t)r16(f+0); h.y=(int16_t)r16(f+2); h.w=r16(f+4); h.h=r16(f+6);
        h.flags=r32(f+8); h.offset=r32(f+20);
        s.frames_.push_back(h);
        off+=24;
    }
    return s;
}

std::vector<uint8_t> ShpFile::decode_indexed(int i) const {
    if (i < 0 || i >= (int)frames_.size()) throw std::runtime_error("shp: frame index out of range");
    const FrameHdr& h=frames_[i];
    std::vector<uint8_t> out((size_t)h.w*h.h, 0);
    if (h.offset==0) return out;                 // empty frame
    if (h.offset > bytes_.size()) return out;    // corrupt offset: degrade to empty frame
    const uint8_t* src=bytes_.data()+h.offset;
    const uint8_t* end=bytes_.data()+bytes_.size();
    if (h.flags & 2u) {                          // RLE-zero (XCC format 3)
        for (int y=0;y<h.h;y++){
            // Every scanline begins with a uint16 total line length in bytes
            // (including these 2 prefix bytes). Advance strictly by it —
            // never scan past a line's end into the next line's prefix.
            if (src+2>end) break;
            uint16_t line_len=r16(src);
            if (line_len<2 || src+line_len>end) break;   // malformed: stop cleanly
            const uint8_t* q=src+2;
            const uint8_t* qe=src+line_len;
            src+=line_len;
            int x=0;
            while (x<h.w && q<qe){
                uint8_t v=*q++;
                if (v==0){ uint8_t run = (q<qe)?*q++:0; for(uint8_t k=0;k<run && x<h.w;k++) out[y*h.w + x++]=0; }
                else out[y*h.w + x++]=v;
            }
        }
    } else {                                     // uncompressed
        size_t n=(size_t)h.w*h.h;
        for (size_t k=0;k<n && src<end;k++) out[k]=*src++;
    }
    return out;
}

std::vector<Rgba> ShpFile::palettize(const std::vector<uint8_t>& idx, const Palette& pal) {
    std::vector<Rgba> out(idx.size());
    for (size_t k=0;k<idx.size();k++){
        uint8_t ix=idx[k];
        if (ix==0){ out[k]={0,0,0,0}; }
        else out[k]=pal[ix];
    }
    return out;
}
}
