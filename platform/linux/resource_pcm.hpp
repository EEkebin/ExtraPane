#pragma once
#include <extrapane/microphone_protocol.hpp>
#include <algorithm>
#include <functional>
#include <span>
#include <stdexcept>

namespace dm::linux_client {
// GStreamer sample boundaries need not match the protocol's 10 ms packets.
class PcmPacketizer final {
public:
    using Sink = std::function<void(const extrapane::microphone_wire::Frame&)>;
    explicit PcmPacketizer(extrapane::media::Binding binding, Sink sink) : sink_(std::move(sink)) {
        frame_.binding = binding;
    }
    void push(std::span<const std::uint8_t> bytes, bool muted, bool discontinuity) {
        if (bytes.size() % 2) throw std::runtime_error("Microphone sample is not aligned PCM16");
        if (discontinuity) {
            filled_ = 0;
            frame_.discontinuity = true;
        }
        while (!bytes.empty()) {
            if (!filled_) {
                frame_.position = position_;
                frame_.muted = muted;
            }
            frame_.muted = frame_.muted || muted;
            const auto size = std::min(bytes.size(), frame_.pcm.size() - filled_);
            std::copy_n(bytes.begin(), size, frame_.pcm.begin() + filled_);
            filled_ += size;
            position_ += size / 2;
            bytes = bytes.subspan(size);
            if (filled_ == frame_.pcm.size()) {
                if (frame_.muted) frame_.pcm.fill(0);
                sink_(frame_);
                ++frame_.sequence;
                frame_.discontinuity = false;
                filled_ = 0;
            }
        }
    }

private:
    Sink sink_;
    extrapane::microphone_wire::Frame frame_;
    std::size_t filled_ = 0;
    std::uint64_t position_ = 0;
};
}
