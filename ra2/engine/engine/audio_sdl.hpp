#pragma once
#include <atomic>
#include <cstdint>
namespace ra2engine {
class Mixer;
// SDL audio sink. W2-GA-23/P4: opens the default device at 22050 Hz STEREO S16
// and mixes on SDL's audio thread (callback) through Mixer::render_stereo, so
// a long game frame (asset load, a slow update) can no longer underrun the
// output the way the old 200 ms queue pumped from update() did. The mixer is
// internally locked; the device buffer is kChunkFrames (~93 ms) deep.
//
// W2-AR-01: every AudioOutput is a HANDLE on ONE process-wide SDL device
// (refcounted). The menu and the game scene each hold a handle; the device is
// opened once and the callback plays the mixer of whichever handle pumped
// last (the scene on screen). Android's AAudio backend allows exactly one
// playback device, and the original has one DirectSound device too.
class AudioOutput {
public:
    AudioOutput() = default;
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;
    ~AudioOutput() { close(); }
    bool open();                 // false: no device (game continues silent)
    // Attaches `mixer` to the running device (idempotent; cheap per frame).
    // Kept under its old name so every existing per-frame call site works.
    void pump(Mixer& mixer);
    void detach();               // stop pulling from this handle's mixer (before it dies)
    void close();                // release the handle (the last one closes the device)
    bool ok() const { return dev_ != 0; }
    // W2-AR-09/-19: pause the shared device while the app is in the background.
    static void set_paused(bool paused);
    // R-163: Android audio focus. 1 = full volume, a duck gain (0.2) while
    // another app may duck us (navigation prompt), 0 = silent (a call, or
    // another media app took focus). Applied in the callback after mixing.
    static void set_focus_gain(float gain);
    static float focus_gain();
    // Diagnostics: SDL devices open (0 or 1) and live handles.
    static int open_device_count();
    static int handle_count();
private:
    static void callback(void* self, uint8_t* stream, int len);
    static constexpr int kChunkFrames = 2048;
    uint32_t dev_ = 0;
    std::atomic<Mixer*> mixer_{nullptr};
};
}  // namespace ra2engine
