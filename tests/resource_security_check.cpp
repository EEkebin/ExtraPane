#include <dm/access.hpp>
#include <extrapane/negotiation.hpp>
#include <extrapane/media.hpp>
#include <extrapane/session_route.hpp>
#include <extrapane/camera_protocol.hpp>
#include "../core/protocol/include/dm/prefix_buffer.hpp"
#include <condition_variable>
#include <deque>
#include <thread>
#include <iostream>

using namespace extrapane;
using namespace extrapane::wire;
void check(bool ok) {
    if (!ok) throw std::runtime_error("Encrypted resource check failed");
}
struct Pipe {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::uint8_t> bytes;
    bool closed = false, tamper = false;
    void write(std::span<const std::uint8_t> input) {
        std::lock_guard lock(mutex);
        if (closed) throw std::runtime_error("Closed");
        bytes.insert(bytes.end(), input.begin(), input.end());
        if (tamper && !input.empty()) bytes.back() ^= 1;
        ready.notify_all();
    }
    void read(std::span<std::uint8_t> output) {
        std::unique_lock lock(mutex);
        for (auto& byte : output) {
            if (!ready.wait_for(lock, std::chrono::seconds(2), [&] { return closed || !bytes.empty(); }) ||
                bytes.empty())
                throw std::runtime_error("Closed or timed out");
            byte = bytes.front();
            bytes.pop_front();
        }
    }
    void close() {
        std::lock_guard lock(mutex);
        closed = true;
        ready.notify_all();
    }
};
struct Pair {
    Pipe c2s, s2c;
    dm::access::Stream client{[&](auto bytes) { s2c.read(bytes); }, [&](auto bytes) { c2s.write(bytes); }};
    dm::access::Stream server{[&](auto bytes) { c2s.read(bytes); }, [&](auto bytes) { s2c.write(bytes); }};
    bool authenticate(dm::access::Gate& gate, const std::string& code) {
        bool clientOk = false, serverOk = false;
        std::thread worker([&] {
            try {
                server.server(gate);
                serverOk = true;
            } catch (...) {
                c2s.close();
                s2c.close();
            }
        });
        try {
            client.client(code);
            clientOk = true;
        } catch (...) {
            c2s.close();
            s2c.close();
        }
        worker.join();
        return clientOk && serverOk;
    }
};
struct ListenerStub : Listener {
    Availability start() override { return {true, {}}; }
    void stop() noexcept override {}
};
struct ResourceStub : Resource {
    int& stopped;
    explicit ResourceStub(int& count) : stopped(count) {}
    Availability prepare() override { return {true, {}}; }
    Availability start() override { return {true, {}}; }
    void stop() noexcept override { ++stopped; }
};
int main() {
    try {
        dm::access::Gate gate;
        auto code = gate.generate();
        Pair pair;
        check(pair.authenticate(gate, code));
        ControlChannel client([&](auto bytes) { pair.client.read(bytes); },
                              [&](auto bytes) { pair.client.write(bytes); });
        ControlChannel host([&](auto bytes) { pair.server.read(bytes); },
                            [&](auto bytes) { pair.server.write(bytes); });
        Server server(1);
        check(server.start(std::make_unique<ListenerStub>()).available);
        check(server.admit(1).available);
        auto local = unavailable_capabilities(), peer = unavailable_capabilities();
        local[1].destination = {true, {}};
        peer[1].source = {true, {}};
        int allocated = 0, stopped = 0;
        Negotiation negotiation(server, 1, local, [&](ResourceKind) {
            ++allocated;
            return std::make_unique<ResourceStub>(stopped);
        });
        auto bytes = encode_capabilities(peer);
        client.send(
            {{Operation::capabilities, ResourceKind::display, 0, static_cast<std::uint32_t>(bytes.size())}, bytes});
        negotiation.serve_one(host);
        check(client.receive().header.operation == Operation::capabilities);
        check(allocated == 0);
        bytes = encode_request({1, ResourceKind::camera, "remote-camera"});
        client.send({{Operation::request, ResourceKind::camera, 1, static_cast<std::uint32_t>(bytes.size())}, bytes});
        negotiation.serve_one(host);
        check(client.receive().header.operation == Operation::grant);
        check(allocated == 1);
        pair.c2s.tamper = true;
        client.send({{Operation::stop, ResourceKind::camera, 1, 0}, {}});
        bool rejected = false;
        try {
            negotiation.serve_one(host);
        } catch (...) {
            rejected = true;
        }
        check(rejected && host.failed() && stopped == 1 && server.listening());
        code = gate.generate();
        code[0] = code[0] == '0' ? '1' : '0';
        Pair wrong;
        check(!wrong.authenticate(gate, code));
        check(server.client_count() == 0 && allocated == 1);
        // The unified router reads only eight admitted bytes. This must preserve
        // the remainder buffered inside the authenticated record layer and replay
        // the prefix exactly once to the existing adapter parser.
        code = gate.generate();
        Pair routePair;
        check(routePair.authenticate(gate, code));
        const auto offered = camera_wire::encode({1280, 720, 30, {9, 4}});
        routePair.client.write(offered);
        dm::protocol::PrefixBuffer lookahead;
        std::array<std::uint8_t, 8> prefix{};
        const auto admittedRead = [&](auto data) { routePair.server.read(data); };
        lookahead.peek(prefix, admittedRead);
        check(classify_session_prefix(prefix) == ResourceKind::camera);
        camera_wire::OfferBytes routed{};
        lookahead.read(routed, admittedRead);
        check(routed == offered && camera_wire::decode(routed).binding.resource == 9);
        routePair.c2s.tamper = true;
        routePair.client.write(offered);
        rejected = false;
        try {
            lookahead.peek(prefix, admittedRead);
        } catch (...) {
            rejected = true;
        }
        check(rejected); // No classifier/resource allocation sees tampered bytes.
        // Exercise media framing through real encrypted record splitting, without
        // sockets or camera hardware. This does not test live channel admission.
        code = gate.generate();
        Pair mediaPair;
        check(mediaPair.authenticate(gate, code));
        media::Channel camera(
            {5, 8}, [&](auto data) { mediaPair.client.read(data); }, [&](auto data) { mediaPair.client.write(data); });
        media::Channel destination(
            {5, 8}, [&](auto data) { mediaPair.server.read(data); }, [&](auto data) { mediaPair.server.write(data); });
        media::Frame frame{{5, 8}, 0, 123456, Bytes(200000, 91)};
        camera.send(frame);
        auto delivered = destination.receive();
        check(delivered.payload == frame.payload && delivered.timestamp == frame.timestamp);
        mediaPair.c2s.tamper = true;
        frame.sequence = 1;
        camera.send(frame);
        rejected = false;
        try {
            destination.receive();
        } catch (...) {
            rejected = true;
        }
        check(rejected && destination.failed());
        std::cout << "Encrypted resource exchange and tamper checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
