#pragma once
#include "peer_rpc.hpp"
#include <memory>

namespace extrapane::peers {
// Single I/O owner; emit queues onto an authenticated connection. All endpoint
// ports come from trusted local adapters, never a remote request. Only loopback
// sockets are created. Call revoke immediately when the resource grant ends.
class MediaBridge {
    struct State;
    std::unique_ptr<State> state_;

public:
    using Emit = std::function<void(const rpc::Message&)>;
    MediaBridge(bool initiator, rpc::Multiplexer::Authorize, Emit);
    ~MediaBridge();
    MediaBridge(const MediaBridge&) = delete;
    // Local native source connects here. Multiple child sockets share the grant.
    std::uint16_t listen(std::uint64_t grant, bool paused = false);
    // Remote stream_open connects to this exact local native receiver port.
    void connect(std::uint64_t grant, std::uint16_t trusted_local_port, bool paused = false);
    // Pause new local accepts until native preparation and the grant reply finish.
    void resume(std::uint64_t grant);
    void receive(const rpc::Message&);
    bool pump(); // Nonblocking, bounded work per call.
    void revoke(std::uint64_t grant);
    std::size_t streams() const noexcept;
};
}
