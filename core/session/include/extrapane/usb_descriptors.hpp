#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <stdexcept>
namespace extrapane::usb {
struct DescriptorClassification {
    bool hid = false, audio = false, composite = false;
};
inline DescriptorClassification validate_device_descriptors(std::span<const std::uint8_t> device,
                                                            const std::vector<std::vector<std::uint8_t>>& configs) {
    auto require = [](bool good, const char* why) {
        if (!good) throw std::runtime_error(why);
    };
    auto word = [](std::span<const std::uint8_t> bytes, std::size_t at) {
        return unsigned(bytes[at]) | (unsigned(bytes[at + 1]) << 8);
    };
    auto supported = [&](unsigned kind) {
        require(kind != 8, "USB storage functions require the storage sharing path");
        require(kind != 9, "USB hubs cannot be forwarded");
        require(kind != 0xe0, "Bluetooth USB adapters are not supported for forwarding");
        require(kind == 1 || kind == 3,
                "USB device contains an unsupported interface (only HID and audio are supported)");
    };
    require(device.size() == 18 && device[0] == 18 && device[1] == 1, "Invalid USB device descriptor");
    if (device[4] != 0 && device[4] != 0xef) supported(device[4]);
    if (device[4] == 0xef) require(device[5] == 2 && device[6] == 1, "Unsupported miscellaneous USB device class");
    require(device[17] > 0 && configs.size() == device[17], "USB configuration inventory is incomplete");
    DescriptorClassification result;
    std::set<unsigned> values;
    for (const auto& blob : configs) {
        std::span<const std::uint8_t> bytes(blob);
        require(bytes.size() >= 9 && bytes[0] == 9 && bytes[1] == 2 && word(bytes, 2) == bytes.size(),
                "Truncated USB configuration descriptor");
        require(bytes[4] > 0 && bytes[5] > 0 && values.insert(bytes[5]).second,
                "Invalid or duplicate USB configuration");
        struct Interface {
            bool zero = false;
            std::set<unsigned> alternates;
        };
        std::map<unsigned, Interface> interfaces;
        unsigned expectedEndpoints = 0, actualEndpoints = 0, currentClass = 0;
        bool current = false;
        std::set<unsigned> endpoints;
        std::vector<std::pair<unsigned, unsigned>> associations;
        std::map<unsigned, unsigned> endpointOwners;
        unsigned currentInterface = 0;
        auto finish = [&] {
            require(!current || expectedEndpoints == actualEndpoints, "USB interface endpoint inventory is incomplete");
        };
        for (std::size_t at = 9; at < bytes.size();) {
            require(bytes.size() - at >= 2 && bytes[at] >= 2 && bytes[at] <= bytes.size() - at,
                    "Malformed USB descriptor length");
            auto d = bytes.subspan(at, bytes[at]);
            const auto type = d[1];
            if (type == 4) {
                finish();
                require(d.size() == 9 && d[2] < bytes[4], "Invalid USB interface descriptor");
                supported(d[5]);
                auto& item = interfaces[d[2]];
                require(item.alternates.insert(d[3]).second, "Duplicate USB alternate setting");
                item.zero |= d[3] == 0;
                current = true;
                currentClass = d[5];
                currentInterface = d[2];
                expectedEndpoints = d[4];
                actualEndpoints = 0;
                endpoints.clear();
                result.hid |= d[5] == 3;
                result.audio |= d[5] == 1;
            } else if (type == 5) {
                require(current && d.size() >= 7 && (d[2] & 15) != 0 && (d[2] & 0x70) == 0 &&
                            endpoints.insert(d[2]).second,
                        "Invalid USB endpoint descriptor");
                auto [owner, inserted] = endpointOwners.emplace(d[2], currentInterface);
                require(inserted || owner->second == currentInterface, "USB endpoint is shared across interfaces");
                ++actualEndpoints;
                require(actualEndpoints <= expectedEndpoints, "Too many USB endpoint descriptors");
            } else if (type == 11) {
                require(d.size() == 8 && d[3] > 0 && unsigned(d[2]) + d[3] <= bytes[4],
                        "Invalid USB interface association");
                supported(d[4]);
                associations.emplace_back(d[2], d[3]);
            } else if (type == 0x21) {
                require(current && currentClass == 3 && d.size() >= 6, "Invalid HID class descriptor");
            } else if (type == 0x24 || type == 0x25) {
                require(current && currentClass == 1, "Unexpected audio class descriptor");
            } else if (type == 0x30 || type == 0x31) {
                require(current && actualEndpoints > 0, "Unexpected USB endpoint companion descriptor");
            } else
                require(false, "Unknown descriptor in USB configuration");
            at += d.size();
        }
        finish();
        require(interfaces.size() == bytes[4], "USB interface inventory is incomplete");
        for (const auto& [number, item] : interfaces)
            require(item.zero, "USB interface has no default alternate setting");
        std::set<unsigned> associated;
        for (const auto& [first, count] : associations)
            for (unsigned n = first; n < first + count; ++n)
                require(interfaces.contains(n) && associated.insert(n).second,
                        "USB interface associations overlap or are incomplete");
        result.composite |= interfaces.size() > 1;
    }
    require(result.hid || result.audio, "USB device has no supported functions");
    return result;
}
}
