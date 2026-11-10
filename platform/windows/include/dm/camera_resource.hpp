#pragma once
#include <dm/camera.hpp>
#include <extrapane/session.hpp>
#include <functional>
#include <atomic>
#include <thread>

namespace dm::camera {
// Client-side source adapter. Construct inertly; prepare/start/stop/destruction
// belong to the serialized COM-initialized owner executor. The sink runs on a
// capture worker and must enqueue bounded native work, not touch UI or stop us.
class SourceResource final : public extrapane::Resource {
public:
    using Sink = std::function<void(Sample)>;
    // Cleanup executes on the capture worker before COM uninitialization. It
    // must not throw or call back into the owner/session lifecycle.
    SourceResource(std::wstring local_device, std::optional<Format> format, Sink sink,
                   std::function<void()> worker_cleanup = {});
    ~SourceResource() override;
    extrapane::Availability prepare() override;
    extrapane::Availability start() override;
    void stop() noexcept override;
    Format format() const { return format_.value(); }
    bool failed() const noexcept { return failed_.load(); }

private:
    std::wstring device_;
    std::optional<Format> format_;
    Sink sink_;
    std::function<void()> worker_cleanup_;
    std::unique_ptr<Capture> capture_;
    std::thread worker_;
    std::atomic_bool stopping_ = false, failed_ = false;
};
}
