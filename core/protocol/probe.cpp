#include <dm/probe.hpp>
#include <algorithm>
#include <stdexcept>

namespace dm::probe {
Packet encode(const Message& message) {
    if (message.kind != Kind::request && message.kind != Kind::response) {
        throw std::invalid_argument("Unknown diagnostic packet kind");
    }
    Packet packet{'D', 'M', 'N', 'P', 1, static_cast<std::uint8_t>(message.kind), 0, 0};
    std::copy(message.token.begin(), message.token.end(), packet.begin() + 8);
    return packet;
}

Message decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != wire_size) throw std::runtime_error("Invalid diagnostic packet length");
    if (bytes[0] != 'D' || bytes[1] != 'M' || bytes[2] != 'N' || bytes[3] != 'P') {
        throw std::runtime_error("Peer is not a DM network diagnostic endpoint");
    }
    if (bytes[4] != 1) throw std::runtime_error("Unsupported diagnostic protocol version");
    if (bytes[6] != 0 || bytes[7] != 0) throw std::runtime_error("Invalid diagnostic reserved fields");
    const auto kind = static_cast<Kind>(bytes[5]);
    if (kind != Kind::request && kind != Kind::response) throw std::runtime_error("Unknown diagnostic packet kind");
    Message result{kind, {}};
    std::copy(bytes.begin() + 8, bytes.end(), result.token.begin());
    return result;
}
}
