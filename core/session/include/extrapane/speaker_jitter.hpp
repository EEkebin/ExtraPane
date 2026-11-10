#pragma once
#include "speaker_protocol.hpp"
#include <algorithm>
#include <array>
#include <span>

namespace extrapane::speaker_wire {
// Owned by one native audio worker. Feed it from a separate bounded transport
// queue; never call accept/render concurrently. No locks or runtime allocations.
class JitterBuffer final {
public:
    static constexpr std::size_t initial_frames = 480 * 4, max_target_frames = 480 * 8, capacity_frames = 480 * 12,
                                 fade_frames = 96;
    struct Statistics {
        std::uint64_t underruns = 0, dropped_frames = 0, discontinuities = 0;
        std::size_t queued_frames = 0, target_frames = initial_frames;
    };
    void reset() noexcept {
        head_ = size_ = 0;
        playing_ = false;
        target_ = initial_frames;
        last_ = {};
        origin_ = {};
        fade_ = 0;
        muted_ = false;
    }
    void accept(const Frame& frame) noexcept {
        if (frame.muted != muted_ || frame.discontinuity) {
            reset();
            muted_ = frame.muted;
            if (frame.discontinuity) ++discontinuities_;
        }
        if (muted_) return;
        if (size_ + sample_count > capacity_frames) {
            const auto remove = size_ - (target_ - sample_count);
            head_ = (head_ + remove) % capacity_frames;
            size_ -= remove;
            dropped_ += remove;
            transition();
        }
        for (std::size_t i = 0; i < sample_count; ++i) {
            auto& pair = samples_[(head_ + size_) % capacity_frames];
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const auto at = (i * 2 + channel) * 2;
                const auto value =
                    static_cast<unsigned>(frame.pcm[at]) | (static_cast<unsigned>(frame.pcm[at + 1]) << 8);
                pair[channel] = static_cast<std::int16_t>(value < 32768 ? static_cast<int>(value)
                                                                        : static_cast<int>(value) - 65536);
            }
            ++size_;
        }
    }
    // Interleaved stereo PCM16; malformed odd-sized requests safely emit silence.
    void render(std::span<std::int16_t> output) noexcept {
        std::fill(output.begin(), output.end(), 0);
        if (output.size() % 2 || muted_) return;
        if (!playing_ && size_ >= target_) {
            playing_ = true;
            transition();
        }
        for (std::size_t at = 0; at < output.size(); at += 2) {
            std::array<std::int16_t, 2> sample{};
            if (playing_) {
                if (size_) {
                    sample = samples_[head_];
                    head_ = (head_ + 1) % capacity_frames;
                    --size_;
                } else {
                    ++underruns_;
                    playing_ = false;
                    target_ = std::min(max_target_frames, target_ + sample_count * 2);
                    transition();
                }
            }
            if (fade_) --fade_;
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const auto value =
                    (static_cast<std::int64_t>(origin_[channel]) * static_cast<std::int64_t>(fade_) +
                     static_cast<std::int64_t>(sample[channel]) * static_cast<std::int64_t>(fade_frames - fade_)) /
                    static_cast<std::int64_t>(fade_frames);
                output[at + channel] = last_[channel] = static_cast<std::int16_t>(value);
            }
        }
    }
    Statistics statistics() const noexcept { return {underruns_, dropped_, discontinuities_, size_, target_}; }

private:
    void transition() noexcept {
        origin_ = last_;
        fade_ = fade_frames;
    }
    std::array<std::array<std::int16_t, 2>, capacity_frames> samples_{};
    std::array<std::int16_t, 2> last_{}, origin_{};
    std::size_t head_ = 0, size_ = 0, target_ = initial_frames, fade_ = 0;
    std::uint64_t underruns_ = 0, dropped_ = 0, discontinuities_ = 0;
    bool playing_ = false, muted_ = false;
};
}
