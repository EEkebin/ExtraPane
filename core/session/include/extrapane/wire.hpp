#pragma once
#include "session.hpp"
#include <array>
#include <span>

namespace extrapane::wire {
// Resource protocol envelope. This belongs INSIDE an admitted encrypted channel;
// it provides bounds and routing, not authentication or replay protection.
constexpr std::uint16_t version = 1;
constexpr std::uint32_t max_control_payload = 65536;
enum class Operation : std::uint8_t { capabilities=1, request=2, grant=3, reject=4, stop=5, status=6 };
struct Header {
    Operation operation;
    ResourceKind kind;
    std::uint64_t resource_id;
    std::uint32_t payload_bytes;
};
using HeaderBytes = std::array<std::uint8_t,24>;
HeaderBytes encode(const Header& header);
Header decode(std::span<const std::uint8_t> bytes);
}
