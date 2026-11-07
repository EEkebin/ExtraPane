#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <wrl/client.h>
#include <mfobjects.h>

namespace dm::camera {
struct Device { std::wstring id, name; };
struct Format { unsigned width=1920,height=1080,fps=30; };
void validate_format(Format format); // Pure validation: no COM or device access.
struct Sample {
    Microsoft::WRL::ComPtr<IMFSample> native;
    std::int64_t timestamp=0; // Media Foundation 100 ns clock.
    std::uint32_t flags=0;
};
// Caller must initialize COM on the owning worker thread. Enumeration does not
// activate cameras. IDs are opaque local device paths; never accept remote IDs.
std::vector<Device> enumerate();
class Capture final {
public:
    Capture(const std::wstring& locally_selected_device,Format format);
    ~Capture();
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    Format format() const;
    // Single consuming worker. Samples remain native MF buffers; no UI IPC.
    // A timeout retains at most one outstanding read for the next call.
    Sample read(std::chrono::milliseconds timeout);
    void stop() noexcept; // May interrupt a read from another thread.
private:
    struct State;std::unique_ptr<State> state_;
};
}
