#pragma once
// W5-E2 (R-173): Bink video decoding off the main thread. A worker owns the
// BinkVideoDecoder and decodes, in order, up to the frame the main thread
// asks for; the main thread only copies the newest finished RGBA picture into
// its streaming texture. `wait = true` (headless --play / shots) blocks until
// the requested frame is ready, so captures stay deterministic.
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "ra2assets/bink.hpp"

namespace ra2engine {

class BinkAsync {
public:
    // `dec` must outlive this object; nothing else may touch it meanwhile.
    BinkAsync(ra2assets::BinkVideoDecoder* dec, int frames) : dec_(dec), frames_(frames) {
        worker_ = std::thread([this] { run(); });
    }
    ~BinkAsync() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            quit_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    BinkAsync(const BinkAsync&) = delete;
    BinkAsync& operator=(const BinkAsync&) = delete;

    // Ask for frame `f` (looping files wrap back to 0 through the decoder's
    // own keyframe rewind).
    void want(int f) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            want_ = f;
        }
        cv_.notify_all();
        if (block_) {
            std::unique_lock<std::mutex> lk(mu_);
            done_cv_.wait(lk, [&] { return ready_ == want_ || failed_ || quit_; });
        }
    }
    void set_blocking(bool b) { block_ = b; }
    // The newest finished frame (-1 none yet); copies its RGBA when it is newer
    // than `have`.
    int take(int have, std::vector<std::uint8_t>& rgba) {
        std::lock_guard<std::mutex> lk(mu_);
        if (ready_ >= 0 && ready_ != have) rgba = pic_;
        return ready_;
    }
    bool failed() const {
        std::lock_guard<std::mutex> lk(mu_);
        return failed_;
    }

private:
    void run() {
        int cur = -1;
        for (;;) {
            int target;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [&] { return quit_ || (want_ >= 0 && want_ != cur && !failed_); });
                if (quit_) return;
                target = want_;
            }
            try {
                // Sequential: step forward one frame at a time (a wrap or a
                // backwards jump decodes the target directly -- the decoder
                // rewinds to the keyframe itself).
                const int next = (cur >= 0 && target > cur && target < frames_) ? cur + 1 : target;
                dec_->decode_frame(next);
                std::vector<std::uint8_t> px = dec_->rgba();
                cur = next;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    pic_.swap(px);
                    ready_ = cur;
                }
            } catch (const std::exception&) {
                std::lock_guard<std::mutex> lk(mu_);
                failed_ = true;
            }
            done_cv_.notify_all();
        }
    }

    ra2assets::BinkVideoDecoder* dec_;
    int frames_;
    mutable std::mutex mu_;
    std::condition_variable cv_, done_cv_;
    std::thread worker_;
    std::vector<std::uint8_t> pic_;
    int want_ = -1, ready_ = -1;
    bool quit_ = false, failed_ = false, block_ = false;
};

}  // namespace ra2engine
