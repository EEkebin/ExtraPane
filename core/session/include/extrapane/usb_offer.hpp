#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
namespace extrapane::usb::wire {
inline void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
// Version 4 binds the declared device class to the offer and eject receipt.
// Old storage-only helpers are rejected before either side exports a device.
inline std::array<std::uint8_t, 8> control(unsigned kind) {
    return {'E', 'P', 'U', 'S', 4, static_cast<std::uint8_t>(kind), 0, 0};
}
enum class DeviceKind : std::uint8_t { storage = 1, generic = 2 };
inline std::array<std::uint8_t, 8> greeting(unsigned message, DeviceKind kind) {
    require((message == 0 || message == 2) && (kind == DeviceKind::storage || kind == DeviceKind::generic),
            "Invalid USB negotiation");
    auto bytes = control(message);
    bytes[6] = static_cast<std::uint8_t>(kind);
    return bytes;
}
inline DeviceKind decode_greeting(const std::array<std::uint8_t, 8>& bytes, unsigned message) {
    require(bytes == greeting(message, DeviceKind::storage) || bytes == greeting(message, DeviceKind::generic),
            "Unsupported USB negotiation");
    return static_cast<DeviceKind>(bytes[6]);
}

struct Offer {
    DeviceKind kind = DeviceKind::storage;
    std::array<std::uint8_t, 16> nonce{};
    std::uint16_t vendor = 0, product = 0;
    std::string bus;
};
inline bool valid_bus(const std::string& bus) {
    const auto dash = bus.find('-');
    return !bus.empty() && bus.size() < 16 && dash > 0 && dash != std::string::npos && dash + 1 < bus.size() &&
           bus.find('-', dash + 1) == std::string::npos &&
           std::all_of(bus.begin(), bus.end(), [](char c) { return (c >= '0' && c <= '9') || c == '-'; });
}
inline std::array<std::uint8_t, 64> encode(const Offer& offer) {
    require(valid_bus(offer.bus), "Invalid selected bus");
    require(offer.kind == DeviceKind::storage || offer.kind == DeviceKind::generic, "Unsupported USB device kind");
    std::array<std::uint8_t, 64> bytes{};
    auto prefix = control(1);
    std::copy(prefix.begin(), prefix.end(), bytes.begin());
    bytes[6] = static_cast<std::uint8_t>(offer.kind);
    std::copy(offer.nonce.begin(), offer.nonce.end(), bytes.begin() + 8);
    bytes[24] = offer.vendor >> 8;
    bytes[25] = offer.vendor;
    bytes[26] = offer.product >> 8;
    bytes[27] = offer.product;
    bytes[28] = static_cast<std::uint8_t>(offer.bus.size());
    std::copy(offer.bus.begin(), offer.bus.end(), bytes.begin() + 29);
    return bytes;
}
inline Offer decode(const std::array<std::uint8_t, 64>& bytes) {
    const auto prefix = control(1);
    require(std::equal(prefix.begin(), prefix.begin() + 6, bytes.begin()) && bytes[7] == 0 &&
                (bytes[6] == 1 || bytes[6] == 2) && bytes[28] > 0 && bytes[28] < 16,
            "Unsupported USB offer");
    Offer offer;
    offer.kind = static_cast<DeviceKind>(bytes[6]);
    std::copy_n(bytes.begin() + 8, 16, offer.nonce.begin());
    offer.vendor = (bytes[24] << 8) | bytes[25];
    offer.product = (bytes[26] << 8) | bytes[27];
    offer.bus.assign(bytes.begin() + 29, bytes.begin() + 29 + bytes[28]);
    require(std::all_of(bytes.begin() + 29 + bytes[28], bytes.end(), [](auto b) { return b == 0; }),
            "USB offer reserved data");
    require(valid_bus(offer.bus), "Invalid offered USB bus");
    return offer;
}
inline std::array<std::uint8_t, 32> receipt(const Offer& offer) {
    std::array<std::uint8_t, 32> bytes{};
    auto prefix = control(4);
    std::copy(prefix.begin(), prefix.end(), bytes.begin());
    bytes[6] = static_cast<std::uint8_t>(offer.kind);
    std::copy(offer.nonce.begin(), offer.nonce.end(), bytes.begin() + 8);
    bytes[24] = offer.vendor >> 8;
    bytes[25] = offer.vendor;
    bytes[26] = offer.product >> 8;
    bytes[27] = offer.product;
    return bytes;
}
}
