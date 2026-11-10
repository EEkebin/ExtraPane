#pragma once
#include <string>
#include <string_view>
#include <stdexcept>

namespace dm::microphone {
inline void validate_endpoint_slot(unsigned slot) {
    if (slot < 1) throw std::invalid_argument("Microphone endpoint identity must be positive");
}
inline std::wstring endpoint_arguments(unsigned slot) {
    validate_endpoint_slot(slot);
    return L" --run --slot " + std::to_wstring(slot);
}
inline std::wstring endpoint_server_lease(unsigned slot) {
    validate_endpoint_slot(slot);
    return L"Global\\ExtraPane.MicrophoneServer.Slot." + std::to_wstring(slot);
}
inline bool endpoint_ready(unsigned slot, std::string_view line) {
    validate_endpoint_slot(slot);
    return line == "MICROPHONE_READY slot=" + std::to_string(slot);
}
}
