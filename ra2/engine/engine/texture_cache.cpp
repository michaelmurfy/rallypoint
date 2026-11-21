#include "texture_cache.hpp"

#include <SDL.h>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace ra2engine {

static std::uint64_t g_next_cache_id = 1;

TextureCache::TextureCache(void* sdl_renderer) : renderer_(sdl_renderer), id_(g_next_cache_id++) {}

TextureCache::~TextureCache() {
    for (auto& kv : cache_) {
        SDL_DestroyTexture(static_cast<SDL_Texture*>(kv.second.tex));
    }
}

void* TextureCache::peek(const std::string& key) const {
    auto it = cache_.find(key);
    return it != cache_.end() ? touch(it->second) : nullptr;
}

void* TextureCache::insert(const std::string& key, void* tex, int w, int h) {
    Entry e;
    e.tex = tex;
    e.bytes = static_cast<std::size_t>(w > 0 ? w : 0) * static_cast<std::size_t>(h > 0 ? h : 0) * 4;
    e.used = frame_;
    bytes_ += e.bytes;
    cache_.emplace(key, e);
    return tex;
}

void TextureCache::pin(const std::string& key) {
    auto it = cache_.find(key);
    if (it != cache_.end()) it->second.pinned = true;
}

void TextureCache::clear() {
    for (auto& kv : cache_) SDL_DestroyTexture(static_cast<SDL_Texture*>(kv.second.tex));
    cache_.clear();
    bytes_ = 0;
    id_ = g_next_cache_id++;
}

std::size_t TextureCache::trim() {
    const std::size_t before = bytes_;
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (!it->second.pinned && it->second.used < frame_) {
            SDL_DestroyTexture(static_cast<SDL_Texture*>(it->second.tex));
            bytes_ -= it->second.bytes;
            it = cache_.erase(it);
            ++evicted_;
        } else {
            ++it;
        }
    }
    return before - bytes_;
}

void TextureCache::next_frame() {
    ++frame_;
    if (bytes_ <= budget_) return;
    // LRU over unpinned entries untouched for >= 2 frames, down to 3/4 budget.
    std::vector<std::pair<std::uint64_t, const std::string*>> cand;
    cand.reserve(cache_.size());
    for (const auto& kv : cache_)
        if (!kv.second.pinned && kv.second.used + 2 <= frame_) cand.push_back({kv.second.used, &kv.first});
    std::sort(cand.begin(), cand.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    const std::size_t target = budget_ / 4 * 3;
    std::vector<std::string> drop;
    std::size_t b = bytes_;
    for (const auto& c : cand) {
        if (b <= target) break;
        const Entry& e = cache_.at(*c.second);
        b -= e.bytes;
        drop.push_back(*c.second);
    }
    for (const std::string& k : drop) {
        auto it = cache_.find(k);
        SDL_DestroyTexture(static_cast<SDL_Texture*>(it->second.tex));
        bytes_ -= it->second.bytes;
        cache_.erase(it);
        ++evicted_;
    }
}

void* TextureCache::get(const std::string& key, const uint8_t* indices, int w,
                         int h, const ra2assets::Palette& pal) {
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return touch(it->second);
    }
    // W2-AN-03: an empty SHP frame (w or h == 0, e.g. the NAMISL silo's
    // SuperAnim frames) draws nothing. SDL_CreateTexture refuses 0-dim
    // textures ("Texture dimensions can't be 0") and the throw used to kill the
    // game on the first render of a silo. Callers pass the nullptr to SDL draw
    // calls, which reject it harmlessly. Render-only; no gate folds textures.
    if (w <= 0 || h <= 0) return nullptr;

    std::vector<ra2assets::Rgba> rgba(static_cast<size_t>(w) * h);
    for (int i = 0; i < w * h; ++i) {
        uint8_t idx = indices[i];
        if (idx == 0) {
            rgba[i] = ra2assets::Rgba{0, 0, 0, 0};
        } else {
            ra2assets::Rgba c = pal[idx];
            c.a = 255;
            rgba[i] = c;
        }
    }

    // ra2assets::Rgba is a tightly packed {r,g,b,a} byte struct, so its
    // in-memory byte order is always R,G,B,A regardless of host endianness.
    // SDL_PIXELFORMAT_RGBA32 is SDL's "byte array" alias that resolves to
    // whichever packed format matches that R,G,B,A memory order on the
    // current platform (SDL_PIXELFORMAT_ABGR8888 on little-endian, which is
    // what both Windows desktop and Android targets use). Using the *32
    // alias keeps this correct without hardcoding the little-endian-only
    // ABGR8888 constant.
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(renderer_);
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) {
        throw std::runtime_error(SDL_GetError());
    }

    SDL_UpdateTexture(tex, nullptr, rgba.data(), w * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    return insert(key, tex, w, h);
}

// G-84/G-90: like get(), but palette index `shadow_index` becomes a translucent
// black shadow pixel (RA2 unit SHPs bake their ground shadow into index 1;
// OpenRA's RA2 palettes declare ShadowIndex: 1). [RE-FALLBACK: shadow blend
// ratio not traced -- 50 %.]
void* TextureCache::get_shadowed(const std::string& key, const uint8_t* indices, int w, int h,
                                 const ra2assets::Palette& pal, int shadow_index, int alpha) {
    auto it = cache_.find(key);
    if (it != cache_.end()) return touch(it->second);
    std::vector<ra2assets::Rgba> rgba(static_cast<size_t>(w) * h);
    for (int i = 0; i < w * h; ++i) {
        const uint8_t idx = indices[i];
        if (idx == 0) rgba[i] = ra2assets::Rgba{0, 0, 0, 0};
        else if (idx == shadow_index) rgba[i] = ra2assets::Rgba{0, 0, 0, static_cast<uint8_t>(alpha)};
        else { ra2assets::Rgba c = pal[idx]; c.a = 255; rgba[i] = c; }
    }
    return get_rgba(key, reinterpret_cast<const uint8_t*>(rgba.data()), w, h);
}

void* TextureCache::get_rgba(const std::string& key, const uint8_t* rgba, int w, int h) {
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return touch(it->second);
    }
    if (w <= 0 || h <= 0) return nullptr;  // W2-AN-03 (see get())

    // Same RGBA32 byte-order rationale as get() above (SDL's *32 alias resolves
    // to the packed format matching R,G,B,A memory order on this platform).
    SDL_Renderer* renderer = static_cast<SDL_Renderer*>(renderer_);
    SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) {
        throw std::runtime_error(SDL_GetError());
    }

    SDL_UpdateTexture(tex, nullptr, rgba, w * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    return insert(key, tex, w, h);
}

}  // namespace ra2engine
