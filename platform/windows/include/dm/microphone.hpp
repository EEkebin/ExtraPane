#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dm::microphone {
struct Device {
    std::wstring id, name;
};
// Active capture endpoints only. Enumeration does not activate a microphone.
std::vector<Device> enumerate();
struct Packet {
    std::vector<std::int16_t> samples; // 48 kHz mono, at most 100 ms.
    std::uint64_t sequence = 0, device_position = 0;
    bool discontinuity = false, muted = false;
};
class Capture final {
public:
    // Inert until start(). ID must originate in local enumerate(), never a peer.
    explicit Capture(std::wstring locally_selected_device);
    ~Capture();
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    // One generation per object. Device setup/errors are reported through read.
    // All WASAPI/COM activation, capture, stop and release stay on one worker.
    void start();
    std::optional<Packet> read(std::chrono::milliseconds timeout);
    void set_muted(bool muted);
    void stop() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};
}
