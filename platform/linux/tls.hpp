#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <dm/access.hpp>

namespace dm::linux_client {
// Protocol-compatible with Windows automatic LAN mode: TLS 1.3 encryption,
// intentionally without peer identity authentication. No paired CLI mode.
class Tls final {
public:
    using Deadline = std::chrono::steady_clock::time_point;
    Tls(const std::string& peer, const std::string& local, std::uint16_t port,
        std::function<bool()> cancelled,unsigned interfaceIndex=0,const std::string& code={});
    // Takes ownership of an accepted nonblocking TCP socket.
    Tls(int acceptedSocket, std::function<bool()> cancelled,access::Gate* gate=nullptr);
    ~Tls();
    Tls(const Tls&) = delete;
    Tls& operator=(const Tls&) = delete;
    bool openControl();
    std::vector<access::ChannelCredential> controlChannels(unsigned kind,std::optional<bool> input={});
    std::optional<bool> authorizedDisplayInput()const;
    void requireResource(unsigned kind) const;
    void read(std::span<std::uint8_t> bytes, Deadline deadline);
    void write(std::span<const std::uint8_t> bytes, Deadline deadline);
private:
    void protect(const std::string& code,access::Gate* gate);
    void readRaw(std::span<uint8_t>,Deadline);void writeRaw(std::span<const uint8_t>,Deadline);
    access::Gate* gate_=nullptr;uint64_t control_=0;
    Deadline accessDeadline_;std::unique_ptr<access::Stream> protected_;
    struct State;
    std::unique_ptr<State> state_;
};
}
