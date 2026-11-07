#pragma once
#include <dm/camera.hpp>
#include <functional>
#include <span>

namespace dm::camera {
// Local Frame Server bridge, never a network listener. Each admitted camera
// owns a fresh random pipe. Producer construction starts no thread or device.
class FramePublisher final {
public:
    explicit FramePublisher(Format format);
    ~FramePublisher();
    FramePublisher(const FramePublisher&)=delete;
    FramePublisher& operator=(const FramePublisher&)=delete;
    const std::wstring& name() const;
    void start();
    void submit(std::span<const std::uint8_t> nv12,std::int64_t timestamp);
    void stop() noexcept;
    std::string last_error() const;
private:
    struct State;std::unique_ptr<State> state_;
};
class FrameSubscriber final {
public:
    using Sink=std::function<void(std::span<const std::uint8_t>,std::int64_t)>;
    using Failure=std::function<void()>;
    // Name comes from local virtual-camera attributes, never the remote peer.
    FrameSubscriber(std::wstring pipe,Format format,Sink sink,Failure failure={});
    ~FrameSubscriber();
    FrameSubscriber(const FrameSubscriber&)=delete;
    FrameSubscriber& operator=(const FrameSubscriber&)=delete;
    void start();
    void stop() noexcept;
    bool failed() const noexcept;
    std::string last_error() const;
private:
    struct State;std::unique_ptr<State> state_;
};
}
