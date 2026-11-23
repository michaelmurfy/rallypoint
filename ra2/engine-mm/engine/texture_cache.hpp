#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "ra2assets/pal.hpp"

namespace ra2engine {

// Palettizes indexed (8-bit) image buffers into RGBA and caches the result
// as an SDL_Texture, keyed by an asset+frame identifier string. Kept
// SDL-free in the header (renderer/texture handles are passed/returned as
// void*) so this header doesn't force SDL.h on every includer.
class TextureCache {
public:
    explicit TextureCache(void* sdl_renderer);
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // Palettize indexed w*h -> RGBA (index 0 transparent) -> cached
    // SDL_Texture. key identifies the asset+frame. Returns an SDL_Texture*
    // (as void*); on cache hit, returns the previously created texture.
    void* get(const std::string& key, const uint8_t* indices, int w, int h,
              const ra2assets::Palette& pal);

    // Upload an already-palettized RGBA buffer (`rgba` = w*h*4 bytes, R,G,B,A
    // memory order, alpha 0 = transparent) directly as a cached SDL_Texture,
    // keyed by `key`. Used by the voxel draw path, whose rasterizer already
    // produces final RGBA (VoxelSprite::rgba) rather than palette indices. On
    // cache hit returns the previously created texture. Trivial variant of the
    // indexed `get` above (skips the palettize loop).
    void* get_rgba(const std::string& key, const uint8_t* rgba, int w, int h);

    // Return the cached SDL_Texture* for `key`, or nullptr if not cached.
    // Lets callers skip decoding/palettizing work on a cache hit.
    // G-84/G-90: index `shadow_index` -> black at `alpha` (baked SHP shadows).
    void* get_shadowed(const std::string& key, const uint8_t* indices, int w, int h,
                       const ra2assets::Palette& pal, int shadow_index, int alpha);

    void* peek(const std::string& key) const;

    // --- W2-GA-P5: bounded cache -------------------------------------------
    // Every lookup stamps the entry with the current frame. next_frame() (the
    // game calls it once per rendered frame) advances the clock and, when the
    // estimated texture bytes exceed the budget, destroys the least-recently
    // used UNPINNED entries that were not touched in the last two frames, so
    // a pointer fetched this frame is never freed under its caller. Pinned
    // entries (pin(), or the get_pinned_* uploads) are never evicted: use them
    // for textures whose pointer is cached across frames (terrain draw list).
    void next_frame();
    void set_budget_bytes(std::size_t b) { budget_ = b; }
    std::size_t bytes() const { return bytes_; }
    std::size_t size() const { return cache_.size(); }
    std::size_t evicted() const { return evicted_; }
    void pin(const std::string& key);
    // W2-AR-26: drop EVERY texture (SDL_RENDER_DEVICE_RESET: the GL context
    // was lost, every handle is invalid) and take a new instance_id(), so the
    // pointer caches keyed on it (terrain draw list) rebuild. Entries come
    // back lazily on the next lookup.
    void clear();
    // W2-AR-09/-26: SDL_APP_LOWMEMORY -- free every unpinned entry not used
    // this frame (the budget LRU, run to zero). Returns the bytes freed.
    std::size_t trim();
    // Unique per TextureCache instance (never reused, unlike the address), so
    // a cache of texture pointers can tell it is talking to the same cache.
    std::uint64_t instance_id() const { return id_; }
    void* renderer() const { return renderer_; }

private:
    struct Entry {
        void* tex = nullptr;              // SDL_Texture*
        std::size_t bytes = 0;
        mutable std::uint64_t used = 0;   // frame of last lookup
        bool pinned = false;
    };
    void* insert(const std::string& key, void* tex, int w, int h);
    void* touch(const Entry& e) const { e.used = frame_; return e.tex; }

    void* renderer_;  // SDL_Renderer*, non-owning
    std::unordered_map<std::string, Entry> cache_;  // key -> SDL_Texture*
    std::uint64_t id_ = 0;
    std::uint64_t frame_ = 1;
    std::size_t bytes_ = 0;
    std::size_t budget_ = std::size_t(384) << 20;   // ~384 MB of RGBA
    std::size_t evicted_ = 0;
};

}  // namespace ra2engine
