#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <memory>
#include <span>
#include <functional>
#include <vector>

namespace dm::net {
struct Interface {
    std::uint32_t index;
    std::string name;
    std::string address;
    unsigned prefix;
    bool up;
};

struct Options {
    std::string bind_address;
    std::string peer;
    std::uint16_t port = 43821;
    std::chrono::milliseconds timeout{5000};
};

std::vector<Interface> interfaces();
struct LocalAddress { std::string name,address; bool up=false,ipv6=false; };
// Display list: connected physical adapters only; does not constrain listeners/routes.
std::vector<LocalAddress> local_addresses();
std::string route_source(const std::string& peer);
void listen_once(const Options& options);
void probe_peer(const Options& options);

// Nonblocking stream. -1 from read/write means would-block; 0 means EOF.
class TcpStream {
public:
    struct State;
    explicit TcpStream(std::unique_ptr<State> state);
    ~TcpStream();
    TcpStream(TcpStream&&) noexcept;
    TcpStream& operator=(TcpStream&&) noexcept;
    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;
    int read_some(std::span<std::uint8_t> bytes);
    int write_some(std::span<const std::uint8_t> bytes);
    void wait(bool writing, std::chrono::steady_clock::time_point deadline);
    std::string local_endpoint() const;
    std::string peer_endpoint() const;
private:
    std::unique_ptr<State> state_;
};
TcpStream connect_stream(const Options& options);
TcpStream accept_stream(const Options& options, const std::function<void(const std::string&)>& ready);
}
