#pragma once
#include <extrapane/usb_descriptors.hpp>
#include <string>
#include <vector>
namespace dm::usb {
struct DeviceDescriptors { std::vector<std::uint8_t> device; std::vector<std::vector<std::uint8_t>> configurations; extrapane::usb::DescriptorClassification classification; };
// Reads descriptor bytes from the hub connection whose driver key matches this
// exact physical USB PnP node. No binding, capture, reset or configuration change.
DeviceDescriptors read_device_descriptors(const std::wstring& instance,unsigned connection_hint=0);
}
