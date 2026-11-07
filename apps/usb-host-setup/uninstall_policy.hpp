#pragma once
#include <string_view>
namespace extrapane::usb_uninstall {
enum class MonitorAction { preserve, remove, refuse };
constexpr MonitorAction monitor_action(bool exists, bool created, bool managed_path,
                                      bool exact_hash, bool external_consumer) {
    if (!exists) return MonitorAction::preserve;
    if (!managed_path) return created ? MonitorAction::refuse : MonitorAction::preserve;
    return created && exact_hash && !external_consumer ? MonitorAction::remove : MonitorAction::refuse;
}
constexpr bool published_inf(std::wstring_view name) {
    if (!name.starts_with(L"oem") || !name.ends_with(L".inf") || name.size() < 8) return false;
    for (auto c : name.substr(3, name.size()-7)) if (c < L'0' || c > L'9') return false;
    return true;
}
}
