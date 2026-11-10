#pragma once
#include <cstdint>
namespace dm::input {
inline std::uint32_t touch_token(std::uint64_t micros) {
    return static_cast<std::uint32_t>(micros) | 1u;
}
struct FocusRect {
    long left = 0, top = 0, right = 0, bottom = 0;
};
inline bool contains(FocusRect r, long x, long y) {
    return r.right > r.left && r.bottom > r.top && x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}
inline bool editable_at_touch(FocusRect monitor, FocusRect element, long x, long y, bool enabled, bool focused,
                              bool editable) {
    return enabled && focused && editable && contains(monitor, x, y) && contains(element, x, y);
}
class TouchKeyboardGate {
public:
    void touch(std::uint32_t token, std::uint64_t now) {
        token_ = token;
        time_ = now;
        handled_ = false;
    }
    void clear() {
        token_ = 0;
        handled_ = false;
    }
    bool accept(std::uint32_t token, std::uint64_t now) {
        if (!token || token != token_ || handled_ || now < time_ || now - time_ > 2500) return false;
        handled_ = true;
        return true;
    }

private:
    std::uint32_t token_ = 0;
    std::uint64_t time_ = 0;
    bool handled_ = false;
};
}
