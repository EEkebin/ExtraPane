#pragma once
#include <array>
#include <atomic>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
namespace extrapane {
// IDs are scoped to a resource kind and reused only after its worker exits.
class HostResources {
    std::mutex mutex_;
    std::array<std::map<unsigned,std::shared_ptr<std::atomic_bool>>,4> active_;
public:
    unsigned acquire(unsigned kind,const std::atomic_bool& quit,const std::atomic_bool& stop) {
        std::lock_guard lock(mutex_);
        if(quit.load()||stop.load())throw std::runtime_error("Server is stopping");
        if(kind>=active_.size())throw std::runtime_error("Unsupported server resource");
        auto& entries=active_[kind];unsigned id=1;
        for(const auto& entry:entries){if(entry.first!=id)break;if(id==std::numeric_limits<unsigned>::max())throw std::runtime_error("Resource identifier space exhausted");++id;}
        auto signal=kind==3?std::make_shared<std::atomic_bool>(false):nullptr;
        entries.emplace(id,std::move(signal));return id;
    }
    std::shared_ptr<std::atomic_bool> eject_signal(unsigned id){std::lock_guard lock(mutex_);return active_[3].at(id);}
    void release(unsigned kind,unsigned id){std::lock_guard lock(mutex_);active_.at(kind).erase(id);}
    void eject_all(){std::lock_guard lock(mutex_);for(auto& [id,signal]:active_[3])*signal=true;}
    bool stop_if_no_usb(std::atomic_bool& stop){std::lock_guard lock(mutex_);if(!active_[3].empty())return false;stop=true;return true;}
};
}
