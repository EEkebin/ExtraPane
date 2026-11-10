#include "../apps/usb-host-setup/monitor_policy.hpp"
#include "../apps/usb-host-setup/uninstall_policy.hpp"
#include "../apps/usb-host-setup/install_policy.hpp"
#include <stdexcept>
#include <vector>
struct ReuseCheck {
    bool marker = true;
    int fail = 0;
    std::vector<int> calls;
    bool has_manifest() { return marker; }
    void verify(int stage) {
        calls.push_back(stage);
        if (fail == stage) throw std::runtime_error("verification refused");
    }
    void payload() { verify(1); }
    void ownership() { verify(2); }
    void registration() { verify(3); }
    void monitor() { verify(4); }
};
struct MigrationCheck {
    int fail = 0;
    std::vector<int> calls;
    void stage(int n) {
        calls.push_back(n);
        if (n == fail) throw std::runtime_error("migration refused");
    }
    void payload() { stage(1); }
    void ownership() { stage(2); }
    void idle() { stage(3); }
    void replace() { stage(4); }
};
int main() {
    using namespace extrapane::usb_monitor;
    {
        using namespace extrapane::usb_host_setup;
        auto current = previous_payload;
        current[0] = L"new-reviewed-build";
        auto installed = previous_payload;
        if (!reviewed_migration_payload(installed, current, true))
            throw std::runtime_error("exact previous package must migrate");
        installed[0] = current[0];
        if (!reviewed_migration_payload(installed, current, true))
            throw std::runtime_error("interrupted executable replacement must resume");
        if (reviewed_migration_payload(installed, current, false))
            throw std::runtime_error("unknown manifest accepted");
        for (std::size_t i = 0; i < 8; ++i) {
            auto altered = previous_payload;
            altered[i] = L"foreign";
            if (reviewed_migration_payload(altered, current, true))
                throw std::runtime_error("changed installed file accepted");
        }
        for (std::size_t i = 1; i < 8; ++i) {
            auto altered = current;
            altered[i] = L"different-driver";
            if (reviewed_migration_payload(previous_payload, altered, true))
                throw std::runtime_error("driver migration not separately reviewed");
        }
        MigrationCheck success;
        migrate_reviewed_backend(success);
        if (success.calls != std::vector<int>{1, 2, 3, 4}) throw std::runtime_error("migration order");
        for (int stage = 1; stage < 4; ++stage) {
            MigrationCheck rejected;
            rejected.fail = stage;
            bool threw = false;
            try {
                migrate_reviewed_backend(rejected);
            } catch (...) {
                threw = true;
            }
            if (!threw || rejected.calls.size() != stage)
                throw std::runtime_error("migration mutated before verification");
        }
    }
    // This executes the same install dispatch used by the Windows setup helper.
    // No bind/recovery-empty/mutation operation is available to the reuse branch.
    ReuseCheck absent;
    absent.marker = false;
    if (extrapane::usb_host_setup::reuse_exact_backend(absent) || !absent.calls.empty())
        throw std::runtime_error("missing marker must use guarded install path");
    ReuseCheck intact;
    if (!extrapane::usb_host_setup::reuse_exact_backend(intact) || intact.calls != std::vector<int>{1, 2, 3, 4})
        throw std::runtime_error("reuse needs every exact verification");
    for (int stage = 1; stage <= 4; ++stage) {
        ReuseCheck rejected;
        rejected.fail = stage;
        bool threw = false;
        try {
            extrapane::usb_host_setup::reuse_exact_backend(rejected);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        if (!threw || rejected.calls.size() != stage)
            throw std::runtime_error("failed reuse verification must not fall through to repair");
    }
    if (!reviewed(legacy_7_2_2, L"bundle") || !reviewed(L"bundle", L"bundle") || reviewed(L"unknown", L"bundle"))
        throw std::runtime_error("only reviewed shared files");
    if (!compatible(5, 0, 8) || !compatible(5, 1, 8) || compatible(4, 99, 8) || compatible(6, 0, 8) ||
        compatible(5, 0, 7) || compatible(5, 0, 9))
        throw std::runtime_error("exact version reply and supported ABI");
    using namespace extrapane::usb_uninstall;
    for (unsigned bits = 0; bits < 8; ++bits)
        if (empty_removal_ready(bits & 1, bits & 2, bits & 4) != (bits == 7))
            throw std::runtime_error("empty retry record accepted only after both payloads gone");
    for (unsigned bits = 0; bits < 32; ++bits) {
        const bool eligible = removable_stub(bits & 1, bits & 2, bits & 4, bits & 8, bits & 16);
        if (eligible != (bits == 15)) throw std::runtime_error("only absent exact owned USB stubs may be removed");
    }
    if (independent_service(L"VBoxUSB", true) || !independent_service(L"VBoxUSB", false))
        throw std::runtime_error("exact owned USB device driver must not count as foreign monitor consumer");
    for (unsigned bits = 0; bits < 64; ++bits) {
        if (removable_missing_driver(bits & 1, bits & 2, bits & 4, bits & 8, bits & 16, bits & 32) != (bits == 63))
            throw std::runtime_error(
                "missing USB image is removable only for an exact owned stopped disabled unreferenced service");
    }
    for (unsigned bits = 0; bits < 128; ++bits) {
        if (removable_pending_stub(bits & 1, bits & 2, bits & 4, bits & 8, bits & 16, bits & 32, bits & 64) !=
            (bits == 127))
            throw std::runtime_error(
                "pending capture removal requires exact ownership, Code 21, no children and stopped backend");
    }
    for (auto name : {L"usbipd", L"VBoxSup", L"VBoxDrv"})
        if (!independent_service(name, true) || !independent_service(name, false))
            throw std::runtime_error("independent services always block owned monitor deletion");
    for (unsigned bits = 0; bits < 32; ++bits) {
        const bool exists = bits & 1, created = bits & 2, managed = bits & 4, hash = bits & 8, shared = bits & 16;
        const auto result = monitor_action(exists, created, managed, hash, shared);
        if (result == MonitorAction::remove && !(exists && created && managed && hash && !shared))
            throw std::runtime_error("never remove unknown/shared monitors");
        if (exists && !created && !managed && result != MonitorAction::preserve)
            throw std::runtime_error("preserve external monitors");
        if (!exists && result != MonitorAction::preserve) throw std::runtime_error("retry after service removal");
    }
    if (monitor_action(true, true, true, true, false) != MonitorAction::remove)
        throw std::runtime_error("remove verified owned monitor");
    // Desktop regression: ExtraPane staged an INF but reused an independent
    // usbipd-win monitor. Preserve both shared drivers and allow private cleanup.
    if (monitor_action(true, false, false, false, true) != MonitorAction::preserve ||
        inf_action(true, true) != InfAction::preserve)
        throw std::runtime_error("independent usbipd must not block private component removal");
    if (monitor_action(true, true, true, true, true) != MonitorAction::refuse)
        throw std::runtime_error("shared monitor still needs owned private file");
    if (inf_action(true, false) != InfAction::remove || inf_action(false, false) != InfAction::preserve ||
        inf_action(false, true) != InfAction::preserve)
        throw std::runtime_error("remove only unshared owned INF");
    for (auto name : {L"oem0.inf", L"oem123.inf"})
        if (!published_inf(name)) throw std::runtime_error("valid published INF");
    for (auto name : {L"oem.inf", L"VBoxUSB.inf", L"oem1.inf.old", L"../oem1.inf", L"oem-1.inf", L"oem1/2.inf"})
        if (published_inf(name)) throw std::runtime_error("reject non-published INF identity");
}
