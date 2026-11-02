#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace dm::probe {
// Diagnostic protocol only: never authorizes a session or carries user data.
constexpr std::size_t wire_size = 16;
using Token = std::array<std::uint8_t, 8>;
using Packet = std::array<std::uint8_t, wire_size>;
enum class Kind : std::uint8_t { request = 1, response = 2 };
struct Message { Kind kind; Token token; };

Packet encode(const Message& message);
Message decode(std::span<const std::uint8_t> bytes);
}
