#pragma once
#include "admission.hpp"
#include "resource_microphone.hpp"
#include <extrapane/session.hpp>
#include <atomic>
#include <thread>
#include <mutex>
namespace dm::mac {
class Transport;
struct ResourceOptions {extrapane::ResourceKind kind=extrapane::ResourceKind::camera;std::string sourceId;ResourceTarget target;std::string code;unsigned width=0,height=0,fps=0;bool nativeTicket=false;};
struct ResourceCallbacks {std::function<void(std::string)> status,ended;std::function<void(unsigned,unsigned,unsigned)> connected;};
class ResourceSession {
    ResourceOptions options_;ResourceCallbacks callbacks_;std::atomic_bool cancelled_=false;MicrophoneMuteGate mute_;
    std::mutex mutex_;std::shared_ptr<Transport> stream_;std::thread worker_;void run();
public:
    ResourceSession(ResourceOptions options,ResourceCallbacks callbacks);
    ~ResourceSession();
    void start();void cancel();void setMuted(bool value);
};
}
