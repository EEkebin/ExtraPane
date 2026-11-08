#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace extrapane::peers::transport {
using Deadline = std::chrono::steady_clock::time_point;
// Encryption only: ephemeral certificates do not authenticate the remote peer.
// Complete peer_auth before accepting application messages.
class ServerIdentity {
public:
    ServerIdentity();
    ~ServerIdentity();
    ServerIdentity(const ServerIdentity&) = delete;
    ServerIdentity& operator=(const ServerIdentity&) = delete;
private:
    struct State;
    std::unique_ptr<State> state_;
    friend class Listener;
};
class Stream {
public:
    ~Stream();
    Stream(Stream&&) noexcept;
    Stream& operator=(Stream&&) noexcept;
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    static Stream connect(const std::string& peer, std::uint16_t port,
                          const std::string& local, Deadline deadline);
    // Exact transfers, at most 1 MiB per call. An error makes this stream unusable.
    // One I/O owner; read/write may not run concurrently. cancel() is safe from
    // another thread, provided this object remains alive and is not being moved.
    void read(std::span<std::uint8_t> bytes, Deadline deadline);
    void write(std::span<const std::uint8_t> bytes, Deadline deadline);
    // Nonblocking pump: -1 means retry, 0 means clean TLS EOF, positive is bytes.
    // After -1 retry the same operation and buffer (writes retain their length).
    // Other direction may be serviced between retries by this same I/O owner.
    int read_some(std::span<std::uint8_t> bytes);
    int write_some(std::span<const std::uint8_t> bytes);
    // Wait for either outstanding TLS operation's requested direction.
    void wait_io(Deadline deadline);
    std::array<std::uint8_t,32> binding() const;
    void cancel() noexcept;
    std::string peer_address() const;
private:
    struct State;
    std::unique_ptr<State> state_;
    explicit Stream(std::unique_ptr<State>);
    friend class Listener;
};
class Listener {
public:
    // Explicit local unicast IPv4 or localhost only; no wildcard or DNS lookup.
    // port=0 allocates an ephemeral port (available through port()).
    Listener(const std::string& bind, std::uint16_t port,
             std::shared_ptr<ServerIdentity> identity = {});
    ~Listener();
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    Stream accept(Deadline deadline);
    void cancel() noexcept;
    std::uint16_t port() const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
