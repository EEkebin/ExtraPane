#pragma once
#include "session.hpp"
#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>

namespace extrapane {
inline constexpr std::size_t session_prefix_size=8;
// Classify only after TLS/access admission. The chosen adapter still validates
// its complete offer and capabilities before allocating any device resource.
inline ResourceKind classify_session_prefix(std::span<const std::uint8_t> bytes){
    if(bytes.size()!=session_prefix_size)throw std::invalid_argument("Incomplete session prefix");
    const std::array<std::uint8_t,7> display{'D','M','V','R',9,1,3};
    if(std::equal(display.begin(),display.end(),bytes.begin())&&bytes[7]<=1)return ResourceKind::display;
    const std::array<std::uint8_t,8> camera{'E','P','C','M',0,1,1,0};
    const std::array<std::uint8_t,8> microphone{'E','P','M','O',0,1,1,1};
    const std::array<std::uint8_t,8> usb{'E','P','U','S',1,0,0,0};
    if(std::equal(camera.begin(),camera.end(),bytes.begin()))return ResourceKind::camera;
    if(std::equal(microphone.begin(),microphone.end(),bytes.begin()))return ResourceKind::microphone;
    if(std::equal(usb.begin(),usb.end(),bytes.begin()))return ResourceKind::usb;
    throw std::invalid_argument("Unsupported resource session protocol");
}
}
