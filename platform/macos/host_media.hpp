#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <memory>
#include <vector>
#include <functional>
namespace dm::mac {
class API_AVAILABLE(macos(13.0)) HostMedia {
    struct State;
    std::unique_ptr<State> state_;

public:
    HostMedia(CGDirectDisplayID display, unsigned width, unsigned height, unsigned fps);
    ~HostMedia();
    std::vector<uint8_t> frame(uint64_t micros, const std::function<bool()>& cancelled);
};
}
