#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace extrapane {
enum class ResourceKind : std::uint8_t { display = 0, camera = 1, microphone = 2, usb = 3, speaker = 4 };
inline constexpr std::size_t resource_kind_count = 5;
inline constexpr std::size_t max_session_resources = 16;
enum class ResourceState { preparing, active, muted, interrupted, stopping, failed };
struct Availability {
    bool available = false;
    std::string reason;
};
struct DeviceDescription {
    std::string local_id;
    std::string name;
    ResourceKind kind;
    Availability availability;
};
struct ResourceRequest {
    std::uint64_t id = 0;
    ResourceKind kind = ResourceKind::display;
    // Opaque adapter-owned identifier. Never interpret this as a path or command.
    std::string device_id;
};
struct ResourceStatus {
    ResourceRequest request;
    ResourceState state;
    std::string detail;
};

// All methods run on the owning session executor, never the UI/audio callback.
// Implementations own only this resource's devices/keys/contacts. stop() must
// be idempotent and release partial preparation as well as active resources.
class Resource {
public:
    virtual ~Resource() = default;
    virtual Availability prepare() = 0;
    virtual Availability start() = 0;
    virtual void stop() noexcept = 0;
};

// Local lifecycle coordinator, NOT an authentication boundary. The transport
// must admit the peer and negotiate the client's explicit selections before
// creating a Session or supplying a Resource. No wire format is defined here.
class Session final {
public:
    Session() = default;
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Availability add(ResourceRequest request, std::unique_ptr<Resource> resource);
    bool stop_resource(std::uint64_t id) noexcept;
    bool fail_resource(std::uint64_t id, std::string reason);
    void close() noexcept;
    bool closed() const noexcept { return closed_; }
    std::vector<ResourceStatus> snapshot() const;

private:
    struct Entry {
        ResourceStatus status;
        std::unique_ptr<Resource> resource;
    };
    std::map<std::uint64_t, Entry> resources_;
    bool closed_ = false;
};
}
