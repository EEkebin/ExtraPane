#pragma once
#include "channel.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace extrapane::media {
constexpr std::size_t max_frame_bytes = 2 * 1024 * 1024;
// One immutable binding per admitted resource lifetime. Never reuse a generation
// when restarting a resource within a session. This is routing, not admission.
struct Binding {
    std::uint64_t resource = 0, generation = 0;
    bool operator==(const Binding&) const = default;
};
struct Frame {
    Binding binding;
    std::uint64_t sequence = 0;
    std::int64_t timestamp = 0; // capture time in 100 ns units
    wire::Bytes payload;        // one complete H.264 access unit
};
using Header = std::array<std::uint8_t, 48>;
Header encode_header(const Frame& frame);
Header encode_header(Binding binding, std::uint64_t sequence, std::int64_t timestamp, std::size_t payload_bytes);
// Payload length is validated before allocation. Payload bytes are not read here.
Frame decode_header(std::span<const std::uint8_t> header);

// Single serialized I/O executor, already admitted encrypted resource stream.
// Its transport must enforce cancellation/deadlines; never run on the UI thread.
class Channel final {
public:
    Channel(Binding binding, wire::ControlChannel::Read read, wire::ControlChannel::Write write);
    void send(const Frame& frame);
    Frame receive();
    bool failed() const noexcept { return failed_; }

private:
    void validate_sequence(const Frame& frame, const std::optional<std::uint64_t>& previous) const;
    Binding binding_;
    wire::ControlChannel::Read read_;
    wire::ControlChannel::Write write_;
    std::optional<std::uint64_t> sent_, received_;
    bool failed_ = false;
};

// Bounded producer/consumer queue. Overflow terminates this resource: dropping
// arbitrary H.264 reference frames would corrupt decoding. A restart must create
// a fresh encoder and generation. close() discards frames and wakes the worker.
class FrameQueue final {
public:
    explicit FrameQueue(Binding binding, std::size_t capacity = 3);
    bool push(Frame frame);
    std::optional<Frame> pop_for(std::chrono::milliseconds timeout);
    void close() noexcept;
    bool closed() const;

private:
    Binding binding_;
    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Frame> frames_;
    std::optional<std::uint64_t> last_;
    bool closed_ = false;
};
}
