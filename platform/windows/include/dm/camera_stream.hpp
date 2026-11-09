#pragma once
#include <dm/camera_resource.hpp>
#include <dm/h264.hpp>
#include <extrapane/media.hpp>

namespace dm::camera {
struct EncodedFrame {
    std::uint64_t sequence=0;
    std::int64_t capture_timestamp=0;
    std::vector<std::uint8_t> h264;
    bool fresh_encoder=false;
};
// A client source resource for the negotiated camera channel. The network sink
// must bound its queue and must not block the camera worker on socket writes.
class EncodedSource final:public extrapane::Resource {
public:
    using Sink=std::function<void(EncodedFrame)>;
    EncodedSource(std::wstring local_device,std::optional<Format> format,Sink sink,const std::atomic_bool* mirrored=nullptr);
    ~EncodedSource()override;
    extrapane::Availability prepare()override;
    extrapane::Availability start()override;
    void stop()noexcept override;
    Format format() const { return source_.format(); }
    bool failed()const noexcept{return source_.failed();}
    void request_keyframe()noexcept{refresh_=true;}
private:
    Format format_;Sink sink_;const std::atomic_bool* mirrored_;std::unique_ptr<dm::video::H264Encoder> encoder_;
    std::uint64_t sequence_=0;std::atomic_bool refresh_=false;
    SourceResource source_;
};

// Session-owned source with a bounded handoff to the network executor. The
// executor drains next_frame(), sends through its admitted media Channel, and
// reports channel failures to the session owner. It never calls stop() itself.
class QueuedSource final:public extrapane::Resource {
public:
    QueuedSource(std::wstring local_device,std::optional<Format> format,extrapane::media::Binding binding,const std::atomic_bool* mirrored=nullptr);
    ~QueuedSource() override;
    extrapane::Availability prepare() override;
    extrapane::Availability start() override;
    void stop() noexcept override;
    Format format() const { return source_.format(); }
    bool failed() const noexcept;
    std::optional<extrapane::media::Frame> next_frame(std::chrono::milliseconds timeout);
private:
    extrapane::media::Binding binding_;
    extrapane::media::FrameQueue queue_;
    EncodedSource source_;
    std::atomic_bool stopped_=false,overflow_=false;
};
}
