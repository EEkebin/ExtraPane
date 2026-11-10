#pragma once
#include <dm/input_protocol.hpp>
#include <memory>
#include <string>
namespace dm::input {
// Only probes after an admitted touch. Never reads element text or values.
class TextFocusProbe final {
public:
    explicit TextFocusProbe(std::wstring monitor, bool host_keyboard = false);
    ~TextFocusProbe();
    void observe(std::span<const Event> events);
    std::uint32_t confirmed_touch() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};
}
