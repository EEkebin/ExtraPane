#pragma once
#include <extrapane/session.hpp>
#include <cstdint>
#include <string>

namespace extrapane::usb {
// Facts must come from the local platform inventory, never a remote request.
struct Device {
    std::string id;
    std::uint64_t generation = 0;
    bool inventory_complete = false;
    bool removable_storage = false;
    bool composite = false;
    bool system_disk = false;
    bool boot_disk = false;
    bool paging_disk = false;
    bool owned_elsewhere = false;
};
Availability eligible(const Device& device);
enum class State { idle, preparing, shared, ejecting, recovery_required };

// Platform operations own their handles and must unwind partial failure.
// A grant is issued only after source volumes are exclusively locked/dismounted.
// Transport loss never authorizes source remount or re-export.
class Operations {
public:
    virtual ~Operations() = default;
    virtual Device inspect() = 0;
    virtual Availability claim(const Device&) = 0;
    virtual Availability lock_and_dismount(const Device&) = 0;
    virtual Availability export_admitted(const Device&) = 0;
    virtual Availability flush_and_eject_remote() = 0;
    virtual void close_transport() noexcept = 0;
    virtual void stop_export() noexcept = 0;
    virtual void release_claim() noexcept = 0;
};

class Transfer final {
public:
    explicit Transfer(Operations& operations): operations_(operations) {}
    ~Transfer();
    Transfer(const Transfer&) = delete;
    Transfer& operator=(const Transfer&) = delete;
    Availability start(const std::string& selected_id, std::uint64_t generation,
                       bool admitted);
    Availability eject();
    void transport_lost() noexcept;
    State state() const noexcept { return state_; }
private:
    void abandon() noexcept;
    Operations& operations_;
    State state_ = State::idle;
    bool claimed_ = false;
    bool dismount_attempted_ = false;
};
}
