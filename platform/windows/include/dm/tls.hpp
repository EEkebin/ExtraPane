#pragma once
#include <dm/network.hpp>
#include <dm/pairing.hpp>
#include <memory>

namespace dm::security {
class TlsStream {
public:
    TlsStream(net::TcpStream socket, bool server, const std::string& pair_name);
    ~TlsStream();
    TlsStream(const TlsStream&) = delete;
    TlsStream& operator=(const TlsStream&) = delete;
    void handshake(std::chrono::steady_clock::time_point deadline);
    void write(std::span<const std::uint8_t> bytes, std::chrono::steady_clock::time_point deadline);
    void read(std::span<std::uint8_t> bytes, std::chrono::steady_clock::time_point deadline);
    void close(std::chrono::steady_clock::time_point deadline);
    std::string description() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
void session_check(const net::Options& options, const std::string& pair_name, bool server);
}
