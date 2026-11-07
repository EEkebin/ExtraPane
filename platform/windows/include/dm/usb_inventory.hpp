#pragma once
#include <extrapane/usb.hpp>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dm::usb {
struct Disk {
    extrapane::usb::Device facts;
    std::wstring interface_path;
    std::wstring instance_id;
    std::wstring usb_instance_id;
    unsigned long disk_number = ~0ul;
    std::string label;
    std::vector<std::wstring> volumes;
    std::vector<unsigned char> descriptor_bytes;
    std::vector<unsigned char> layout_bytes;
    std::vector<std::string> unresolved;
};
struct Inventory {
    std::vector<Disk> disks;
    std::vector<std::string> errors;
};
// Only a local backend may supply this probe. The ID comes from SetupAPI, not
// a remote request. Missing/unknown ownership keeps inventory incomplete.
using OwnershipProbe = std::function<std::optional<bool>(const std::wstring& usb_instance_id)>;
Inventory inventory(const OwnershipProbe& owned_elsewhere = {});

namespace detail {
struct Descriptor { bool usb=false; bool removable=false; std::string label; };
std::optional<Descriptor> descriptor(std::span<const unsigned char> bytes);
// True means a bootable/system/unknown partition type must be excluded.
std::optional<bool> protected_layout(std::span<const unsigned char> bytes);
}
}
