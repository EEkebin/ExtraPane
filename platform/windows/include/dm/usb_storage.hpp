#pragma once
#include <dm/usb_inventory.hpp>
#include <memory>

namespace dm::usb {
// A local privileged backend must implement this contract. None of these IDs
// may come directly from a remote peer. A claim reserves ownership only; it must
// not bind/restart/detach the USB device before all source volumes are locked.
class StorageBackend {
public:
    virtual ~StorageBackend() = default;
    virtual extrapane::Availability claim(const Disk&) = 0;
    virtual extrapane::Availability export_locked(const Disk&) = 0;
    // A busy remote volume must return false without stopping the export.
    virtual extrapane::Availability flush_and_eject_remote() = 0;
    virtual void close_transport() noexcept = 0;
    // Stop transport/export without automatically rebinding or remounting source.
    virtual void stop_export() noexcept = 0;
    virtual void release_claim(bool recovery_required) noexcept = 0;
};

// Native handle boundary also permits hardware-free ordering/failure tests.
class StorageHandles {
public:
    virtual ~StorageHandles() = default;
    virtual extrapane::Availability open(const Disk&) = 0;
    virtual extrapane::Availability validate(const Disk&) = 0;
    virtual extrapane::Availability verify_locked(const Disk&) = 0;
    virtual extrapane::Availability lock_all() = 0;
    virtual extrapane::Availability dismount_all() = 0;
    // Trusted backend calls only after durable offline quarantine is verified.
    // Transfers removal supervision to the exact-device USB backend before its
    // expected driver re-enumeration; it never brings the source disk online.
    virtual extrapane::Availability handoff_quarantined(const Disk&) = 0;
    virtual bool invalidated() const noexcept = 0;
    // Once dismount begins, closing locks is explicit recovery, not disconnect
    // cleanup. Keep this owner alive while recovery_required() remains true.
    virtual void release(bool permit_unlock) noexcept = 0;
};
std::unique_ptr<StorageHandles> native_storage_handles();

class StorageOperations final : public extrapane::usb::Operations {
public:
    using Inspect = std::function<Inventory()>;
    StorageOperations(Disk selected, Inspect inspect, std::unique_ptr<StorageBackend> backend,
                      std::unique_ptr<StorageHandles> handles = native_storage_handles());
    ~StorageOperations() override;
    extrapane::usb::Device inspect() override;
    extrapane::Availability claim(const extrapane::usb::Device&) override;
    extrapane::Availability lock_and_dismount(const extrapane::usb::Device&) override;
    extrapane::Availability export_admitted(const extrapane::usb::Device&) override;
    extrapane::Availability flush_and_eject_remote() override;
    void close_transport() noexcept override;
    void stop_export() noexcept override;
    void release_claim() noexcept override;
    bool source_invalidated() const noexcept;
    bool recovery_required() const noexcept { return dismount_started_; }

private:
    bool matches(const extrapane::usb::Device&) const;
    Disk selected_;
    Inspect inspect_;
    std::unique_ptr<StorageBackend> backend_;
    std::unique_ptr<StorageHandles> handles_;
    bool claimed_ = false, locked_ = false, dismount_started_ = false, dismounted_ = false, exported_ = false;
};
}
