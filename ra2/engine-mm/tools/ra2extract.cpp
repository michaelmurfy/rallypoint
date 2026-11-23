#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "ra2assets/fileio.hpp"
#include "ra2assets/mix.hpp"
#include "ra2assets/pal.hpp"
#include "ra2assets/shp.hpp"
#include "ra2assets/pcx.hpp"
#include <algorithm>
#include <map>
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace ra2assets;

// Descend a nested-mix chain: "cache.mix" or "a.mix/b.mix".
static MixFile descend(MixFile m, std::string path){
    while (!path.empty()){
        size_t s = path.find('/');
        std::string head = (s==std::string::npos) ? path : path.substr(0,s);
        path = (s==std::string::npos) ? "" : path.substr(s+1);
        m = m.open_nested(head);
    }
    return m;
}

// Resolve "name.shp" or "inner.mix/name.shp" (any depth) to entry bytes.
static std::vector<uint8_t> read_path(const MixFile& m, const std::string& path){
    size_t s = path.find('/');
    if (s == std::string::npos){
        const MixEntry* e = m.find(path);
        if (!e) throw std::runtime_error("not found: " + path);
        return m.read(*e);
    }
    MixFile inner = m.open_nested(path.substr(0,s));
    return read_path(inner, path.substr(s+1));
}

static int rd16(const uint8_t* p){ return p[0] | (p[1] << 8); }

// --- magic scanning (menu video hunt) ---------------------------------------
static size_t find_bytes(const std::vector<uint8_t>& hay, const char* needle, size_t from=0){
    size_t nl=std::string(needle).size();
    if (hay.size()<nl) return std::string::npos;
    auto it=std::search(hay.begin()+from, hay.end(), (const uint8_t*)needle, (const uint8_t*)needle+nl);
    return it==hay.end() ? std::string::npos : (size_t)(it-hay.begin());
}

static void scan_mix(const MixFile& t, const std::string& prefix, int depth,
                     int& nent, int& nvqa, int& nshp, int& nmix){
    for (auto& e : t.entries()){
        std::vector<uint8_t> b;
        try { b=t.read(e); } catch(...){ continue; }
        if (b.empty()) continue;
        nent++;
        char loc[512];
        std::snprintf(loc,sizeof loc,"%s%08X", prefix.c_str(), e.id);
        // VQA signatures anywhere in the entry
        if (b.size()>=12 && !memcmp(b.data(),"FORM",4) && !memcmp(b.data()+8,"WVQA",4)){
            std::printf("[VQA-FILE] %s size=%zu\n", loc, b.size()); nvqa++;
        } else {
            for (const char* tag : {"WVQA","VQFL","VQFR"}){
                size_t off=find_bytes(b, tag);
                if (off!=std::string::npos){
                    std::printf("[VQA-CHUNK] %s size=%zu tag=%s off=%zu\n", loc, b.size(), tag, off);
                    nvqa++; break;
                }
            }
        }
        // multi-frame SHP candidates (menu animation hunt)
        try {
            ShpFile s=ShpFile::load(b);
            if (s.count()>4 && s.width()>0 && s.height()>0 &&
                s.width()<2048 && s.height()<2048 && (long)s.width()*s.height()>=2000){
                std::printf("[SHP] %s size=%zu dims=%dx%d frames=%d\n",
                            loc, b.size(), s.width(), s.height(), s.count()); nshp++;
            }
        } catch(...){}
        // nested mix? recurse (content-detected; validated by successful reads)
        if (depth<3 && b.size()>=16){
            try {
                MixFile inner=MixFile::open(b); // copy; b reused below only for size
                if (inner.count()>0 && inner.count()<20000){
                    // validate the first entry is readable before trusting it
                    inner.read(inner.entries()[0]);
                    std::printf("[MIX] %s size=%zu entries=%zu -> descending\n", loc, b.size(), inner.count());
                    nmix++;
                    scan_mix(inner, std::string(loc)+"/", depth+1, nent, nvqa, nshp, nmix);
                }
            } catch(...){}
        }
    }
}

int main(int argc, char** argv){
    if (argc>=3 && std::string(argv[2])=="--scanraw"){
        // scan a plain file's raw bytes for VQA signatures (no mix parsing)
        auto b=read_file(argv[1]);
        if (b.empty()){ std::cerr<<"cannot read "<<argv[1]<<"\n"; return 1; }
        std::printf("scanraw %s size=%zu\n", argv[1], b.size());
        // report ALL occurrences of each tag (up to 40)
        for (const char* tag : {"FORM","WVQA","VQFL","VQFR","VQHD"}){
            size_t from=0; int hits=0;
            while (hits<40){
                size_t off=find_bytes(b, tag, from);
                if (off==std::string::npos) break;
                std::printf("  tag=%s off=%zu", tag, off);
                if (!strcmp(tag,"FORM") && off+12<=b.size())
                    std::printf(" next8=%.8s", (const char*)b.data()+off+8);
                std::printf("\n");
                hits++; from=off+1;
            }
            if (!hits) std::printf("  tag=%s (none)\n", tag);
        }
        return 0;
    }
    if (argc<3){
        std::cerr<<"usage: ra2extract <mix> --list [nested-path]\n"
                 <<"       ra2extract <mix> --raw <asset-path> <out.bin>\n"
                 <<"       ra2extract <mix> <shp-path> <pal-path> <out.png> [frame]\n"
                 <<"       asset paths may descend nested mixes: conquer.mix/e1.shp\n"
                 <<"       [frame] defaults to 0; must be < the SHP's frame count.\n";
        return 2;
    }
    try {
        auto bytes=read_file(argv[1]);
        if (bytes.empty()){ std::cerr<<"cannot read "<<argv[1]<<"\n"; return 1; }
        MixFile m = MixFile::open(std::move(bytes));
        if (std::string(argv[2])=="--list"){
            MixFile t = (argc>=4) ? descend(std::move(m), argv[3]) : std::move(m);
            std::cout<<"entries: "<<t.count()<<"\n";
            for (auto& e : t.entries())
                std::printf("  %08X  offset=%-10u size=%u\n", e.id, e.offset, e.size);
            for (auto& n : t.local_names()) std::cout<<"  "<<n<<"\n";
            return 0;
        }
        if (std::string(argv[2])=="--scanmagic"){
            // usage: <mix> --scanmagic [nested-path]
            MixFile t = (argc>=4) ? descend(std::move(m), argv[3]) : std::move(m);
            int nent=0,nvqa=0,nshp=0,nmix=0;
            std::string prefix = std::string(argv[1]) + (argc>=4 ? std::string("/")+argv[3] : std::string()) + "/";
            scan_mix(t, prefix, 0, nent, nvqa, nshp, nmix);
            std::printf("scanmagic done: entries=%d vqa-hits=%d shp-candidates=%d nested-mixes=%d\n",
                        nent, nvqa, nshp, nmix);
            return 0;
        }
        if (std::string(argv[2])=="--resolve"){
            // Name every entry of a nested mix using a known-filenames list.
            // usage: <mix> --resolve <nested> <namesfile>
            if (argc<5){ std::cerr<<"need --resolve <nested> <namesfile>\n"; return 2; }
            MixFile t = descend(std::move(m), argv[3]);
            std::ifstream nf(argv[4]);
            std::string line; std::map<uint32_t,std::string> id2name;
            while (std::getline(nf, line)){
                // take first whitespace/tab-delimited token as the filename
                size_t e = line.find_first_of(" \t\r\n");
                std::string nm = (e==std::string::npos)? line : line.substr(0,e);
                if (nm.empty()) continue;
                id2name[mix_id(nm)] = nm;
            }
            int named=0, unnamed=0;
            for (auto& en : t.entries()){
                auto it = id2name.find(en.id);
                if (it != id2name.end()){ std::printf("  %08X  %-24s size=%u\n", en.id, it->second.c_str(), en.size); named++; }
                else { std::printf("  %08X  <?>                      size=%u\n", en.id, en.size); unnamed++; }
            }
            std::printf("resolved %d / %d (unnamed %d)\n", named, named+unnamed, unnamed);
            return 0;
        }
        if (std::string(argv[2])=="--frames"){
            // Render ALL frames of ONE entry (located by hex hash id) of a nested
            // mix to PNGs. usage: <mix> --frames <nested-mix> <hexid> <pal> <outdir>
            if (argc<7){ std::cerr<<"need --frames <nested> <hexid> <pal> <outdir>\n"; return 2; }
            MixFile t = descend(std::move(m), argv[3]);
            uint32_t want = (uint32_t)std::strtoul(argv[4], nullptr, 16);
            Palette pal;
            { auto pb=read_file(argv[1]); MixFile pm=MixFile::open(std::move(pb));
              auto pr=read_path(pm, argv[5]); pal=Palette::load(pr.data(),pr.size()); }
            std::string outdir=argv[6];
            for (auto& e : t.entries()){
                if (e.id!=want) continue;
                ShpFile s=ShpFile::load(t.read(e));
                for (int f=0; f<s.count(); ++f){
                    auto fi=s.frame_info(f);
                    auto idx=s.decode_indexed(f);
                    auto rgba=ShpFile::palettize(idx,pal);
                    char nm[1024];
                    std::snprintf(nm,sizeof nm,"%s/f%08X_%02d.png",outdir.c_str(),e.id,f);
                    stbi_write_png(nm,fi.w,fi.h,4,rgba.data(),fi.w*4);
                }
                std::printf("frames: %08X %d frames %dx%d\n", e.id, s.count(), s.width(), s.height());
                return 0;
            }
            std::cerr<<"hash not found\n"; return 1;
        }
        if (std::string(argv[2])=="--dumpall"){
            // Bulk-dump every entry of a nested mix as PNG, auto-detecting PCX
            // (byte0==0x0A) vs SHP (try frame 0). Names outputs by hash id so an
            // asset can be located visually then reverse-matched. Palette used for
            // SHP frames only (PCX is self-palettized).
            // usage: <mix> --dumpall <nested-mix-path> <outdir> <pal-path>
            if (argc<6){ std::cerr<<"need --dumpall <nested> <outdir> <pal>\n"; return 2; }
            MixFile t = descend(std::move(m), argv[3]);
            std::string outdir = argv[4];
            std::vector<uint8_t> praw;
            try { praw = read_file(argv[5]); } catch(...){}
            // pal path may be nested in the ORIGINAL mix; reload for pal
            Palette pal;
            bool havepal=false;
            try {
                auto pbytes=read_file(argv[1]);
                MixFile pm=MixFile::open(std::move(pbytes));
                auto pr=read_path(pm, argv[5]);
                pal=Palette::load(pr.data(),pr.size()); havepal=true;
            } catch(...){}
            int npcx=0,nshp=0,nfail=0;
            for (auto& e : t.entries()){
                std::vector<uint8_t> b;
                try { b=t.read(e); } catch(...){ continue; }
                if (b.empty()) continue;
                char nm[1024];
                if (b[0]==0x0A){ // PCX
                    try {
                        PcxImage img=decode_pcx(b);
                        auto rgba=img.rgba();
                        std::snprintf(nm,sizeof nm,"%s/pcx_%08X_%dx%d.png",outdir.c_str(),e.id,img.w,img.h);
                        stbi_write_png(nm,img.w,img.h,4,rgba.data(),img.w*4);
                        npcx++; continue;
                    } catch(...){}
                }
                if (havepal){ // try SHP frame 0
                    try {
                        ShpFile s=ShpFile::load(b);
                        if (s.count()>0){
                            auto fi=s.frame_info(0);
                            if (fi.w>0&&fi.h>0&&fi.w<4096&&fi.h<4096){
                                auto idx=s.decode_indexed(0);
                                auto rgba=ShpFile::palettize(idx,pal);
                                std::snprintf(nm,sizeof nm,"%s/shp_%08X_%dx%d_f%d.png",outdir.c_str(),e.id,fi.w,fi.h,s.count());
                                stbi_write_png(nm,fi.w,fi.h,4,rgba.data(),fi.w*4);
                                nshp++; continue;
                            }
                        }
                    } catch(...){}
                }
                nfail++;
            }
            std::cout<<"dumpall: pcx="<<npcx<<" shp="<<nshp<<" other="<<nfail<<"\n";
            return 0;
        }
        if (std::string(argv[2])=="--raw"){
            // Dump the raw extracted bytes of any nested asset to disk, so an
            // external oracle (e.g. XCC's own decoder) can be fed byte-identical
            // input. Extraction is ra2assets'; decoding is the oracle's.
            if (argc<5){ std::cerr<<"need --raw <asset-path> <out.bin>\n"; return 2; }
            auto raw=read_path(m, argv[3]);
            std::ofstream out(argv[4], std::ios::binary);
            if (!out){ std::cerr<<"cannot write "<<argv[4]<<"\n"; return 1; }
            out.write(reinterpret_cast<const char*>(raw.data()), (std::streamsize)raw.size());
            std::cout<<"wrote "<<argv[4]<<" ("<<raw.size()<<" bytes)\n";
            return 0;
        }
        // PCX -> PNG: <mix> <pcx-path> <out.png>  (self-palettized 8-bit RLE PCX)
        {
            std::string p2(argv[2]);
            if (p2.size()>=4 && p2.compare(p2.size()-4,4,".pcx")==0){
                if (argc<4){ std::cerr<<"need <pcx-path> <out.png>\n"; return 2; }
                auto pb = read_path(m, argv[2]);
                int nplanes = pb.size()>66 ? pb[65] : 1;
                int bpp     = pb.size()>3  ? pb[3]  : 8;
                if (nplanes==3 && bpp==8){
                    // 24-bit RLE PCX (R,G,B planes per scanline) — decode locally.
                    int xmin=rd16(&pb[4]), ymin=rd16(&pb[6]), xmax=rd16(&pb[8]), ymax=rd16(&pb[10]);
                    int w=xmax-xmin+1, h=ymax-ymin+1, bpl=rd16(&pb[66]);
                    std::vector<uint8_t> rgba((size_t)w*h*4, 255);
                    size_t sp=128; const int stride=bpl*3;
                    std::vector<uint8_t> row(stride);
                    for (int y=0; y<h && sp<pb.size(); ++y){
                        int x=0;
                        while (x<stride && sp<pb.size()){
                            uint8_t b=pb[sp++]; int run=1; uint8_t v;
                            if ((b&0xC0)==0xC0){ run=b&0x3F; v=(sp<pb.size())?pb[sp++]:0; }
                            else v=b;
                            while (run-- && x<stride) row[x++]=v;
                        }
                        for (int px=0; px<w; ++px){
                            size_t o=((size_t)y*w+px)*4;
                            rgba[o+0]=row[px];            // R plane
                            rgba[o+1]=row[bpl+px];        // G plane
                            rgba[o+2]=row[2*bpl+px];      // B plane
                        }
                    }
                    stbi_write_png(argv[3], w, h, 4, rgba.data(), w*4);
                    std::cout<<"wrote "<<argv[3]<<" ("<<w<<"x"<<h<<", pcx24)\n";
                    return 0;
                }
                PcxImage img = decode_pcx(pb);
                auto rgba = img.rgba();
                stbi_write_png(argv[3], img.w, img.h, 4, rgba.data(), img.w*4);
                std::cout<<"wrote "<<argv[3]<<" ("<<img.w<<"x"<<img.h<<", pcx)\n";
                return 0;
            }
        }
        if (argc<5){ std::cerr<<"need <shp-path> <pal-path> <out.png>\n"; return 2; }
        ShpFile s=ShpFile::load(read_path(m, argv[2]));
        if (s.count() == 0) throw std::runtime_error("shp has no frames");
        // Optional trailing frame index (derivation aid for the Combat Phase's
        // fire/die sequence dumps -- Task 7): defaults to 0 (prior behavior
        // unchanged for every existing caller/test).
        int frame = 0;
        if (argc >= 6) frame = std::atoi(argv[5]);
        if (frame < 0 || frame >= s.count()) throw std::runtime_error("frame out of range");
        auto praw=read_path(m, argv[3]);
        Palette pal=Palette::load(praw.data(),praw.size());
        auto idx=s.decode_indexed(frame);
        auto rgba=ShpFile::palettize(idx,pal);
        auto fi=s.frame_info(frame);
        stbi_write_png(argv[4], fi.w, fi.h, 4, rgba.data(), fi.w*4);
        std::cout<<"wrote "<<argv[4]<<" ("<<fi.w<<"x"<<fi.h<<", frame "<<frame<<"/"<<s.count()<<")\n";
        return 0;
    } catch (const std::exception& ex){
        std::cerr<<"error: "<<ex.what()<<"\n";
        return 1;
    }
}
