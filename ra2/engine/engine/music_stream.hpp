#pragma once
// W2-GA-22: the in-game score streamed instead of decoded whole on the game
// thread. The original streams its themes (ThemeClass +0x2c stream); here a
// worker thread fetches the WAV bytes (a file-offset MIX read), decodes the
// IMA-ADPCM one block at a time (ra2assets::WavImaStream), converts to the
// 22050 Hz stereo sink format and keeps a bounded ring ~2 s ahead of the
// audio thread, which pulls through Mixer::set_music. Nothing is kept once a
// track ends (no per-track PCM in the SoundBank). Pure C++ (no SDL).
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "audio_mixer.hpp"

namespace ra2engine {

class MusicStream : public MusicSource {
public:
    // `fetch` runs on the worker thread and returns the whole WAV file (the
    // compressed bytes; a few MB). An exception or an invalid WAV (e.g. the
    // zero-filled thememd.mix entries) marks the stream failed.
    explicit MusicStream(std::function<std::vector<uint8_t>()> fetch);
    ~MusicStream() override;
    MusicStream(const MusicStream&) = delete;
    MusicStream& operator=(const MusicStream&) = delete;

    std::size_t read(int16_t* lr, std::size_t frames) override;

    bool failed() const { return failed_.load(); }
    // Decoder done and every decoded frame handed to the mixer.
    bool finished() const;
    // Track length in seconds once the header is parsed (0 before / failed).
    double length_secs() const { return length_ms_.load() / 1000.0; }
    // Blocks until the header is parsed or the stream failed (max `ms`).
    bool wait_ready(int ms);

private:
    void run(std::function<std::vector<uint8_t>()> fetch);

    static constexpr std::size_t kRingFrames = 22050 * 2;   // ~2 s ahead
    mutable std::mutex mu_;
    std::condition_variable cv_;
    // Circular buffer of interleaved L,R samples at 22050 Hz.
    std::vector<int16_t> ring_ = std::vector<int16_t>(kRingFrames * 2 + 2);
    std::size_t head_ = 0, count_ = 0;   // in samples
    bool decoder_done_ = false;
    bool ready_ = false;
    std::atomic<bool> stop_{false};
    std::atomic<bool> failed_{false};
    std::atomic<long> length_ms_{0};
    std::thread worker_;
};

}  // namespace ra2engine
