#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace dm::video::wire {
constexpr std::uint32_t max_jpeg = 2 * 1024 * 1024;
constexpr std::array<std::uint8_t, 32> request{'D','M','V','R',7,1,3}; // byte 7: explicit input request
constexpr std::array<std::uint8_t, 4> ready{'R','D','Y','7'};
struct ClientDisplay { std::uint32_t width=0,height=0,hz=0; };
void set_client_display(std::span<std::uint8_t> request, ClientDisplay display);
ClientDisplay take_client_display(std::span<std::uint8_t> request);
enum class Codec : std::uint8_t { jpeg=1, h264=2 };
struct Offer { std::uint32_t width, height, fps, frames; Codec codec=Codec::jpeg; std::uint8_t input_caps=0; std::uint64_t input_epoch=0; };
struct FrameHeader { std::uint32_t sequence, bytes; std::uint64_t micros; };
using OfferBytes = std::array<std::uint8_t, 32>;
using HeaderBytes = std::array<std::uint8_t, 24>;
OfferBytes encode_offer(const Offer& offer);
Offer decode_offer(std::span<const std::uint8_t> bytes);
HeaderBytes encode_header(const FrameHeader& header);
FrameHeader decode_header(std::span<const std::uint8_t> bytes);
}
