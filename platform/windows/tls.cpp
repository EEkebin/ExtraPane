#include <windows.h>
#include <dm/tls.hpp>
#include <dm/probe.hpp>
#include <mbedtls/ssl.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <psa/crypto.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace dm::security {
namespace {
using Deadline = std::chrono::steady_clock::time_point;
void check_deadline(Deadline deadline) {
    if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Secure operation timed out");
}
[[noreturn]] void tls_error(const char* stage, int code) {
    std::array<char, 200> text{};
    mbedtls_strerror(code, text.data(), text.size());
    throw std::runtime_error(std::string(stage) + ": " + text.data());
}
int send_callback(void* ctx, const unsigned char* bytes, std::size_t size) noexcept {
    try {
        const int result = static_cast<net::TcpStream*>(ctx)->write_some({bytes, size});
        return result == -1 ? MBEDTLS_ERR_SSL_WANT_WRITE : result;
    } catch (...) { return MBEDTLS_ERR_NET_SEND_FAILED; }
}
int recv_callback(void* ctx, unsigned char* bytes, std::size_t size) noexcept {
    try {
        const int result = static_cast<net::TcpStream*>(ctx)->read_some({bytes, size});
        return result == -1 ? MBEDTLS_ERR_SSL_WANT_READ : result;
    } catch (...) { return MBEDTLS_ERR_NET_RECV_FAILED; }
}
}
struct TlsStream::State {
    net::TcpStream socket;
    mbedtls_ssl_config config{};
    mbedtls_ssl_context ssl{};
    explicit State(net::TcpStream value) : socket(std::move(value)) {
        mbedtls_ssl_config_init(&config);
        mbedtls_ssl_init(&ssl);
    }
    ~State() { mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&config); }
    bool retry(int code, Deadline deadline) {
        if (code != MBEDTLS_ERR_SSL_WANT_READ && code != MBEDTLS_ERR_SSL_WANT_WRITE) return false;
        socket.wait(code == MBEDTLS_ERR_SSL_WANT_WRITE, deadline);
        return true;
    }
};
TlsStream::TlsStream(net::TcpStream socket, bool server, const std::string& pair_name)
    : state_(std::make_unique<State>(std::move(socket))) {
    if (psa_crypto_init() != PSA_SUCCESS) throw std::runtime_error("Initialize system-backed crypto failed");
    auto key = load_pairing(pair_name);
    const auto erase = [&] { SecureZeroMemory(key.data(), key.size()); };
    try {
        int result = mbedtls_ssl_config_defaults(&state_->config, server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        if (result) tls_error("TLS configuration", result);
        mbedtls_ssl_conf_min_tls_version(&state_->config, MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_max_tls_version(&state_->config, MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_tls13_key_exchange_modes(&state_->config, MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL);
        mbedtls_ssl_conf_session_tickets(&state_->config, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
        mbedtls_ssl_conf_new_session_tickets(&state_->config, 0);
        result = mbedtls_ssl_conf_psk(&state_->config, key.data(), key.size(), reinterpret_cast<const unsigned char*>(pair_name.data()), pair_name.size());
        if (result) tls_error("TLS pairing", result);
        result = mbedtls_ssl_setup(&state_->ssl, &state_->config);
        if (result) tls_error("TLS setup", result);
        mbedtls_ssl_set_bio(&state_->ssl, &state_->socket, send_callback, recv_callback, nullptr);
    } catch (...) { erase(); throw; }
    erase();
}
TlsStream::~TlsStream() = default;
void TlsStream::handshake(Deadline deadline) {
    for (;;) {
        check_deadline(deadline);
        const int result = mbedtls_ssl_handshake(&state_->ssl);
        if (result == 0) return;
        if (!state_->retry(result, deadline)) tls_error("TLS authentication failed", result);
    }
}
void TlsStream::write(std::span<const std::uint8_t> bytes, Deadline deadline) {
    while (!bytes.empty()) {
        check_deadline(deadline);
        const int result = mbedtls_ssl_write(&state_->ssl, bytes.data(), std::min<std::size_t>(bytes.size(), 16384));
        if (result > 0) { bytes = bytes.subspan(static_cast<std::size_t>(result)); continue; }
        if (!state_->retry(result, deadline)) tls_error("TLS write", result);
    }
}
void TlsStream::read(std::span<std::uint8_t> bytes, Deadline deadline) {
    while (!bytes.empty()) {
        check_deadline(deadline);
        const int result = mbedtls_ssl_read(&state_->ssl, bytes.data(), bytes.size());
        if (result > 0) { bytes = bytes.subspan(static_cast<std::size_t>(result)); continue; }
        if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) throw std::runtime_error("TLS peer closed before the complete message");
        if (!state_->retry(result, deadline)) tls_error("TLS read", result);
    }
}
void TlsStream::close(Deadline deadline) {
    for (;;) {
        check_deadline(deadline);
        const int result = mbedtls_ssl_close_notify(&state_->ssl);
        if (result == 0) return;
        if (!state_->retry(result, deadline)) tls_error("TLS close", result);
    }
}
std::string TlsStream::description() const {
    return std::string(mbedtls_ssl_get_version(&state_->ssl)) + " " + mbedtls_ssl_get_ciphersuite(&state_->ssl);
}

void session_check(const net::Options& options, const std::string& pair_name, bool server) {
    // Resolve trust before opening a port or trying to connect.
    auto key = load_pairing(pair_name);
    SecureZeroMemory(key.data(), key.size());
    auto socket = server ? net::accept_stream(options, [](const std::string& local) { std::cout << "LISTENING " << local << " TLS-only" << std::endl; })
                         : net::connect_stream(options);
    TlsStream stream(std::move(socket), server, pair_name);
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    stream.handshake(deadline);
    // Diagnostic application payload, with no media/input capabilities or grants.
    probe::Packet packet{};
    if (!server) {
        probe::Token token{};
        if (psa_generate_random(token.data(), token.size()) != PSA_SUCCESS) throw std::runtime_error("Random token generation failed");
        packet = probe::encode({probe::Kind::request, token});
        stream.write(packet, deadline);
        stream.read(packet, deadline);
        const auto response = probe::decode(packet);
        if (response.kind != probe::Kind::response || response.token != token) throw std::runtime_error("Secure session response mismatch");
    } else {
        stream.read(packet, deadline);
        const auto request = probe::decode(packet);
        if (request.kind != probe::Kind::request) throw std::runtime_error("Expected secure session request");
        stream.write(probe::encode({probe::Kind::response, request.token}), deadline);
    }
    stream.close(deadline);
    std::cout << "SESSION_OK " << stream.description() << " permissions=none capabilities=diagnostic-only" << std::endl;
}
}
