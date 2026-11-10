#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace dm::linux_client {
// A weak directory retains no idle capture objects. Entry lifetime bridges the
// final release/new acquisition race; only that device waits for its teardown.
template <class Key, class Hub>
class SharedCaptureRegistry {
    struct Entry {
        std::mutex mutex;
        std::shared_ptr<Hub> hub;
    };
    std::mutex mutex_;
    std::map<Key, std::weak_ptr<Entry>> entries_;

public:
    class Lease {
        friend class SharedCaptureRegistry;
        std::shared_ptr<Entry> entry_;
        std::shared_ptr<Hub> hub_;
        Lease(std::shared_ptr<Entry> entry, std::shared_ptr<Hub> hub)
            : entry_(std::move(entry)), hub_(std::move(hub)) {}

    public:
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&&) = default;
        Lease& operator=(Lease&&) = delete;
        ~Lease() {
            if (!entry_) return;
            std::lock_guard lock(entry_->mutex);
            if (hub_.use_count() == 2) entry_->hub.reset();
            hub_.reset();
        }
        Hub* operator->() const { return hub_.get(); }
    };
    template <class Factory, class Validate>
    Lease acquire(const Key& key, Factory factory, Validate validate) {
        std::shared_ptr<Entry> entry;
        {
            std::lock_guard lock(mutex_);
            for (auto it = entries_.begin(); it != entries_.end();)
                if (it->second.expired())
                    it = entries_.erase(it);
                else
                    ++it;
            auto& weak = entries_[key];
            entry = weak.lock();
            if (!entry) {
                entry = std::make_shared<Entry>();
                weak = entry;
            }
        }
        std::lock_guard lock(entry->mutex);
        if (!entry->hub) {
            auto created = factory();
            validate(*created);
            entry->hub = std::move(created);
        } else
            validate(*entry->hub);
        return Lease(entry, entry->hub);
    }
};
}
