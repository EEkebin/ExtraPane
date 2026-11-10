#pragma once
#include "media.hpp"
namespace extrapane::camera_wire {
// Camera-only session preamble, inside already admitted TLS/access encryption.
// Identifiers are scoped to this connection and never authorize another stream.
struct Offer {
    unsigned width = 1280, height = 720, fps = 30;
    media::Binding binding;
};
using OfferBytes = std::array<std::uint8_t, 32>;
OfferBytes encode(const Offer& offer);
Offer decode(std::span<const std::uint8_t> bytes);
}
