#include <extrapane/server.hpp>
#include <stdexcept>
#include <utility>

namespace extrapane {
namespace {
class ClaimedResource final : public Resource {
public:
    ClaimedResource(std::shared_ptr<DeviceClaims> claims, std::string id, std::unique_ptr<Resource> resource)
        : claims_(std::move(claims)), id_(std::move(id)), resource_(std::move(resource)) {}
    ~ClaimedResource() override { stop(); }
    Availability prepare() override {
        if (leased_) return {false, "Device already prepared"};
        if (!claims_->acquire(id_)) return {false, "Device is shared with another connection"};
        leased_ = true;
        return resource_->prepare();
    }
    Availability start() override {
        if (!leased_) return {false, "Device has not been prepared"};
        return resource_->start();
    }
    void stop() noexcept override {
        if (!leased_) return;
        resource_->stop();
        claims_->release(id_);
        leased_ = false;
    }

private:
    std::shared_ptr<DeviceClaims> claims_;
    std::string id_;
    std::unique_ptr<Resource> resource_;
    bool leased_ = false;
};
}

std::unique_ptr<Resource> with_device_claim(std::shared_ptr<DeviceClaims> claims, std::string local_id,
                                            std::unique_ptr<Resource> resource) {
    if (!claims || local_id.empty() || !resource) throw std::invalid_argument("Invalid device lease");
    return std::make_unique<ClaimedResource>(std::move(claims), std::move(local_id), std::move(resource));
}

Server::Server(std::size_t capacity) : capacity_(capacity) {
    if (!capacity) throw std::invalid_argument("Server capacity must be positive");
}
Server::~Server() {
    stop();
}
Availability Server::start(std::unique_ptr<Listener> listener) {
    if (listener_) return {false, "Server is already listening"};
    if (!listener) return {false, "No listener supplied"};
    Availability result;
    try {
        result = listener->start();
    } catch (...) {
        listener->stop();
        return {false, "Listener startup failed"};
    }
    if (!result.available) {
        listener->stop();
        return result;
    }
    listener_ = std::move(listener);
    return {true, {}};
}
Availability Server::admit(std::uint64_t id) {
    if (!listener_) return {false, "Server is stopped"};
    if (!id || sessions_.contains(id)) return {false, "Invalid or duplicate session"};
    if (sessions_.size() >= capacity_) return {false, "Server capacity reached"};
    sessions_.emplace(id, std::make_unique<Session>());
    return {true, {}};
}
Availability Server::add(std::uint64_t id, ResourceRequest request, std::unique_ptr<Resource> resource) {
    const auto it = sessions_.find(id);
    if (it == sessions_.end()) return {false, "Session is not admitted"};
    return it->second->add(std::move(request), std::move(resource));
}
bool Server::stop_resource(std::uint64_t id, std::uint64_t resource) noexcept {
    const auto it = sessions_.find(id);
    return it != sessions_.end() && it->second->stop_resource(resource);
}
bool Server::fail_resource(std::uint64_t id, std::uint64_t resource, std::string reason) {
    const auto it = sessions_.find(id);
    return it != sessions_.end() && it->second->fail_resource(resource, std::move(reason));
}
bool Server::disconnect(std::uint64_t id) noexcept {
    return sessions_.erase(id) != 0;
}
void Server::stop() noexcept {
    // Reject new admission before beginning resource teardown.
    auto listener = std::move(listener_);
    if (listener) listener->stop();
    sessions_.clear();
}
std::vector<ResourceStatus> Server::snapshot(std::uint64_t id) const {
    const auto it = sessions_.find(id);
    return it == sessions_.end() ? std::vector<ResourceStatus>{} : it->second->snapshot();
}
}
