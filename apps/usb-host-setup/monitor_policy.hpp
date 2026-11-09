#pragma once
#include <cstdint>
#include <string_view>
namespace extrapane::usb_monitor {
// Oracle-signed VBoxUSBMon7.2.2.170484 shipped by the installed usbipd-win.
// Signature, protected file ownership and service identity remain mandatory.
inline constexpr std::wstring_view legacy_7_2_2=L"ffcad560ac7714460248631aa17238f4d2ad9fd5aed87531b93dab410d23fd87";
constexpr bool reviewed(std::wstring_view digest,std::wstring_view bundled){return digest==bundled||digest==legacy_7_2_2;}
// Pinned usbipd source4431eba... VBoxUsbMon.IsVersionSupported requires ABI5.x.
constexpr bool compatible(std::uint32_t major,std::uint32_t /*minor*/,unsigned bytes){return bytes==8&&major==5;}
}
