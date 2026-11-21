#include "audio_sdl.hpp"

#include <SDL.h>

#include <cstring>

#include "audio_mixer.hpp"

namespace ra2engine {

// W2-AR-01: ONE SDL playback device for the life of the process. The original
// opens a single DirectSound device; SDL's Android AAudio backend supports
// exactly one playback device (a second SDL_OpenAudioDevice trips
// SDL_aaudio.c:81 and overwrites the backend's single `audioDevice` global).
// The menu's AudioSystem and every GameScene's AudioSystem each own an
// AudioOutput, but those are now only HANDLES on this shared device: the first
// open() opens it, the last close() closes it, and pump() hands the callback
// whichever Mixer is current (the scene on screen). All state below is touched
// on the main thread only, except `mixer`, which the callback reads.
namespace {
struct SharedDevice {
    SDL_AudioDeviceID dev = 0;
    int refs = 0;
    std::atomic<Mixer*> mixer{nullptr};
};
SharedDevice g_audio;
// R-163: 0..256 fixed-point focus gain (256 = unity), set from the Android
// AudioManager listener thread (RA2Activity), read by the callback.
std::atomic<int> g_focus_gain{256};
}  // namespace

void AudioOutput::callback(void*, uint8_t* stream, int len) {
    Mixer* m = g_audio.mixer.load();
    const std::size_t frames = static_cast<std::size_t>(len) / 4;   // S16 stereo
    if (!m) {
        std::memset(stream, 0, static_cast<std::size_t>(len));
        return;
    }
    int16_t* out = reinterpret_cast<int16_t*>(stream);
    m->render_stereo(out, frames);
    const int g = g_focus_gain.load(std::memory_order_relaxed);
    if (g < 256) {
        for (std::size_t i = 0; i < frames * 2; ++i) out[i] = static_cast<int16_t>((out[i] * g) >> 8);
    }
}

void AudioOutput::set_focus_gain(float gain) {
    const int g = static_cast<int>(gain * 256.0f + 0.5f);
    g_focus_gain.store(g < 0 ? 0 : g > 256 ? 256 : g);
}
float AudioOutput::focus_gain() { return g_focus_gain.load() / 256.0f; }

bool AudioOutput::open() {
    if (dev_ != 0) return true;
    if (g_audio.refs == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
        SDL_AudioSpec want;
        SDL_zero(want);
        want.freq = 22050;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = static_cast<Uint16>(kChunkFrames);
        want.callback = &AudioOutput::callback;
        want.userdata = nullptr;

        SDL_AudioSpec have;
        SDL_zero(have);
        // allowed_changes=0: SDL converts to the hardware format itself, so the
        // mixer's fixed 22050 Hz stereo S16 render never resamples at runtime.
        g_audio.dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, /*allowed_changes=*/0);
        if (g_audio.dev == 0) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }
        SDL_PauseAudioDevice(g_audio.dev, 0);  // run the callback (silence until attached)
    }
    ++g_audio.refs;
    dev_ = g_audio.dev;
    return true;
}

void AudioOutput::pump(Mixer& mixer) {
    if (dev_ == 0) return;
    mixer_.store(&mixer);
    g_audio.mixer.store(&mixer);   // this handle's scene is the one on screen
}

void AudioOutput::detach() {
    if (dev_ == 0) return;
    // Under the device lock: once this returns the callback is not inside (and
    // will never again enter) this handle's mixer, so it may be destroyed.
    SDL_LockAudioDevice(dev_);
    Mixer* mine = mixer_.load();
    if (mine && g_audio.mixer.load() == mine) g_audio.mixer.store(nullptr);
    mixer_.store(nullptr);
    SDL_UnlockAudioDevice(dev_);
}

void AudioOutput::close() {
    if (dev_ == 0) return;
    detach();
    dev_ = 0;
    if (--g_audio.refs == 0) {
        SDL_CloseAudioDevice(g_audio.dev);   // stops + joins the callback thread
        g_audio.dev = 0;
        g_audio.mixer.store(nullptr);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void AudioOutput::set_paused(bool paused) {
    if (g_audio.dev != 0) SDL_PauseAudioDevice(g_audio.dev, paused ? 1 : 0);
}

int AudioOutput::open_device_count() { return g_audio.dev != 0 ? 1 : 0; }
int AudioOutput::handle_count() { return g_audio.refs; }

}  // namespace ra2engine

#ifdef __ANDROID__
#include <jni.h>
// R-163: RA2Activity's OnAudioFocusChangeListener -> the sink's gain.
extern "C" JNIEXPORT void JNICALL Java_org_ra2_engine_RA2Activity_nativeAudioFocusGain(JNIEnv*, jclass, jfloat gain) {
    ra2engine::AudioOutput::set_focus_gain(gain);
    SDL_Log("[lifecycle] audio focus gain %.2f", static_cast<double>(gain));
}
#endif
