#include <dm/usb_host_backend.hpp>
#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <chrono>
#include <thread>

namespace dm::usb {
namespace {
using extrapane::Availability;
Availability fail(const char* text) {
    return {false, text};
}
Availability step(Availability result, const char* name) {
    if (!result.available) result.reason = std::string(name) + ": " + result.reason;
    return result;
}
bool equal_id(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) {
               return std::towupper(x) == std::towupper(y);
           });
}
bool canonical_bus(const std::string& value) {
    const auto dash = value.find('-');
    if (dash == std::string::npos || dash == 0 || dash > 2 || value.size() - dash - 1 < 1 ||
        value.size() - dash - 1 > 2)
        return false;
    for (const auto offset : {std::size_t{0}, dash + 1})
        if (value[offset] < '1' || value[offset] > '9') return false;
    for (std::size_t i = 0; i < value.size(); ++i)
        if (i != dash && (value[i] < '0' || value[i] > '9')) return false;
    return true;
}
bool canonical_guid(const std::wstring& guid) {
    if (guid.size() != 38 || guid.front() != L'{' || guid.back() != L'}') return false;
    for (std::size_t i = 1; i < 37; ++i) {
        if (i == 9 || i == 14 || i == 19 || i == 24) {
            if (guid[i] != L'-') return false;
        } else if (!((guid[i] >= L'0' && guid[i] <= L'9') || (guid[i] >= L'a' && guid[i] <= L'f') ||
                     (guid[i] >= L'A' && guid[i] <= L'F')))
            return false;
    }
    return true;
}
class Backend final : public StorageBackend {
public:
    Backend(std::unique_ptr<HostRunner> runner, std::unique_ptr<RecoveryGuard> recovery, HostTransport transport,
            std::uint16_t port)
        : runner_(std::move(runner)), recovery_(std::move(recovery)), transport_(std::move(transport)), port_(port) {}
    ~Backend() override {
        close_transport();
        stop_export();
        release_claim(true);
    }
    Availability claim(const Disk& disk) override {
        if (reserved_ || quarantined_ || !runner_ || !recovery_ || !port_ || !transport_.start || !transport_.close ||
            !transport_.flush_and_eject || !transport_.handoff_quarantined)
            return fail("USB host backend, recovery guard, or admitted transport is unavailable");
        if (!extrapane::usb::eligible(disk.facts).available || disk.usb_instance_id.empty())
            return fail("USB source inventory is not eligible");
        auto result = runner_->ready();
        if (!result.available) return result;
        result = runner_->reserve(disk.usb_instance_id);
        if (!result.available) return result;
        try {
            const auto candidate = find(disk.usb_instance_id);
            if (!candidate || !canonical_bus(candidate->bus_id) || candidate->forced || candidate->attached ||
                !candidate->binding_id.empty()) {
                runner_->release();
                return fail("Selected USB device is missing, already bound, or owned elsewhere");
            }
            selected_ = disk;
            bus_ = candidate->bus_id;
            reserved_ = true;
            return {true, {}};
        } catch (...) {
            runner_->release();
            throw;
        }
    }
    Availability export_locked(const Disk& disk) override {
        if (!reserved_ || quarantined_ || disk.facts.id != selected_.facts.id ||
            disk.facts.generation != selected_.facts.generation ||
            !equal_id(disk.usb_instance_id, selected_.usb_instance_id))
            return fail("USB source ownership changed before export");
        const auto before = find(selected_.usb_instance_id);
        if (!before || before->bus_id != bus_ || before->forced || before->attached || !before->binding_id.empty())
            return fail("USB device changed or became bound before export");
        // This durable guard must confirm persistent offline state, not merely
        // retain volume handles; usbipd can restore the source driver on loss.
        quarantined_ = true;
        auto result = step(recovery_->quarantine(selected_), "Preparing persistent USB quarantine");
        if (!result.available) return result;
        result = step(recovery_->verify(selected_), "Verifying disk after taking it offline");
        if (!result.available) return result;
        result = step(recovery_->binding_intent(selected_, bus_), "Recording USB binding intent");
        if (!result.available) return result;
        result = step(transport_.handoff_quarantined(selected_), "Handing off quarantined USB handles");
        if (!result.available) return result;
        result = step(recovery_->verify(selected_), "Verifying disk after handle handoff");
        if (!result.available) return result;
        // No --force, broad hardware ID, auto-bind policy, or remote device ID.
        result = step(runner_->bind(bus_), "Binding the selected USB source");
        if (!result.available) return result;
        const auto bound = find(selected_.usb_instance_id);
        if (!bound || bound->bus_id != bus_ || bound->forced || bound->attached || !canonical_guid(bound->binding_id))
            return fail("USB binding result is ambiguous; keep the device quarantined for recovery");
        binding_ = bound->binding_id;
        result = recovery_->binding_acquired(selected_, binding_);
        if (!result.available) return result;
        result = recovery_->verify(selected_);
        if (!result.available) return result;
        result = runner_->start(selected_.usb_instance_id, bus_, port_);
        if (!result.available) return result;
        if (!runner_->running()) return fail("Private USB host stopped before transport admission");
        result = transport_.start(bus_, port_);
        if (result.available) transport_open_ = true;
        return result;
    }
    Availability flush_and_eject_remote() override {
        if (!transport_open_ || !runner_ || !runner_->running())
            return fail("USB destination is unavailable for a verified eject");
        auto result = transport_.flush_and_eject();
        if (result.available) {
            result = recovery_->destination_ejected(selected_);
            if (result.available) eject_confirmed_ = true;
        }
        return result;
    }
    void close_transport() noexcept override {
        try {
            if (transport_.close) transport_.close();
        } catch (...) {}
        transport_open_ = false;
    }
    void stop_export() noexcept override {
        if (runner_) runner_->stop();
    }
    void release_claim(bool) noexcept override {
        if (!reserved_ || !runner_) return;
        close_transport();
        stop_export();
        Availability restored{false, "USB source return requires stopped exporter and exact binding release"};
        // Clean return records destination flush; interrupted return never fabricates it.
        if (eject_confirmed_ && !binding_.empty() && !runner_->running()) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            bool unbindRequested = false, releaseRecorded = false;
            do {
                try {
                    const auto device = find(selected_.usb_instance_id);
                    if (device && device->bus_id == bus_ && !device->attached && !device->forced) {
                        if (!unbindRequested && equal_id(device->binding_id, binding_) &&
                            recovery_->verify(selected_).available) {
                            const auto removed = runner_->unbind(binding_);
                            unbindRequested = removed.available;
                        } else if (unbindRequested && device->binding_id.empty()) {
                            if (!releaseRecorded) {
                                const auto released = recovery_->binding_released(selected_, binding_);
                                if (!released.available) {
                                    restored = released;
                                    break;
                                }
                                releaseRecorded = true;
                            }
                            restored =
                                recovery_->restore_source(selected_, [&](const RecoveryRecord& record) -> Availability {
                                    const auto current = find(selected_.usb_instance_id);
                                    if (record.state != RecoveryState::released || record.bus_id != bus_ || !current ||
                                        current->bus_id != bus_ || current->attached || current->forced ||
                                        !current->binding_id.empty())
                                        return fail("Source backend ownership changed before automatic return");
                                    return {true, {}};
                                });
                            if (restored.available) {
                                binding_.clear();
                                break;
                            }
                        }
                    }
                    // Changed binding identity is not an asynchronous reattach.
                    if (device && (!device->binding_id.empty() && !equal_id(device->binding_id, binding_))) break;
                } catch (...) {}
                if (std::chrono::steady_clock::now() >= deadline) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            } while (true);
        }
        if (!eject_confirmed_ && !runner_->running()) {
            if (!quarantined_)
                restored = {true, {}};
            else {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                do {
                    try {
                        restored = recovery_->restore_interrupted_source(
                            selected_, [&](const RecoveryRecord& record) -> Availability {
                                if (runner_->running()) return fail("Private USB exporter has not stopped");
                                if (!record.bus_id.empty() && record.bus_id != bus_)
                                    return fail("USB journal bus identity changed");
                                const auto device = find(selected_.usb_instance_id);
                                if (!device || device->bus_id != bus_ || device->attached || device->forced)
                                    return fail("Exact USB source is not released yet");
                                if (!device->binding_id.empty()) {
                                    if (binding_.empty() || !equal_id(device->binding_id, binding_))
                                        return fail("USB binding belongs to another owner");
                                    const auto removed = runner_->unbind(binding_);
                                    if (!removed.available) return removed;
                                }
                                const auto current = find(selected_.usb_instance_id);
                                if (!current || current->bus_id != bus_ || current->attached || current->forced ||
                                    !current->binding_id.empty())
                                    return fail("USB source is re-enumerating after release");
                                return {true, {}};
                            });
                        if (restored.available) {
                            binding_.clear();
                            break;
                        }
                    } catch (const std::exception& error) {
                        restored = {false, error.what()};
                    } catch (...) {
                        restored = fail("USB source return failed");
                    }
                    if (std::chrono::steady_clock::now() >= deadline) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                } while (true);
            }
        }
        runner_->release();
        reserved_ = false;
        if (!restored.available && !runner_->running() && quarantined_ && !binding_.empty()) {
            try {
                const auto absent =
                    recovery_->release_absent_source(selected_, [&](const PendingRecovery& pending) -> Availability {
                        if (runner_->running() || !pending.record || pending.record->version != 2 ||
                            pending.record->state != RecoveryState::bound || pending.record->bus_id != bus_ ||
                            !equal_id(
                                std::wstring(pending.record->binding_id.begin(), pending.record->binding_id.end()),
                                binding_))
                            return fail("Absent USB release does not match this stopped session");
                        const auto source = runner_->absent_binding_source(pending);
                        if (source.empty() || !equal_id(source, selected_.usb_instance_id))
                            return fail("Exact USB source is not proven absent");
                        return runner_->release_absent_binding(pending, source);
                    });
                if (absent.available) {
                    binding_.clear();
                    restored = {true,
                                "USB ownership released; device is unplugged. Interrupted writes may be incomplete."};
                }
            } catch (...) {}
        }
        try {
            if (transport_.source_restored) transport_.source_restored(restored);
        } catch (...) {}
    }

private:
    std::optional<HostDevice> find(const std::wstring& identity) {
        std::optional<HostDevice> found;
        for (const auto& device : runner_->devices())
            if (equal_id(device.instance_id, identity)) {
                if (found) throw std::runtime_error("Duplicate USB instance identity in local backend inventory");
                found = device;
            }
        return found;
    }
    std::unique_ptr<HostRunner> runner_;
    std::unique_ptr<RecoveryGuard> recovery_;
    HostTransport transport_;
    std::uint16_t port_;
    Disk selected_;
    std::string bus_;
    std::wstring binding_;
    bool reserved_ = false, quarantined_ = false, transport_open_ = false, eject_confirmed_ = false;
};
}
std::unique_ptr<StorageBackend> usb_host_backend(std::unique_ptr<HostRunner> runner,
                                                 std::unique_ptr<RecoveryGuard> recovery, HostTransport transport,
                                                 std::uint16_t loopback_port) {
    return std::make_unique<Backend>(std::move(runner), std::move(recovery), std::move(transport), loopback_port);
}
}
