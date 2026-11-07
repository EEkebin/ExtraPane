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
class SourceResource final:public extrapane::Resource {
public:
    using Sink=std::function<void(Sample)>;
    SourceResource(std::wstring local_device,Format format,Sink sink);
    ~SourceResource() override;
    extrapane::Availability prepare() override;
    extrapane::Availability start() override;
    void stop() noexcept override;
    bool failed() const noexcept{return failed_.load();}
private:
    std::wstring device_;Format format_;Sink sink_;
    std::unique_ptr<Capture> capture_;std::thread worker_;
    std::atomic_bool stopping_=false,failed_=false;
};
}
