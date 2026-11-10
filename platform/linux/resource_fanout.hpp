#pragma once
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include <span>

namespace dm::linux_client {
inline bool independentlyDecodable(std::span<const std::uint8_t> bytes) {
    bool sps = false, pps = false, idr = false;
    for (std::size_t i = 0; i + 3 < bytes.size(); ++i) {
        if (bytes[i] || bytes[i + 1]) continue;
        std::size_t header = 0;
        if (bytes[i + 2] == 1)
            header = i + 3;
        else if (bytes[i + 2] == 0 && i + 4 < bytes.size() && bytes[i + 3] == 1)
            header = i + 4;
        if (!header) continue;
        const auto type = bytes[header] & 31;
        sps |= type == 7;
        pps |= type == 8;
        idr |= type == 5;
        i = header;
    }
    return sps && pps && idr;
}
struct CapturePacket {
    std::vector<std::uint8_t> bytes;
    std::int64_t timestamp = 0;
    bool keyframe = false, discontinuity = false;
};
// Synchronization belongs to the capture hub. Overflow fails one subscription;
// compressed dependency frames must never be silently discarded mid-stream.
class CaptureQueue {
public:
    explicit CaptureQueue(bool camera) : waitingKeyframe_(camera) {}
    void push(std::shared_ptr<const CapturePacket> packet) {
        if (!error.empty()) return;
        if (waitingKeyframe_) {
            if (!packet->keyframe) return;
            waitingKeyframe_ = false;
        }
        if (packets_.size() >= 32 || packet->bytes.size() > 8 * 1024 * 1024 - bytes_) {
            fail("Recipient cannot keep up with capture; reconnect this resource");
            return;
        }
        bytes_ += packet->bytes.size();
        packets_.push_back(std::move(packet));
    }
    std::shared_ptr<const CapturePacket> pop() {
        if (packets_.empty()) return {};
        auto packet = std::move(packets_.front());
        packets_.pop_front();
        bytes_ -= packet->bytes.size();
        return packet;
    }
    void fail(std::string reason) {
        error = std::move(reason);
        packets_.clear();
        bytes_ = 0;
    }
    std::string error;

private:
    bool waitingKeyframe_;
    std::size_t bytes_ = 0;
    std::deque<std::shared_ptr<const CapturePacket>> packets_;
};
}
