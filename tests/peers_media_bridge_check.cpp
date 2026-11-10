#include <extrapane/media_bridge.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
constexpr Socket bad = INVALID_SOCKET;
void close_socket(Socket s) {
    closesocket(s);
}
void nonblock(Socket s) {
    u_long v = 1;
    ioctlsocket(s, FIONBIO, &v);
}
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
using Socket = int;
constexpr Socket bad = -1;
void close_socket(Socket s) {
    close(s);
}
void nonblock(Socket s) {
    fcntl(s, F_SETFL, O_NONBLOCK);
}
#endif
using namespace extrapane::peers;
void check(bool v) {
    if (!v) throw std::runtime_error("Media bridge check failed");
}
template <class F>
void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (...) {
        rejected = true;
    }
    check(rejected);
}
struct Sock {
    Socket s = bad;
    ~Sock() {
        if (s != bad) close_socket(s);
    }
};
sockaddr_in address(std::uint16_t p) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(p);
    return a;
}
int main() {
    try {
#ifdef _WIN32
        WSADATA w{};
        check(!WSAStartup(MAKEWORD(2, 2), &w));
#endif
        Sock destination{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
        auto dest = address(0);
        check(!bind(destination.s, reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) && !listen(destination.s, 4));
#ifdef _WIN32
        int len = sizeof(dest);
#else
        socklen_t len = sizeof(dest);
#endif
        check(!getsockname(destination.s, reinterpret_cast<sockaddr*>(&dest), &len));
        nonblock(destination.s);
        bool allowed = true;
        auto auth = [&](std::uint64_t id) { return allowed && id == 7; };
        std::deque<rpc::Message> to_a, to_b;
        MediaBridge a(true, auth, [&](const auto& m) { to_b.push_back(m); });
        MediaBridge b(false, auth, [&](const auto& m) { to_a.push_back(m); });
        auto port = a.listen(7);
        b.connect(7, ntohs(dest.sin_port));
        rejects([&] { a.listen(8); });
        rejects([&] { a.connect(7, 1234); });
        rejects([&] { a.receive({rpc::Operation::stream_open, 2, rpc::integer(7)}); });
        auto pump = [&] {
            a.pump();
            b.pump();
            while (!to_a.empty()) {
                auto m = std::move(to_a.front());
                to_a.pop_front();
                a.receive(m);
            }
            while (!to_b.empty()) {
                auto m = std::move(to_b.front());
                to_b.pop_front();
                b.receive(m);
            }
        };
        Sock source{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
        auto target = address(port);
        check(!::connect(source.s, reinterpret_cast<sockaddr*>(&target), sizeof(target)));
        nonblock(source.s);
        Sock received;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (received.s == bad) {
            check(std::chrono::steady_clock::now() < deadline);
            pump();
            received.s = accept(destination.s, nullptr, nullptr);
        }
        nonblock(received.s);
        check(a.streams() == 1 && b.streams() == 1);
        // Cross the 64 KiB flow window in both directions, preserving every byte.
        Bytes payload(300000);
        for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<std::uint8_t>(i * 37);
        auto transfer = [&](Socket from, Socket to) {
            std::size_t sent = 0;
            Bytes output;
            std::array<std::uint8_t, 3001> buffer{};
            while (output.size() < payload.size()) {
                check(std::chrono::steady_clock::now() < deadline);
                if (sent < payload.size()) {
                    int n = send(from, reinterpret_cast<const char*>(payload.data() + sent),
                                 static_cast<int>(payload.size() - sent), 0);
                    if (n > 0) sent += n;
                }
                pump();
                int n = recv(to, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
                if (n > 0) output.insert(output.end(), buffer.begin(), buffer.begin() + n);
            }
            check(output == payload);
        };
        transfer(source.s, received.s);
        transfer(received.s, source.s);
        // One grant admits multiple native child sockets, without opening another grant.
        Sock source2{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
        check(!::connect(source2.s, reinterpret_cast<sockaddr*>(&target), sizeof(target)));
        Sock received2;
        while (received2.s == bad) {
            check(std::chrono::steady_clock::now() < deadline);
            pump();
            received2.s = accept(destination.s, nullptr, nullptr);
        }
        check(a.streams() == 2 && b.streams() == 2);
// Half-close propagates only after queued bytes, while reverse direction lives.
#ifdef _WIN32
        shutdown(source.s, SD_SEND);
#else
        shutdown(source.s, SHUT_WR);
#endif
        bool eof = false;
        while (!eof) {
            check(std::chrono::steady_clock::now() < deadline);
            pump();
            char c;
            int n = recv(received.s, &c, 1, 0);
            eof = n == 0;
        }
        transfer(received.s, source.s);
        allowed = false;
        a.revoke(7);
        b.revoke(7);
        check(!a.streams() && !b.streams());
        pump();
        rejects([&] { b.receive({rpc::Operation::stream_open, 99, rpc::integer(7)}); });
        std::cout << "Loopback media bridge checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
