#pragma once
#include <atomic>
#include <optional>

namespace dm::mac {
// Bounds handshake and control-only workers, independently of display slots.
class AdmissionBudget {
    std::atomic_uint used_=0;
    const unsigned capacity_;
public:
    class Lease {
        friend class AdmissionBudget;
        AdmissionBudget* owner_;
        explicit Lease(AdmissionBudget* owner):owner_(owner){}
    public:
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        Lease(Lease&& other)noexcept:owner_(other.owner_){other.owner_=nullptr;}
        ~Lease(){release();}
        void release(){if(owner_){--owner_->used_;owner_=nullptr;}}
    };
    explicit AdmissionBudget(unsigned capacity=32):capacity_(capacity){}
    std::optional<Lease> tryAcquire(){
        unsigned count=used_.load();
        while(count<capacity_)if(used_.compare_exchange_weak(count,count+1))return Lease(this);
        return std::nullopt;
    }
};
}
