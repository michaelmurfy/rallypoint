#include "ra2assets/bink_audio.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

namespace ra2assets {

// LSB-first bit reader over one packet (Bink is little-endian bit order).
class BinkAudioBits {
public:
    BinkAudioBits(const std::uint8_t* d, std::size_t n) : d_(d), bits_(n * 8) {}
    std::uint32_t get(int n) {
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i, ++pos_) {
            const std::size_t byte = pos_ >> 3;
            const std::uint32_t bit = byte < bits_ / 8 ? (d_[byte] >> (pos_ & 7)) & 1u : 0u;
            v |= bit << i;
        }
        return v;
    }
    bool get1() { return get(1) != 0; }
    void skip(int n) { pos_ += static_cast<std::size_t>(n); }
    void align32() { pos_ = (pos_ + 31) & ~static_cast<std::size_t>(31); }
    long left() const { return static_cast<long>(bits_) - static_cast<long>(pos_); }

private:
    const std::uint8_t* d_;
    std::size_t bits_, pos_ = 0;
};

namespace {
const int kCriticalFreqs[25] = {100,  200,  300,  400,  510,  630,  770,  920,  1080, 1270, 1480,  1720, 2000,
                                2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500, 24500};
const int kRleLength[16] = {2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64};

float get_float(BinkAudioBits& gb) {
    const int power = static_cast<int>(gb.get(5));
    float f = std::ldexp(static_cast<float>(gb.get(23)), power - 23);
    if (gb.get1()) f = -f;
    return f;
}

void fft(std::vector<std::complex<double>>& a, bool inverse) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * 3.14159265358979323846 / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Inverse real DFT of N real samples from the Bink coefficient layout
// c[0] = DC, c[1] = Nyquist, then (re, im) of bins 1..N/2-1, scaled by 0.5:
// x[n] = 0.5 * (X0 + XN/2 (-1)^n + 2 sum Re(conj(X_k) e^{+i 2 pi k n / N}))
// (the stored imaginary part is negated -- checked against ffmpeg).
void inverse_rdft(const float* c, int n, float* out) {
    std::vector<std::complex<double>> a(static_cast<std::size_t>(n));
    a[0] = c[0];
    a[static_cast<std::size_t>(n / 2)] = c[1];
    for (int k = 1; k < n / 2; ++k) {
        const std::complex<double> x(c[2 * k], -c[2 * k + 1]);
        a[static_cast<std::size_t>(k)] = x;
        a[static_cast<std::size_t>(n - k)] = std::conj(x);
    }
    fft(a, true);
    for (int i = 0; i < n; ++i) out[i] = static_cast<float>(0.5 * a[static_cast<std::size_t>(i)].real());
}

// DCT-III (the inverse of the DCT-II), unnormalised:
// x[n] = c0/2 + sum_{k>=1} c_k cos(pi k (2n+1) / 2N), through a cos table
// indexed by k(2n+1) mod 4N.
void inverse_dct(const float* c, int n, float* out) {
    static std::vector<double> table;
    static int table_n = 0;
    if (table_n != n) {
        table.resize(static_cast<std::size_t>(4 * n));
        for (int m = 0; m < 4 * n; ++m) table[static_cast<std::size_t>(m)] = std::cos(3.14159265358979323846 * m / (2.0 * n));
        table_n = n;
    }
    const int mod = 4 * n;
    for (int i = 0; i < n; ++i) {
        double s = c[0] * 0.5;
        const int step = 2 * i + 1;
        int m = step;
        for (int k = 1; k < n; ++k, m += step) {
            if (m >= mod) m -= mod * (m / mod);
            s += c[k] * table[static_cast<std::size_t>(m)];
        }
        out[i] = static_cast<float>(s);
    }
}
}  // namespace

BinkAudioDecoder::BinkAudioDecoder(int sample_rate, int channels, bool dct, bool version_b)
    : out_channels_(std::max(1, std::min(2, channels))), out_rate_(sample_rate), dct_(dct), version_b_(version_b) {
    int frame_len_bits = sample_rate < 22050 ? 9 : sample_rate < 44100 ? 10 : 11;
    int rate = sample_rate;
    if (!dct_) {   // RDFT: the channels are interleaved inside one transform
        rate *= out_channels_;
        channels_ = 1;
        if (!version_b_ && out_channels_ == 2) frame_len_bits += 1;
    } else {
        channels_ = out_channels_;
    }
    frame_len_ = 1 << frame_len_bits;
    overlap_len_ = frame_len_ / 16;
    block_size_ = (frame_len_ - overlap_len_) * channels_;
    const long half = (static_cast<long>(rate) + 1) / 2;
    root_ = dct_ ? static_cast<float>(frame_len_ / (std::sqrt(static_cast<double>(frame_len_)) * 32768.0))
                 : static_cast<float>(2.0 / (std::sqrt(static_cast<double>(frame_len_)) * 32768.0));
    for (int i = 0; i < 96; ++i) quant_table_.push_back(std::exp(i * 0.15289164787221953823f) * root_);
    for (num_bands_ = 1; num_bands_ < 25; ++num_bands_)
        if (half <= kCriticalFreqs[num_bands_ - 1]) break;
    bands_.assign(static_cast<std::size_t>(num_bands_) + 1, 0);
    bands_[0] = 2;
    for (int i = 1; i < num_bands_; ++i)
        bands_[static_cast<std::size_t>(i)] = static_cast<int>((static_cast<long>(kCriticalFreqs[i - 1]) * frame_len_ / half) & ~1L);
    bands_[static_cast<std::size_t>(num_bands_)] = frame_len_;
    previous_.assign(static_cast<std::size_t>(channels_), std::vector<float>(static_cast<std::size_t>(overlap_len_), 0.0f));
}

void BinkAudioDecoder::decode_block(BinkAudioBits& gb, std::vector<std::int16_t>& out_pcm) {
    std::vector<std::vector<float>> out(static_cast<std::size_t>(channels_), std::vector<float>(static_cast<std::size_t>(frame_len_)));
    std::vector<float> coeffs(static_cast<std::size_t>(frame_len_) + 2);
    std::vector<float> quant(static_cast<std::size_t>(num_bands_));
    if (dct_) gb.skip(2);
    for (int ch = 0; ch < channels_; ++ch) {
        std::fill(coeffs.begin(), coeffs.end(), 0.0f);
        if (version_b_) {
            std::uint32_t a = gb.get(32), b = gb.get(32);
            float fa, fb;
            std::memcpy(&fa, &a, 4);
            std::memcpy(&fb, &b, 4);
            coeffs[0] = fa * root_;
            coeffs[1] = fb * root_;
        } else {
            coeffs[0] = get_float(gb) * root_;
            coeffs[1] = get_float(gb) * root_;
        }
        for (int i = 0; i < num_bands_; ++i)
            quant[static_cast<std::size_t>(i)] = quant_table_[static_cast<std::size_t>(std::min<std::uint32_t>(gb.get(8), 95))];
        int k = 0;
        float q = quant[0];
        int i = 2;
        while (i < frame_len_) {
            int j;
            if (version_b_) {
                j = i + 16;
            } else if (gb.get1()) {
                j = i + kRleLength[gb.get(4)] * 8;
            } else {
                j = i + 8;
            }
            j = std::min(j, frame_len_);
            const int width = static_cast<int>(gb.get(4));
            if (width == 0) {
                std::fill(coeffs.begin() + i, coeffs.begin() + j, 0.0f);
                i = j;
                while (k < num_bands_ && bands_[static_cast<std::size_t>(k)] < i) q = quant[static_cast<std::size_t>(k++)];
            } else {
                while (i < j) {
                    if (k < num_bands_ && bands_[static_cast<std::size_t>(k)] == i) q = quant[static_cast<std::size_t>(k++)];
                    const int coeff = static_cast<int>(gb.get(width));
                    if (coeff) coeffs[static_cast<std::size_t>(i)] = gb.get1() ? -q * coeff : q * coeff;
                    else coeffs[static_cast<std::size_t>(i)] = 0.0f;
                    ++i;
                }
            }
        }
        if (dct_) {
            coeffs[0] /= 0.5f;
            inverse_dct(coeffs.data(), frame_len_, out[static_cast<std::size_t>(ch)].data());
            // av_tx DCT-III scale 1/(N/2) -- checked against ffmpeg.
            for (float& v : out[static_cast<std::size_t>(ch)]) v *= 2.0f / static_cast<float>(frame_len_);
        } else {
            inverse_rdft(coeffs.data(), frame_len_, out[static_cast<std::size_t>(ch)].data());
        }
    }
    for (int ch = 0; ch < channels_; ++ch) {
        std::vector<float>& o = out[static_cast<std::size_t>(ch)];
        std::vector<float>& prev = previous_[static_cast<std::size_t>(ch)];
        const int count = overlap_len_ * channels_;
        if (!first_) {
            int j = ch;
            for (int i = 0; i < overlap_len_; ++i, j += channels_)
                o[static_cast<std::size_t>(i)] = (prev[static_cast<std::size_t>(i)] * (count - j) + o[static_cast<std::size_t>(i)] * j) / count;
        }
        std::copy(o.begin() + (frame_len_ - overlap_len_), o.end(), prev.begin());
    }
    first_ = false;
    gb.align32();   // get_bits_align32 after every block
    // Output: the first frame_len - overlap samples of every channel. For
    // RDFT the one coded channel already holds interleaved L,R.
    const int n = frame_len_ - overlap_len_;
    auto s16 = [](float v) {
        const float s = v * 32768.0f;
        return static_cast<std::int16_t>(std::lrint(std::max(-32768.0f, std::min(32767.0f, s))));
    };
    if (channels_ == 1) {
        for (int i = 0; i < n; ++i) out_pcm.push_back(s16(out[0][static_cast<std::size_t>(i)]));
    } else {
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < channels_; ++ch) out_pcm.push_back(s16(out[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i)]));
    }
}

void BinkAudioDecoder::decode_packet(const std::uint8_t* data, std::size_t size, std::vector<std::int16_t>& out) {
    if (size < 4) return;
    BinkAudioBits gb(data, size);
    gb.skip(32);   // the packet's decoded-sample count
    while (gb.left() >= 32) decode_block(gb, out);
}

}  // namespace ra2assets
