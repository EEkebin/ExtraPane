#include <extrapane/peer_transport.hpp>
#include <array>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace extrapane::peers::transport;
using namespace std::chrono_literals;
static auto deadline() {
    return std::chrono::steady_clock::now() + 5s;
}
static void require(bool b, const char* why) {
    if (!b) throw std::runtime_error(why);
}
template <class F>
static void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "expected failure");
}
int main() {
    try {
        for (const auto* bad : {"", "::1", "example.org", "224.0.0.1", "255.255.255.255"})
            rejects([&] { Listener l(bad, 0); });
        {
            Listener wildcard("0.0.0.0", 0);
            require(wildcard.port() != 0, "wildcard ephemeral port missing");
            rejects([&] { Listener duplicate("0.0.0.0", wildcard.port()); });
#ifndef __APPLE__
            // BSD SO_REUSEADDR permits wildcard/specific address overlap. It still
            // rejects the exact same listening binding (checked above).
            rejects([&] { Listener intercept("127.0.0.1", wildcard.port()); });
#endif
            rejects([&] { Stream::connect("0.0.0.0", wildcard.port(), "127.0.0.1", deadline()); });
            rejects([&] { Stream::connect("127.0.0.1", wildcard.port(), "0.0.0.0", deadline()); });
            auto accepted = std::async(std::launch::async, [&] { return wildcard.accept(deadline()); });
            auto client = Stream::connect("127.0.0.1", wildcard.port(), "127.0.0.1", deadline());
            auto server = accepted.get();
            require(client.binding() == server.binding(), "wildcard listener exporter mismatch");
        }
#ifndef _WIN32
        // The accepted side actively closes first, leaving its local endpoint in
        // TIME_WAIT. A fresh listener must still accept a new TLS connection.
        std::uint16_t restartedPort = 0;
        for (int attempt = 0; attempt < 3; ++attempt) {
            Listener restarting("127.0.0.1", restartedPort);
            restartedPort = restarting.port();
            auto accepted = std::async(std::launch::async, [&] { return restarting.accept(deadline()); });
            auto client = Stream::connect("127.0.0.1", restartedPort, {}, deadline());
            auto server = accepted.get();
            require(client.binding() == server.binding(), "restarted listener TLS mismatch");
            server.cancel();
            std::array<std::uint8_t, 1> byte{};
            rejects([&] { client.read(byte, deadline()); });
        }
#endif
        Listener listener("localhost", 0);
        require(listener.port() != 0, "ephemeral port missing");
        rejects([&] { Listener duplicate("127.0.0.1", listener.port()); });
#ifndef __APPLE__
        // Linux/Windows additionally reserve wildcard/specific overlap; macOS uses
        // BSD address semantics while retaining exact-binding exclusivity.
        rejects([&] { Listener intercept("0.0.0.0", listener.port()); });
#endif
        std::array<std::uint8_t, 32> previous{};
        for (int round = 0; round < 2; ++round) {
            auto accepted = std::async(std::launch::async, [&] { return listener.accept(deadline()); });
            auto client = Stream::connect("LOCALHOST", listener.port(), "127.0.0.1", deadline());
            auto server = accepted.get();
            require(client.peer_address() == "127.0.0.1" && server.peer_address() == "127.0.0.1", "wrong peer address");
            require(client.binding() == server.binding(), "exporters disagree");
            require(client.binding() != previous, "exporter reused");
            previous = client.binding();
            std::vector<std::uint8_t> payload(200000);
            for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<std::uint8_t>(i);
            auto echo = std::async(std::launch::async, [&] {
                std::vector<std::uint8_t> data(payload.size());
                server.read(data, deadline());
                server.write(data, deadline());
            });
            client.write(payload, deadline());
            std::vector<std::uint8_t> result(payload.size());
            client.read(result, deadline());
            echo.get();
            require(result == payload, "exact transfer corrupted");
            std::array<std::uint8_t, 3> small{7, 8, 9}, got{};
            require(client.read_some(got) == -1, "empty stream should not block");
            require(server.write_some(small) == 3, "small write failed");
            client.wait_io(deadline());
            require(client.read_some(got) == 3 && got == small, "nonblocking transfer failed");
            if (round == 0) {
                auto blocked = std::async(std::launch::async, [&] { rejects([&] { client.read(got, deadline()); }); });
                std::this_thread::sleep_for(20ms);
                client.cancel();
                require(blocked.wait_for(1s) == std::future_status::ready, "cancel did not wake read");
                blocked.get();
            } else {
                const auto start = std::chrono::steady_clock::now();
                rejects([&] { client.read(got, start + 40ms); });
                require(std::chrono::steady_clock::now() - start < 1s, "deadline not bounded");
            }
        }
        auto wait = std::async(std::launch::async, [&] { rejects([&] { listener.accept(deadline()); }); });
        std::this_thread::sleep_for(20ms);
        listener.cancel();
        require(wait.wait_for(1s) == std::future_status::ready, "cancel did not wake accept");
        wait.get();
        std::cout << "peer transport: TLS loopback, exporter binding, exact/nonblocking delivery, deadlines, "
                     "cancellation and address policy passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
