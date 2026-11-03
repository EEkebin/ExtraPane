#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace dm::linux_client {
// Protocol-compatible with Windows automatic LAN mode: TLS 1.3 encryption,
// intentionally without peer identity authentication. No paired CLI mode.
class Tls final {
public:
    using Deadline = std::chrono::steady_clock::time_point;
    Tls(const std::string& peer, const std::string& local, std::uint16_t port,
        std::function<bool()> cancelled,unsigned interfaceIndex=0);
    // Takes ownership of an accepted nonblocking TCP socket.
    Tls(int acceptedSocket, std::function<bool()> cancelled);
    ~Tls();
    Tls(const Tls&) = delete;
    Tls& operator=(const Tls&) = delete;
    void read(std::span<std::uint8_t> bytes, Deadline deadline);
    void write(std::span<const std::uint8_t> bytes, Deadline deadline);
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
