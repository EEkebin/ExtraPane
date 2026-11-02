#include <dm/probe.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void rejects(std::span<const std::uint8_t> packet) {
    try { (void)dm::probe::decode(packet); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Malformed packet was accepted");
}
}

int main() {
    try {
        using namespace dm::probe;
        const Token token{0, 1, 2, 127, 128, 253, 254, 255};
        for (const auto kind : {Kind::request, Kind::response}) {
            const auto packet = encode({kind, token});
            const auto result = decode(packet);
            require(result.kind == kind && result.token == token, "Round trip lost fields");
            for (std::size_t length = 0; length < packet.size(); ++length) rejects(std::span(packet).first(length));
            for (std::size_t index = 0; index < 8; ++index) {
                auto malformed = packet;
                malformed[index] = 255;
                rejects(malformed);
            }
            std::array<std::uint8_t, wire_size + 1> oversized{};
            std::copy(packet.begin(), packet.end(), oversized.begin());
            rejects(oversized);
        }
        bool invalid_kind_rejected = false;
        try { (void)encode({static_cast<Kind>(42), token}); }
        catch (const std::invalid_argument&) { invalid_kind_rejected = true; }
        require(invalid_kind_rejected, "Encoder accepted invalid kind");
        std::cout << "Protocol validation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
