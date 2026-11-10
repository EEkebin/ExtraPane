#include <extrapane/peer_transport.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#endif
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/ec.h>
#include <openssl/x509.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <limits>
#include <stdexcept>

namespace extrapane::peers::transport {
namespace {
#ifdef _WIN32
using Socket = SOCKET;
using SockLen = int;
constexpr Socket invalid = INVALID_SOCKET;
int socket_error() {
    return WSAGetLastError();
}
bool pending(int e) {
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
bool interrupted(int e) {
    return e == WSAEINTR;
}
void close_socket(Socket s) {
    closesocket(s);
}
void shutdown_socket(Socket s) {
    shutdown(s, SD_BOTH);
}
void runtime() {
    struct Runtime {
        Runtime() {
            WSADATA d{};
            if (WSAStartup(MAKEWORD(2, 2), &d)) throw std::runtime_error("Winsock initialization failed");
        }
        ~Runtime() { WSACleanup(); }
    };
    static Runtime r;
}
#else
using Socket = int;
using SockLen = socklen_t;
constexpr Socket invalid = -1;
int socket_error() {
    return errno;
}
bool pending(int e) {
    return e == EWOULDBLOCK || e == EAGAIN || e == EINPROGRESS;
}
bool interrupted(int e) {
    return e == EINTR;
}
void close_socket(Socket s) {
    ::close(s);
}
void shutdown_socket(Socket s) {
    shutdown(s, SHUT_RDWR);
}
void runtime() {}
#endif
[[noreturn]] void fail(const char* text) {
    throw std::runtime_error(text);
}
struct SocketOwner {
    Socket socket = invalid;
    std::atomic<bool> cancelled{false};
    const std::atomic<bool>* parentCancelled = nullptr;
    ~SocketOwner() {
        if (socket != invalid) close_socket(socket);
    }
    void cancel() noexcept {
        cancelled.store(true);
        if (socket != invalid) shutdown_socket(socket);
    }
    void check(Deadline d) const {
        if (cancelled.load() || (parentCancelled && parentCancelled->load()))
            throw Error(ErrorReason::cancelled, "Connection cancelled");
        if (std::chrono::steady_clock::now() >= d) throw Error(ErrorReason::timeout, "Connection timed out");
    }
    void wait_events(short events, Deadline d) {
        for (;;) {
            check(d);
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(d - std::chrono::steady_clock::now()).count();
            const int ms = static_cast<int>(std::clamp<std::int64_t>(remaining, 1, 50));
#ifdef _WIN32
            WSAPOLLFD p{socket, static_cast<SHORT>(events), 0};
            const int r = WSAPoll(&p, 1, ms);
#else
            pollfd p{socket, events, 0};
            const int r = poll(&p, 1, ms);
#endif
            if (r < 0) {
                if (interrupted(socket_error())) continue;
                throw Error(ErrorReason::network, "Socket wait failed");
            }
            if (r > 0) {
                check(d);
                if (p.revents & POLLNVAL) fail("Socket is invalid");
                if (p.revents & (p.events | POLLERR | POLLHUP)) return;
            }
        }
    }
    void wait(bool write, Deadline d) { wait_events(static_cast<short>(write ? POLLOUT : POLLIN), d); }
};
sockaddr_in address(std::string text, std::uint16_t port, bool allow_wildcard = false) {
    std::string folded = text;
    std::transform(folded.begin(), folded.end(), folded.begin(),
                   [](unsigned char c) { return c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : char(c); });
    if (folded == "localhost") text = "127.0.0.1";
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if (inet_pton(AF_INET, text.c_str(), &a.sin_addr) != 1) fail("A numeric IPv4 address or localhost is required");
    const auto first = ntohl(a.sin_addr.s_addr) >> 24;
    if ((first == 0 && !(allow_wildcard && a.sin_addr.s_addr == htonl(INADDR_ANY))) || first >= 224)
        fail("An explicit unicast IPv4 address is required");
    return a;
}
void configure(Socket s) {
#ifdef _WIN32
    u_long yes = 1;
    if (ioctlsocket(s, FIONBIO, &yes)) fail("Cannot make socket nonblocking");
#else
    const int flags = fcntl(s, F_GETFL);
    if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0 || fcntl(s, F_SETFD, FD_CLOEXEC) < 0)
        fail("Cannot configure socket");
#ifdef SO_NOSIGPIPE
    const int noSignal = 1;
    if (setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof(noSignal))) fail("Cannot suppress socket signals");
#endif
#endif
    const int yesFlag = 1;
    if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yesFlag), sizeof(yesFlag)))
        fail("Cannot disable Nagle buffering");
}
// Keep the native socket width and suppress SIGPIPE without process-wide handlers.
const BIO_METHOD* socket_method() {
    struct Method {
        BIO_METHOD* value = BIO_meth_new(BIO_TYPE_SOURCE_SINK, "ExtraPane socket");
        Method() {
            if (!value) fail("Cannot create socket BIO method");
            BIO_meth_set_create(value, [](BIO* b) {
                BIO_set_init(b, 1);
                return 1;
            });
            BIO_meth_set_destroy(value, [](BIO*) { return 1; });
            BIO_meth_set_ctrl(value, [](BIO*, int cmd, long, void*) -> long { return cmd == BIO_CTRL_FLUSH ? 1 : 0; });
            BIO_meth_set_read(value, [](BIO* b, char* data, int size) {
                BIO_clear_retry_flags(b);
                const auto socket = static_cast<SocketOwner*>(BIO_get_data(b))->socket;
                const int r = static_cast<int>(::recv(socket, data, size, 0));
                if (r < 0 && (pending(socket_error()) || interrupted(socket_error()))) BIO_set_retry_read(b);
                return r;
            });
            BIO_meth_set_write(value, [](BIO* b, const char* data, int size) {
                BIO_clear_retry_flags(b);
                const auto socket = static_cast<SocketOwner*>(BIO_get_data(b))->socket;
#ifdef MSG_NOSIGNAL
                constexpr int flags = MSG_NOSIGNAL;
#else
                constexpr int flags=0;
#endif
                const int r = static_cast<int>(::send(socket, data, size, flags));
                if (r < 0 && (pending(socket_error()) || interrupted(socket_error()))) BIO_set_retry_write(b);
                return r;
            });
        }
        ~Method() { BIO_meth_free(value); }
    };
    static Method method;
    return method.value;
}
using Context = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;
Context context(bool server) {
    Context c(SSL_CTX_new(server ? TLS_server_method() : TLS_client_method()), SSL_CTX_free);
    if (!c || !SSL_CTX_set_min_proto_version(c.get(), TLS1_3_VERSION) ||
        !SSL_CTX_set_max_proto_version(c.get(), TLS1_3_VERSION))
        fail("Cannot configure TLS 1.3");
    SSL_CTX_set_verify(c.get(), SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_session_cache_mode(c.get(), SSL_SESS_CACHE_OFF);
    SSL_CTX_set_options(c.get(), SSL_OP_NO_TICKET);
    if (server) SSL_CTX_set_num_tickets(c.get(), 0);
    return c;
}
}
struct ServerIdentity::State {
    Context context{transport::context(true)};
};
ServerIdentity::ServerIdentity() : state_(std::make_unique<State>()) {
    std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)> ec(EC_KEY_new_by_curve_name(NID_X9_62_prime256v1), EC_KEY_free);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(EVP_PKEY_new(), EVP_PKEY_free);
    std::unique_ptr<X509, decltype(&X509_free)> cert(X509_new(), X509_free);
    if (!ec || !key || !cert || !EC_KEY_generate_key(ec.get()) || !EVP_PKEY_set1_EC_KEY(key.get(), ec.get()) ||
        !X509_set_version(cert.get(), 2) || !ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1) ||
        !X509_gmtime_adj(X509_get_notBefore(cert.get()), -60) ||
        !X509_gmtime_adj(X509_get_notAfter(cert.get()), 86400) || !X509_set_pubkey(cert.get(), key.get()))
        fail("Cannot create ephemeral TLS certificate");
    auto* name = X509_get_subject_name(cert.get());
    if (!X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                    reinterpret_cast<const unsigned char*>("ExtraPane ephemeral transport"), -1, -1,
                                    0) ||
        !X509_set_issuer_name(cert.get(), name) || X509_sign(cert.get(), key.get(), EVP_sha256()) <= 0 ||
        !SSL_CTX_use_certificate(state_->context.get(), cert.get()) ||
        !SSL_CTX_use_PrivateKey(state_->context.get(), key.get()))
        fail("Cannot sign ephemeral TLS certificate");
}
ServerIdentity::~ServerIdentity() = default;
struct Stream::State : SocketOwner {
    Context context{nullptr, SSL_CTX_free};
    std::shared_ptr<ServerIdentity> identity;
    std::unique_ptr<SSL, decltype(&SSL_free)> ssl{nullptr, SSL_free};
    std::string peer;
    short readWant = 0, writeWant = 0;
    void retry(int r, Deadline d) {
        const int e = SSL_get_error(ssl.get(), r);
        if (e == SSL_ERROR_WANT_READ)
            wait(false, d);
        else if (e == SSL_ERROR_WANT_WRITE)
            wait(true, d);
        else
            throw Error((e == SSL_ERROR_SYSCALL || e == SSL_ERROR_ZERO_RETURN) ? ErrorReason::network
                                                                               : ErrorReason::protocol,
                        "TLS connection closed or failed");
    }
    void handshake(SSL_CTX* ctx, bool server, Deadline d) {
        ssl.reset(SSL_new(ctx));
        if (!ssl) fail("Cannot create TLS stream");
        auto* bio = BIO_new(socket_method());
        if (!bio) fail("Cannot create TLS socket BIO");
        BIO_set_data(bio, static_cast<SocketOwner*>(this));
        SSL_set_bio(ssl.get(), bio, bio);
        for (;;) {
            check(d);
            ERR_clear_error();
            const int r = server ? SSL_accept(ssl.get()) : SSL_connect(ssl.get());
            if (r == 1) return;
            retry(r, d);
        }
    }
};
Stream::Stream(std::unique_ptr<State> s) : state_(std::move(s)) {}
Stream::~Stream() = default;
Stream::Stream(Stream&&) noexcept = default;
Stream& Stream::operator=(Stream&&) noexcept = default;
Stream Stream::connect(const std::string& peer, std::uint16_t port, const std::string& local, Deadline d) {
    runtime();
    if (!port) fail("Destination port is required");
    const auto dst = address(peer, port);
    sockaddr_in src{};
    if (!local.empty()) src = address(local, 0);
    auto s = std::make_unique<State>();
    s->socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s->socket == invalid) fail("Cannot create socket");
    configure(s->socket);
    if (!local.empty() && ::bind(s->socket, reinterpret_cast<const sockaddr*>(&src), sizeof(src)))
        fail("Cannot bind selected local address");
    if (::connect(s->socket, reinterpret_cast<const sockaddr*>(&dst), sizeof(dst))) {
        if (!pending(socket_error())) throw Error(ErrorReason::network, "Cannot connect to peer");
        s->wait(true, d);
        int error = 0;
        SockLen size = sizeof(error);
        if (getsockopt(s->socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) || error)
            throw Error(ErrorReason::network, "Cannot connect to peer");
    }
    char text[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &dst.sin_addr, text, sizeof(text));
    s->peer = text;
    s->context = context(false);
    s->handshake(s->context.get(), false, d);
    return Stream(std::move(s));
}
void Stream::read(std::span<std::uint8_t> bytes, Deadline d) {
    if (!state_) fail("Stream is empty");
    try {
        if (bytes.size() > 1024 * 1024) fail("Read exceeds transport limit");
        while (!bytes.empty()) {
            state_->check(d);
            ERR_clear_error();
            const int r = SSL_read(state_->ssl.get(), bytes.data(), static_cast<int>(bytes.size()));
            if (r > 0)
                bytes = bytes.subspan(r);
            else
                state_->retry(r, d);
        }
    } catch (...) {
        cancel();
        throw;
    }
}
void Stream::write(std::span<const std::uint8_t> bytes, Deadline d) {
    if (!state_) fail("Stream is empty");
    try {
        if (bytes.size() > 1024 * 1024) fail("Write exceeds transport limit");
        while (!bytes.empty()) {
            state_->check(d);
            ERR_clear_error();
            const int r = SSL_write(state_->ssl.get(), bytes.data(),
                                    static_cast<int>(std::min<std::size_t>(bytes.size(), 16384)));
            if (r > 0)
                bytes = bytes.subspan(r);
            else
                state_->retry(r, d);
        }
    } catch (...) {
        cancel();
        throw;
    }
}
int Stream::read_some(std::span<std::uint8_t> bytes) {
    if (!state_) fail("Stream is empty");
    try {
        if (bytes.empty() || bytes.size() > 1024 * 1024) fail("Invalid nonblocking read size");
        if (state_->cancelled.load()) throw Error(ErrorReason::cancelled, "Connection cancelled");
        ERR_clear_error();
        const int r = SSL_read(state_->ssl.get(), bytes.data(), static_cast<int>(bytes.size()));
        state_->readWant = 0;
        if (r > 0) return r;
        const int e = SSL_get_error(state_->ssl.get(), r);
        if (e == SSL_ERROR_ZERO_RETURN) return 0;
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            state_->readWant = static_cast<short>(e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT);
            return -1;
        }
        throw Error(e == SSL_ERROR_SYSCALL ? ErrorReason::network : ErrorReason::protocol, "TLS read failed");
    } catch (...) {
        cancel();
        throw;
    }
}
int Stream::write_some(std::span<const std::uint8_t> bytes) {
    if (!state_) fail("Stream is empty");
    try {
        if (bytes.empty() || bytes.size() > 16384) fail("Nonblocking writes require 1 to 16384 bytes");
        if (state_->cancelled.load()) throw Error(ErrorReason::cancelled, "Connection cancelled");
        ERR_clear_error();
        const int r = SSL_write(state_->ssl.get(), bytes.data(), static_cast<int>(bytes.size()));
        state_->writeWant = 0;
        if (r > 0) return r;
        const int e = SSL_get_error(state_->ssl.get(), r);
        if (e == SSL_ERROR_ZERO_RETURN) return 0;
        if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
            state_->writeWant = static_cast<short>(e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT);
            return -1;
        }
        throw Error(e == SSL_ERROR_SYSCALL ? ErrorReason::network : ErrorReason::protocol, "TLS write failed");
    } catch (...) {
        cancel();
        throw;
    }
}
void Stream::wait_io(Deadline d) {
    if (!state_) fail("Stream is empty");
    try {
        const short events = static_cast<short>(state_->readWant | state_->writeWant);
        state_->wait_events(events ? events : POLLIN, d);
    } catch (...) {
        cancel();
        throw;
    }
}
std::array<std::uint8_t, 32> Stream::binding() const {
    if (!state_) fail("Stream is empty");
    constexpr char label[] = "ExtraPane/peer-control/1";
    std::array<std::uint8_t, 32> result{};
    if (SSL_export_keying_material(state_->ssl.get(), result.data(), result.size(), label, sizeof(label) - 1, nullptr,
                                   0, 0) != 1)
        fail("Cannot export TLS channel binding");
    return result;
}
void Stream::cancel() noexcept {
    if (state_) state_->cancel();
}
std::string Stream::peer_address() const {
    return state_ ? state_->peer : std::string{};
}
struct Listener::State : SocketOwner {
    std::shared_ptr<ServerIdentity> identity;
    std::uint16_t port = 0;
};
Listener::Listener(const std::string& bind, std::uint16_t port, std::shared_ptr<ServerIdentity> identity)
    : state_(std::make_unique<State>()) {
    runtime();
    auto addr = address(bind, port, true);
    auto& s = *state_;
    s.identity = identity ? std::move(identity) : std::make_shared<ServerIdentity>();
    s.socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s.socket == invalid) fail("Cannot create listener");
    configure(s.socket);
#ifdef _WIN32
    const int exclusive = 1;
    if (setsockopt(s.socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
                   sizeof(exclusive)))
        fail("Cannot reserve listener address");
#else
    // Permit restarting our listener after accepted TCP connections enter
    // TIME_WAIT. Never use SO_REUSEPORT: live listeners must remain exclusive.
    const int reuse = 1;
    if (setsockopt(s.socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)))
        fail("Cannot configure listener restart");
#endif
    if (::bind(s.socket, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) || ::listen(s.socket, 16))
        fail("Cannot listen on selected address");
    SockLen size = sizeof(addr);
    if (getsockname(s.socket, reinterpret_cast<sockaddr*>(&addr), &size)) fail("Cannot inspect listener address");
    s.port = ntohs(addr.sin_port);
}
Listener::~Listener() = default;
void Listener::cancel() noexcept {
    state_->cancel();
}
std::uint16_t Listener::port() const noexcept {
    return state_->port;
}
Stream Listener::accept(Deadline d) {
    auto s = std::make_unique<Stream::State>();
    sockaddr_in peer{};
    SockLen size = sizeof(peer);
    for (;;) {
        state_->check(d);
        s->socket = ::accept(state_->socket, reinterpret_cast<sockaddr*>(&peer), &size);
        if (s->socket != invalid) break;
        if (!pending(socket_error()) && !interrupted(socket_error())) fail("Accept failed");
        state_->wait(false, d);
    }
    configure(s->socket);
    char text[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &peer.sin_addr, text, sizeof(text));
    s->peer = text;
    s->identity = state_->identity;
    s->parentCancelled = &state_->cancelled;
    s->handshake(s->identity->state_->context.get(), true, d);
    s->parentCancelled = nullptr;
    return Stream(std::move(s));
}
}
