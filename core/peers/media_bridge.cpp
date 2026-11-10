#include <extrapane/media_bridge.hpp>
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace extrapane::peers {
namespace {
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid = INVALID_SOCKET;
void close_socket(Socket s) {
    closesocket(s);
}
bool retry() {
    auto e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
void startup() {
    static const bool ready = [] {
        WSADATA d{};
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    if (!ready) throw std::runtime_error("Socket initialization failed");
}
void nonblock(Socket s) {
    u_long n = 1;
    if (ioctlsocket(s, FIONBIO, &n)) throw std::runtime_error("Nonblocking socket failed");
}
#else
using Socket = int;
constexpr Socket invalid = -1;
void close_socket(Socket s) {
    ::close(s);
}
bool retry() {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR;
}
void startup() {}
void nonblock(Socket s) {
    if (fcntl(s, F_SETFL, O_NONBLOCK) < 0 || fcntl(s, F_SETFD, FD_CLOEXEC) < 0)
        throw std::runtime_error("Nonblocking socket failed");
#ifdef SO_NOSIGPIPE
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
}
#endif
void need(bool yes) {
    if (!yes) throw std::runtime_error("Invalid local media channel or revoked grant");
}
struct Sock {
    Socket s = invalid;
    Sock() = default;
    explicit Sock(Socket n) : s(n) {}
    ~Sock() {
        if (s != invalid) close_socket(s);
    }
    Sock(Sock&& o) noexcept : s(std::exchange(o.s, invalid)) {}
    Sock& operator=(Sock&& o) noexcept {
        if (s != invalid) close_socket(s);
        s = std::exchange(o.s, invalid);
        return *this;
    }
    Sock(const Sock&) = delete;
};
Sock make_socket() {
    startup();
    Sock s(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    need(s.s != invalid);
    nonblock(s.s);
    return s;
}
sockaddr_in address(std::uint16_t port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    return a;
}
int write_socket(Socket s, std::span<const std::uint8_t> bytes) {
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    return static_cast<int>(
        ::send(s, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), flags));
}
using Clock = std::chrono::steady_clock;
int connected_ready(Socket socket) {
#ifdef _WIN32
    WSAPOLLFD p{socket, POLLWRNORM, 0};
    int result = WSAPoll(&p, 1, 0);
#else
    pollfd p{socket, POLLOUT, 0};
    int result = ::poll(&p, 1, 0);
#endif
    if (result < 0) return retry() ? 0 : -1;
    if (!result) return 0;
    return p.revents & (POLLERR | POLLHUP | POLLNVAL) ? -1 : 1;
}
}
struct MediaBridge::State {
    struct Endpoint {
        Sock listener;
        std::uint16_t port = 0;
        bool paused = false;
    };
    struct Channel {
        Sock socket;
        std::uint64_t grant;
        bool connecting = false, local_eof = false, remote_eof = false, write_shutdown = false;
        Bytes outgoing;
        Clock::time_point deadline = Clock::now() + std::chrono::seconds(10);
    };
    rpc::Multiplexer::Authorize authorized;
    Emit emit;
    rpc::Multiplexer mux;
    std::map<std::uint64_t, Endpoint> endpoints;
    std::map<std::uint64_t, Channel> channels;
    std::deque<std::uint64_t> closed;
    std::uint64_t cursor = 0;
    State(bool initiator, rpc::Multiplexer::Authorize check, Emit send)
        : authorized(std::move(check)), emit(std::move(send)), mux(initiator, authorized) {
        need(bool(emit));
    }
    void eof(std::uint64_t id, Channel& c) {
        if (!c.local_eof) {
            emit({rpc::Operation::stream_close, id, {}});
            c.local_eof = true;
            c.deadline = Clock::now() + std::chrono::seconds(30);
        }
    }
    void erase(std::uint64_t id) {
        mux.close(id);
        channels.erase(id);
        closed.push_back(id);
        if (closed.size() > 512) closed.pop_front();
    }
};
MediaBridge::MediaBridge(bool initiator, rpc::Multiplexer::Authorize check, Emit emit)
    : state_(std::make_unique<State>(initiator, std::move(check), std::move(emit))) {}
MediaBridge::~MediaBridge() = default;
std::uint16_t MediaBridge::listen(std::uint64_t grant, bool paused) {
    auto& s = *state_;
    need(grant && s.authorized(grant) && !s.endpoints.contains(grant) && s.endpoints.size() < 256);
    auto socket = make_socket();
    auto a = address(0);
    need(!::bind(socket.s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) && !::listen(socket.s, 16));
#ifdef _WIN32
    int length = sizeof(a);
#else
    socklen_t length = sizeof(a);
#endif
    need(!getsockname(socket.s, reinterpret_cast<sockaddr*>(&a), &length));
    auto port = ntohs(a.sin_port);
    s.endpoints.emplace(grant, State::Endpoint{std::move(socket), port, paused});
    return port;
}
void MediaBridge::connect(std::uint64_t grant, std::uint16_t port, bool paused) {
    auto& s = *state_;
    need(grant && port && s.authorized(grant) && !s.endpoints.contains(grant) && s.endpoints.size() < 256);
    s.endpoints.emplace(grant, State::Endpoint{Sock{}, port, paused});
}
void MediaBridge::resume(std::uint64_t grant) {
    auto& s = *state_;
    auto found = s.endpoints.find(grant);
    need(found != s.endpoints.end() && s.authorized(grant));
    found->second.paused = false;
}
void MediaBridge::receive(const rpc::Message& m) {
    auto& s = *state_;
    // Already queued peer bytes/credits may arrive after local revocation/EOF.
    // Bound tombstones; never reinterpret a closed stream as a new grant.
    if (m.operation != rpc::Operation::stream_open &&
        std::find(s.closed.begin(), s.closed.end(), m.id) != s.closed.end())
        return;
    if (m.operation == rpc::Operation::stream_open) {
        auto grant = rpc::integer(m.payload);
        auto ep = s.endpoints.find(grant);
        need(ep != s.endpoints.end() && ep->second.listener.s == invalid && s.authorized(grant));
        s.mux.opened(m);
        try {
            auto socket = make_socket();
            auto a = address(ep->second.port);
            const int result = ::connect(socket.s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
            need(result == 0 || retry());
            s.channels.emplace(m.id, State::Channel{std::move(socket), grant, result != 0});
        } catch (...) {
            s.mux.close(m.id);
            throw;
        }
        return;
    }
    auto found = s.channels.find(m.id);
    need(found != s.channels.end() && s.authorized(found->second.grant));
    auto& c = found->second;
    if (m.operation == rpc::Operation::stream_data) {
        need(!c.remote_eof);
        s.mux.receive(m);
    } else if (m.operation == rpc::Operation::stream_credit)
        s.mux.credited(m);
    else if (m.operation == rpc::Operation::stream_close) {
        need(m.payload.empty() && !c.remote_eof);
        c.remote_eof = true;
        c.deadline = Clock::now() + std::chrono::seconds(30);
    } else
        need(false);
}
bool MediaBridge::pump() {
    auto& s = *state_;
    bool progress = false;
    std::vector<std::uint64_t> revoked;
    std::size_t accepts = 0;
    for (auto& [grant, ep] : s.endpoints) {
        if (!s.authorized(grant)) {
            revoked.push_back(grant);
            continue;
        }
        if (ep.paused || ep.listener.s == invalid || s.channels.size() >= rpc::Multiplexer::max_streams ||
            accepts >= 16)
            continue;
        Sock accepted(::accept(ep.listener.s, nullptr, nullptr));
        if (accepted.s == invalid) {
            need(retry());
            continue;
        }
        nonblock(accepted.s);
        auto msg = s.mux.open(grant);
        s.channels.emplace(msg.id, State::Channel{std::move(accepted), grant});
        s.emit(msg);
        ++accepts;
        progress = true;
    }
    for (auto grant : revoked) {
        revoke(grant);
        progress = true;
    }
    std::vector<std::uint64_t> ended, order;
    for (auto i = s.channels.upper_bound(s.cursor); i != s.channels.end(); ++i) order.push_back(i->first);
    for (auto i = s.channels.begin(); i != s.channels.upper_bound(s.cursor); ++i) order.push_back(i->first);
    if (order.size() > 64) order.resize(64);
    std::size_t budget = 256 * 1024;
    for (auto id : order) {
        s.cursor = id;
        auto& c = s.channels.at(id);
        if (c.connecting) {
            int result = connected_ready(c.socket.s);
            if (result < 0) {
                s.eof(id, c);
                ended.push_back(id);
                continue;
            }
            if (!result) {
                if (Clock::now() < c.deadline) continue;
                s.eof(id, c);
                ended.push_back(id);
                continue;
            }
            int error = 0;
#ifdef _WIN32
            int length = sizeof(error);
#else
            socklen_t length = sizeof(error);
#endif
            if (getsockopt(c.socket.s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) || error) {
                s.eof(id, c);
                ended.push_back(id);
                continue;
            }
            c.connecting = false;
            progress = true;
        }
        auto pending = s.mux.pending(id);
        if (!pending.empty()) {
            int n = write_socket(c.socket.s, pending);
            if (n > 0) {
                s.emit(s.mux.consumed(id, static_cast<std::size_t>(n)));
                progress = true;
            } else if (!n || !retry()) {
                s.eof(id, c);
                ended.push_back(id);
                continue;
            }
        }
        if (c.remote_eof && !c.write_shutdown && s.mux.pending(id).empty()) {
#ifdef _WIN32
            shutdown(c.socket.s, SD_SEND);
#else
            shutdown(c.socket.s, SHUT_WR);
#endif
            c.write_shutdown = true;
            progress = true;
        }
        if (!c.outgoing.empty() && budget >= rpc::data_chunk) {
            auto msg = s.mux.send(id, c.outgoing);
            if (msg) {
                budget -= msg->payload.size();
                s.cursor = id;
                c.outgoing.erase(c.outgoing.begin(), c.outgoing.begin() + msg->payload.size());
                s.emit(*msg);
                progress = true;
            }
        }
        if (!c.local_eof && c.outgoing.empty() && budget >= rpc::data_chunk) {
            std::array<std::uint8_t, rpc::data_chunk> buffer{};
            int n = static_cast<int>(
                recv(c.socket.s, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0));
            if (n > 0) {
                c.outgoing.assign(buffer.begin(), buffer.begin() + n);
                auto msg = s.mux.send(id, c.outgoing);
                if (msg) {
                    budget -= msg->payload.size();
                    s.cursor = id;
                    c.outgoing.erase(c.outgoing.begin(), c.outgoing.begin() + msg->payload.size());
                    s.emit(*msg);
                }
                progress = true;
            } else if (!n) {
                s.eof(id, c);
                progress = true;
            } else if (!retry()) {
                s.eof(id, c);
                ended.push_back(id);
                continue;
            }
        }
        if ((c.local_eof && c.remote_eof && c.outgoing.empty() && s.mux.pending(id).empty()) ||
            ((c.local_eof || c.remote_eof) && Clock::now() >= c.deadline))
            ended.push_back(id);
    }
    for (auto id : ended) {
        s.erase(id);
        progress = true;
    }
    return progress;
}
void MediaBridge::revoke(std::uint64_t grant) {
    auto& s = *state_;
    s.endpoints.erase(grant);
    for (auto id : s.mux.revoke(grant)) {
        auto found = s.channels.find(id);
        if (found != s.channels.end()) {
            s.eof(id, found->second);
            s.erase(id);
        }
    }
}
std::size_t MediaBridge::streams() const noexcept {
    return state_->channels.size();
}
}
