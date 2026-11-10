#pragma once
#include <dm/camera_endpoint.hpp>
#include <extrapane/media.hpp>
namespace dm::camera {
class Decoder {
public:
    using Sink = std::function<void(std::span<const std::uint8_t>, std::int64_t)>;
    virtual ~Decoder() = default;
    virtual void decode(std::span<const std::uint8_t> access_unit, std::int64_t timestamp, const Sink& sink) = 0;
};
std::unique_ptr<Decoder> native_camera_decoder(Format format);
// All methods, including destruction, run on one COM-initialized native session
// executor. No socket I/O or codec work belongs on the UI thread. Construct only
// after admission; lease the local camera slot before prepare().
class Receiver final : public extrapane::Resource {
public:
    using FailureReport = std::function<void(const std::string&)>;
    using DecoderFactory = std::function<std::unique_ptr<Decoder>(Format)>;
    Receiver(Format format, unsigned slot, extrapane::media::Binding binding,
             std::unique_ptr<CameraRegistration> registration = native_camera_registration(),
             DecoderFactory factory = native_camera_decoder, FailureReport report = {});
    ~Receiver() override;
    extrapane::Availability prepare() override;
    extrapane::Availability start() override;
    void stop() noexcept override;
    void receive_one(extrapane::media::Channel& channel);
    void accept(const extrapane::media::Frame& frame);
    bool failed() const noexcept { return failed_; }

private:
    extrapane::Availability fail(std::string reason);
    FailureReport report_;
    Format format_;
    extrapane::media::Binding binding_;
    CameraEndpoint endpoint_;
    DecoderFactory factory_;
    std::unique_ptr<Decoder> decoder_;
    std::optional<std::uint64_t> sequence_;
    std::optional<std::int64_t> origin_, last_capture_, last_output_;
    bool prepared_ = false, running_ = false, stopped_ = false, failed_ = false;
};
}
