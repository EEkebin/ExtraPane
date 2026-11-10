#pragma once
#include <dm/network.hpp>
#include <dm/pairing.hpp>
#include <dm/prefix_buffer.hpp>
#include <memory>
#include <optional>
#include <dm/access.hpp>

namespace dm::security {
void set_access_client(std::string code);
std::shared_ptr<access::Gate> enable_access_host();
class TlsStream {
public:
    TlsStream(net::TcpStream socket, bool server, const std::string& pair_name,
              std::optional<std::string> credential = std::nullopt);
    ~TlsStream();
    TlsStream(const TlsStream&) = delete;
    TlsStream& operator=(const TlsStream&) = delete;
    void handshake(std::chrono::steady_clock::time_point deadline);
    void write(std::span<const std::uint8_t> bytes, std::chrono::steady_clock::time_point deadline);
    void read(std::span<std::uint8_t> bytes, std::chrono::steady_clock::time_point deadline);
    // Inspect at most eight admitted application bytes without consuming them.
    void peek(std::span<std::uint8_t> bytes, std::chrono::steady_clock::time_point deadline);
    void close(std::chrono::steady_clock::time_point deadline);
    // Cross-thread cancellation only shuts down the native socket. The caller
    // must keep this stream alive until the canceling thread has joined.
    void cancel() noexcept;
    std::string description() const;
    // Group tickets are issued only from a code-authenticated root connection.
    std::vector<access::ChannelCredential> issue_channels(unsigned mask);
    void require_resource(unsigned kind) const;
    bool open_control();
    std::vector<access::ChannelCredential> control_channels(unsigned kind, std::optional<bool> display_input = {});
    std::optional<bool> authorized_display_input() const;

private:
    void read_raw(std::span<std::uint8_t>, std::chrono::steady_clock::time_point);
    void write_raw(std::span<const std::uint8_t>, std::chrono::steady_clock::time_point);
    dm::protocol::PrefixBuffer prefix_;
    std::unique_ptr<access::Stream> protected_;
    std::chrono::steady_clock::time_point access_deadline_;
    std::string credential_;
    bool server_ = false;
    uint64_t control_ = 0;
    std::shared_ptr<access::Gate> control_gate_;
    struct State;
    std::unique_ptr<State> state_;
};
void session_check(const net::Options& options, const std::string& pair_name, bool server);
}
