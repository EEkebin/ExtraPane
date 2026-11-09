#pragma once
#include <dm/camera.hpp>
#include <mfidl.h>
#include <span>

namespace dm::camera {
// Native receiving-side MF source. Caller initializes COM and Media Foundation.
// Registration/activation through Windows' virtual-camera API is separate.
// This owner must outlive consumers; destruction shuts down their source/stream.
class MediaSource final {
public:
    explicit MediaSource(Format format,std::wstring local_pipe={});
    ~MediaSource();
    MediaSource(const MediaSource&)=delete;
    MediaSource& operator=(const MediaSource&)=delete;
    Microsoft::WRL::ComPtr<IMFMediaSourceEx> source() const;
    // IMFActivate::DetachObject transfers shutdown responsibility to the caller.
    Microsoft::WRL::ComPtr<IMFMediaSourceEx> detach();
    // One decoded, tightly packed NV12 frame. Raw frames can replace an older
    // pending raw frame safely; encoded H.264 reference frames cannot.
    void submit(std::span<const std::uint8_t> nv12,std::int64_t timestamp);
    void stop() noexcept;
private:
    struct State;std::unique_ptr<State> state_;
};
}
