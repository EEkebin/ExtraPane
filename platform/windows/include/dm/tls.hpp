#pragma once
#include <dm/network.hpp>
#include <dm/pairing.hpp>
#include <memory>
#include <dm/access.hpp>

namespace dm::security {
void set_access_client(std::string code);
std::shared_ptr<access::Gate> enable_access_host();
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
    // Cross-thread cancellation only shuts down the native socket. The caller
    // must keep this stream alive until the canceling thread has joined.
    void cancel() noexcept;
    std::string description() const;
private:
    void read_raw(std::span<std::uint8_t>,std::chrono::steady_clock::time_point);
    void write_raw(std::span<const std::uint8_t>,std::chrono::steady_clock::time_point);
    std::unique_ptr<access::Stream> protected_;
    std::chrono::steady_clock::time_point access_deadline_;
    bool server_=false;
    struct State;
    std::unique_ptr<State> state_;
};
void session_check(const net::Options& options, const std::string& pair_name, bool server);
}
