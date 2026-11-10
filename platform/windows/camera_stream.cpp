#include <dm/camera_stream.hpp>
#include <dm/camera_encode.hpp>
#include <stdexcept>
namespace dm::camera {
EncodedSource::EncodedSource(std::wstring device, std::optional<Format> format, Sink sink,
                             const std::atomic_bool* mirrored)
    : format_(format.value_or(Format{})), sink_(std::move(sink)), mirrored_(mirrored),
      source_(
          std::move(device), format,
          [this](Sample sample) {
              if (refresh_.exchange(false)) encoder_.reset();
              const bool fresh = !encoder_;
              if (!encoder_)
                  encoder_ = std::make_unique<dm::video::H264Encoder>(format_.width, format_.height, format_.fps);
              auto pixels = nv12_pixels(sample, format_);
              if (mirrored_ && mirrored_->load(std::memory_order_relaxed)) mirror_nv12(pixels, format_);
              auto output = encoder_->encode_nv12(pixels, sequence_);
              if (output.empty() || output.size() > 2 * 1024 * 1024)
                  throw std::runtime_error("Camera H264 output exceeds channel limit");
              sink_({sequence_++, sample.timestamp, std::move(output), fresh});
          },
          [this] { encoder_.reset(); }) {
    if (!sink_) throw std::invalid_argument("Camera requires an encoded-frame sink");
}
EncodedSource::~EncodedSource() {
    stop();
}
extrapane::Availability EncodedSource::prepare() {
    auto result = source_.prepare();
    if (result.available) format_ = source_.format();
    return result;
}
extrapane::Availability EncodedSource::start() {
    return source_.start();
}
void EncodedSource::stop() noexcept {
    source_.stop();
    encoder_.reset();
    sequence_ = 0;
}
QueuedSource::QueuedSource(std::wstring device, std::optional<Format> format, extrapane::media::Binding binding,
                           const std::atomic_bool* mirrored)
    : binding_(binding), queue_(binding),
      source_(
          std::move(device), format,
          [this](EncodedFrame frame) {
              if (!queue_.push({binding_, frame.sequence, frame.capture_timestamp, std::move(frame.h264)})) {
                  if (!stopped_) overflow_ = true;
                  throw std::runtime_error("Camera media queue closed");
              }
          },
          mirrored) {}
QueuedSource::~QueuedSource() {
    stop();
}
extrapane::Availability QueuedSource::prepare() {
    if (stopped_ || queue_.closed()) return {false, "Camera resource requires a new generation after stopping"};
    return source_.prepare();
}
extrapane::Availability QueuedSource::start() {
    if (stopped_ || queue_.closed()) return {false, "Camera resource is closed"};
    return source_.start();
}
void QueuedSource::stop() noexcept {
    stopped_ = true;
    queue_.close();
    source_.stop();
}
bool QueuedSource::failed() const noexcept {
    return overflow_ || source_.failed();
}
std::optional<extrapane::media::Frame> QueuedSource::next_frame(std::chrono::milliseconds timeout) {
    if (source_.failed()) queue_.close();
    return queue_.pop_for(timeout);
}
}
