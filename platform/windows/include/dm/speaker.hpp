#pragma once
#include <extrapane/speaker_protocol.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
namespace dm::speaker {
struct Device {std::wstring id,name;};
std::vector<Device> enumerate();
// Selected WASAPI shared-mode output; never changes the Windows default device.
class Playback final {
public:
    Playback(std::wstring device,std::atomic_bool& mute,std::atomic_uint& volume,std::atomic_uint64_t& muteEpoch);
    ~Playback();
    Playback(const Playback&)=delete;
    void push(const extrapane::speaker_wire::Frame&,std::uint64_t receivedEpoch);
    void stop()noexcept;
private: struct State;std::unique_ptr<State> state_;
};
}
