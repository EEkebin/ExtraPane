#pragma once
#include "media.hpp"
#include <atomic>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace extrapane::camera {
// One encoded source; each recipient has an independent, bounded sequence.
// A late join cannot start in the middle of an H.264 reference chain.
class Fanout final {
public:
    struct Recipient {
        explicit Recipient(media::Binding b) : binding(b), frames(b) {}
        media::Binding binding;
        media::FrameQueue frames;
        std::atomic_bool admitted = false, failed = false;
        bool waiting_keyframe = true; // protected by Fanout mutex
    };
    std::shared_ptr<Recipient> add(const std::string& id, media::Binding binding) {
        std::lock_guard lock(mutex_);
        if (id.empty() || id.size() > 64 || recipients_.contains(id))
            throw std::invalid_argument("Camera recipient identity is invalid or duplicate");
        auto item = std::make_shared<Recipient>(binding);
        recipients_.emplace(id, item);
        return item;
    }
    void remove(const std::string& id) {
        std::lock_guard lock(mutex_);
        auto it = recipients_.find(id);
        if (it != recipients_.end()) {
            it->second->frames.close();
            recipients_.erase(it);
        }
    }
    void publish(std::uint64_t sequence, std::int64_t timestamp, const wire::Bytes& payload, bool fresh) {
        if (payload.empty() || payload.size() > media::max_frame_bytes)
            throw std::invalid_argument("Invalid camera fanout payload");
        std::lock_guard lock(mutex_);
        for (auto& [id, item] : recipients_) {
            if (!item->admitted.load() || item->failed.load()) continue;
            if (item->waiting_keyframe) {
                if (!fresh) continue;
                item->waiting_keyframe = false;
            }
            if (!item->frames.push({item->binding, sequence, timestamp, payload})) item->failed = true;
        }
    }

private:
    std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Recipient>> recipients_;
};
}
