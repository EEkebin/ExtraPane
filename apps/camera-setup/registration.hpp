#pragma once
#include <array>
#include <optional>
#include <stdexcept>
#include <string>

namespace extrapane::camera_setup {
inline constexpr wchar_t owner_value[] = L"ExtraPane.CameraSource/v1";
enum class Value : unsigned { server, threading, friendly, owner };
using Text = std::optional<std::wstring>;
using Snapshot = std::array<Text, 4>;
struct Store {
    virtual ~Store() = default;
    virtual Text read(Value value) = 0;
    virtual void write(Value value, const Text& text) = 0;
};
inline Snapshot snapshot(Store& store) {
    Snapshot state;
    for (unsigned i = 0; i < 4; ++i) state[i] = store.read(static_cast<Value>(i));
    return state;
}
// The caller validates ownership/path policy before entering the transaction.
// No arbitrary pre-existing registration is adopted or removed.
inline void replace(Store& store, const Snapshot& next) {
    const auto previous = snapshot(store);
    try {
        for (unsigned i = 0; i < 4; ++i) store.write(static_cast<Value>(i), next[i]);
    } catch (...) {
        bool restored = true;
        for (unsigned i = 0; i < 4; ++i) {
            try {
                store.write(static_cast<Value>(i), previous[i]);
            } catch (...) {
                restored = false;
            }
        }
        if (!restored)
            throw std::runtime_error("Camera registration failed and rollback could not restore every value");
        throw;
    }
}
inline Snapshot registration(const std::wstring& path) {
    return {path, L"Both", L"ExtraPane Camera Source", owner_value};
}
inline bool absent(const Snapshot& state) {
    for (const auto& value : state)
        if (value) return false;
    return true;
}
inline bool owner_matches(const Snapshot& state) {
    return state[3] && *state[3] == owner_value && state[1] && *state[1] == L"Both" && state[0];
}
inline bool payload_name(const std::wstring& name) {
    const std::wstring prefix = L"ExtraPane.CameraSource.";
    if (name.size() != prefix.size() + 64 + 4 || name.compare(0, prefix.size(), prefix) != 0 ||
        name.substr(name.size() - 4) != L".dll")
        return false;
    for (auto c : name.substr(prefix.size(), 64))
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return false;
    return true;
}
}
