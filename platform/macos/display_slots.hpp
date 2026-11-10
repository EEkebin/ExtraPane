#pragma once
#include <array>
#include <atomic>
#include <stdexcept>

namespace dm::mac {
// CGVirtualDisplay's existing helper currently supports serial slots 0..3.
// Admission/control sockets never reserve one of these display identities.
class DisplaySlots {
    std::array<std::atomic_bool, 4> used_{};

public:
    class Lease {
        friend class DisplaySlots;
        DisplaySlots* owner_;
        unsigned index_;
        Lease(DisplaySlots* owner, unsigned index) : owner_(owner), index_(index) {}

    public:
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept : owner_(other.owner_), index_(other.index_) { other.owner_ = nullptr; }
        ~Lease() {
            if (owner_) owner_->used_[index_] = false;
        }
        unsigned index() const { return index_; }
    };
    Lease acquire() {
        for (unsigned index = 0; index < used_.size(); ++index) {
            bool expected = false;
            if (used_[index].compare_exchange_strong(expected, true)) return Lease(this, index);
        }
        throw std::runtime_error("Four Mac virtual displays are already connected");
    }
};
}
