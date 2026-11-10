#pragma once
#include "session.hpp"
#include <set>

namespace extrapane {
// A process-wide registry on the device that owns the physical hardware.
// Local IDs from unrelated remote peers must never be inserted here.
class DeviceClaims final {
public:
    bool acquire(const std::string& local_id) { return claims_.insert(local_id).second; }
    void release(const std::string& local_id) noexcept { claims_.erase(local_id); }

private:
    std::set<std::string> claims_;
};

// Binds a resource's lifetime to an exclusive local-device lease. Both the
// registry and resources must be used on the same serialized executor.
std::unique_ptr<Resource> with_device_claim(std::shared_ptr<DeviceClaims> claims, std::string local_id,
                                            std::unique_ptr<Resource> resource);

class Listener {
public:
    virtual ~Listener() = default;
    virtual Availability start() = 0;
    virtual void stop() noexcept = 0;
};

// Admission/cryptographic validation remains the transport's responsibility.
// No worker threads or OS devices are started by constructing this manager.
class Server final {
public:
    explicit Server(std::size_t capacity);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Availability start(std::unique_ptr<Listener> listener);
    Availability admit(std::uint64_t admitted_session_id);
    Availability add(std::uint64_t session_id, ResourceRequest request, std::unique_ptr<Resource> resource);
    bool stop_resource(std::uint64_t session_id, std::uint64_t resource_id) noexcept;
    bool fail_resource(std::uint64_t session_id, std::uint64_t resource_id, std::string reason);
    bool disconnect(std::uint64_t session_id) noexcept;
    void stop() noexcept;
    bool listening() const noexcept { return listener_ != nullptr; }
    std::size_t client_count() const noexcept { return sessions_.size(); }
    bool has_session(std::uint64_t id) const noexcept { return sessions_.contains(id); }
    std::vector<ResourceStatus> snapshot(std::uint64_t session_id) const;

private:
    std::size_t capacity_;
    std::unique_ptr<Listener> listener_;
    std::map<std::uint64_t, std::unique_ptr<Session>> sessions_;
};
}
