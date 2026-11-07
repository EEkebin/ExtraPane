#include <extrapane/session.hpp>
#include <utility>

namespace extrapane {
Session::~Session() { close(); }

Availability Session::add(ResourceRequest request, std::unique_ptr<Resource> resource) {
    // A supplied adapter must be inert until prepare(). Rejections do not
    // acquire hardware, and destructors remain responsible for local memory.
    if (closed_) return {false, "Session is closed"};
    if (!resource || request.id == 0 || request.device_id.empty())
        return {false, "Invalid resource request"};
    if (resources_.contains(request.id)) return {false, "Resource ID already used"};
    if (resources_.size() >= max_session_resources) return {false, "Session resource limit reached"};
    switch(request.kind) {
    case ResourceKind::display:case ResourceKind::camera:case ResourceKind::microphone:case ResourceKind::usb:break;
    default:return {false,"Unknown resource kind"};
    }
    for (const auto& [id, entry] : resources_) {
        if (!entry.resource) continue;
        if (entry.status.request.kind == request.kind &&
            (request.kind != ResourceKind::usb || entry.status.request.device_id == request.device_id))
            return {false, "Resource already active"};
    }
    const auto id = request.id;
    auto [it, inserted] = resources_.emplace(id, Entry{{std::move(request), ResourceState::preparing, {}}, std::move(resource)});
    auto& entry = it->second;
    Availability result;
    try {
        result = entry.resource->prepare();
        if (result.available) result = entry.resource->start();
    } catch (...) {
        // Do not expose adapter exception text: it may contain local secrets.
        entry.resource->stop();
        entry.resource.reset();
        entry.status.state = ResourceState::failed;
        entry.status.detail = "Resource adapter failed";
        return {false, entry.status.detail};
    }
    if (!result.available) {
        entry.resource->stop();
        entry.resource.reset();
        entry.status.state = ResourceState::failed;
        entry.status.detail = result.reason;
        return result;
    }
    entry.status.state = ResourceState::active;
    return {true, {}};
}

bool Session::stop_resource(std::uint64_t id) noexcept {
    auto it = resources_.find(id);
    if (it == resources_.end()) return false;
    if (it->second.resource) {
        it->second.status.state = ResourceState::stopping;
        it->second.resource->stop();
    }
    resources_.erase(it);
    return true;
}

bool Session::fail_resource(std::uint64_t id, std::string reason) {
    auto it = resources_.find(id);
    if (it == resources_.end() || !it->second.resource) return false;
    it->second.resource->stop();
    it->second.resource.reset();
    it->second.status.state = ResourceState::failed;
    it->second.status.detail = std::move(reason);
    return true;
}

void Session::close() noexcept {
    if (closed_) return;
    closed_ = true;
    while (!resources_.empty()) stop_resource(resources_.begin()->first);
}

std::vector<ResourceStatus> Session::snapshot() const {
    std::vector<ResourceStatus> result;
    result.reserve(resources_.size());
    for (const auto& [id, entry] : resources_) result.push_back(entry.status);
    return result;
}
}
