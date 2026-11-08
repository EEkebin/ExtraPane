#pragma once
#include "windows_peer_media.hpp"
namespace extrapane::windows_peer {
// One private native capture listener per authorized existing-screen grant.
// Source.device is the local Windows monitor device_path (not GDI ordinal).
class ScopedScreenProvider {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    explicit ScopedScreenProvider(Factory);
    ~ScopedScreenProvider();
    std::string start(const Source&,const peers::Grant&,std::uint16_t local_port,std::chrono::steady_clock::time_point deadline);
    std::uint16_t port()const;
    bool running()const;
    void stop()noexcept;
};
class ScopedScreenReceiver {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    explicit ScopedScreenReceiver(Factory);
    ~ScopedScreenReceiver();
    void start(const peers::Grant&,std::uint16_t bridge_port,std::string credential,std::string renderer="d3d11",std::string monitor={});
    bool running()const;
    void stop()noexcept;
};
}
