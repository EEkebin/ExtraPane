#pragma once
#include "../linux/resource_pcm.hpp"
#include <mutex>

namespace dm::mac {
class MicrophoneMuteGate final {
    mutable std::mutex mutex_;
    bool muted_ = false;
    std::int64_t unmuteBoundary_ = 0;

public:
    void set(bool value, std::int64_t hostTime) {
        std::lock_guard lock(mutex_);
        if (muted_ && !value) unmuteBoundary_ = hostTime;
        muted_ = value;
    }
    bool current() const {
        std::lock_guard lock(mutex_);
        return muted_;
    }
    bool sampleMuted(std::int64_t capturedAt) const {
        std::lock_guard lock(mutex_);
        return muted_ || capturedAt < unmuteBoundary_;
    }
};
// Capture buffers can contain several wire frames. Recheck live mute before
// each send, including after a preceding send blocked on network backpressure.
class MicrophoneDelivery final {
    std::function<bool()> muted_;
    dm::linux_client::PcmPacketizer packetizer_;

public:
    MicrophoneDelivery(extrapane::media::Binding binding, std::function<bool()> muted,
                       dm::linux_client::PcmPacketizer::Sink sink)
        : muted_(std::move(muted)), packetizer_(binding, [this, sink = std::move(sink)](auto frame) {
              if (muted_()) {
                  frame.muted = true;
                  frame.pcm.fill(0);
              }
              sink(frame);
          }) {}
    MicrophoneDelivery(const MicrophoneDelivery&) = delete;
    MicrophoneDelivery& operator=(const MicrophoneDelivery&) = delete;
    void push(std::span<const std::uint8_t> bytes, bool captureMuted, bool discontinuity) {
        packetizer_.push(bytes, captureMuted || muted_(), discontinuity);
    }
};
}
