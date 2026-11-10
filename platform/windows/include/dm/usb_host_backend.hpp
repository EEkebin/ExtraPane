#pragma once
#include <dm/usb_storage.hpp>
#include <dm/usb_runner.hpp>
#include <dm/usb_recovery.hpp>
#include <cstdint>

namespace dm::usb {
struct HostTransport {
    // Operates on an already admitted TLS session; native tunnel validates the
    // exact USB/IP import bus ID before forwarding to this loopback endpoint.
    std::function<extrapane::Availability(const std::string& bus, std::uint16_t port)> start;
    std::function<void()> close;
    // Must prove destination flush/eject, not merely receipt of a disconnect.
    std::function<extrapane::Availability()> flush_and_eject;
    std::function<extrapane::Availability(const Disk&)> handoff_quarantined;
    // Reports actual source restoration, independently of remote eject success.
    std::function<void(extrapane::Availability)> source_restored;
};
std::unique_ptr<StorageBackend> usb_host_backend(std::unique_ptr<HostRunner> runner,
                                                 std::unique_ptr<RecoveryGuard> recovery, HostTransport transport,
                                                 std::uint16_t loopback_port);
}
