#pragma once
#include <string_view>
namespace extrapane::usb_uninstall {
constexpr bool independent_service(std::wstring_view name,bool exact_owned_usb_driver) {
    return name != L"VBoxUSB" || !exact_owned_usb_driver;
}
enum class MonitorAction { preserve, remove, refuse };
constexpr MonitorAction monitor_action(bool exists, bool created, bool managed_path,
                                      bool exact_hash, bool external_consumer) {
    if (!exists) return MonitorAction::preserve;
    if (!managed_path) return created ? MonitorAction::refuse : MonitorAction::preserve;
    return created && exact_hash && !external_consumer ? MonitorAction::remove : MonitorAction::refuse;
}
enum class InfAction { preserve, remove };
// An INF in DriverStore is independent of our private payload directory. Its
// adoption by another installed USB consumer must not block app removal.
constexpr InfAction inf_action(bool created, bool external_consumer) {
    return created && !external_consumer ? InfAction::remove : InfAction::preserve;
}
constexpr bool published_inf(std::wstring_view name) {
    if (!name.starts_with(L"oem") || !name.ends_with(L".inf") || name.size() < 8) return false;
    for (auto c : name.substr(3, name.size()-7)) if (c < L'0' || c > L'9') return false;
    return true;
}
}
