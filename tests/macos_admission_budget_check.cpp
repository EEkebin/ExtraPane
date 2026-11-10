#include "../platform/macos/admission_budget.hpp"
#include "../platform/macos/display_slots.hpp"
#include <iostream>
#include <vector>
int main() {
    dm::mac::AdmissionBudget budget(32);
    dm::mac::DisplaySlots displays;
    std::vector<dm::mac::AdmissionBudget::Lease> controls;
    for (unsigned i = 0; i < 32; ++i) {
        auto lease = budget.tryAcquire();
        if (!lease) return 1;
        controls.push_back(std::move(*lease));
    }
    // Established controls retain their budget and cannot accumulate forever.
    if (budget.tryAcquire()) return 2;
    // Closing one control permits a new handshake, without reserving a monitor.
    controls.pop_back();
    auto handshake = budget.tryAcquire();
    if (!handshake || budget.tryAcquire()) return 3;
    auto display = displays.acquire();
    if (display.index() != 0) return 4;
    // Only validated display promotion releases the admission worker budget.
    handshake->release();
    handshake->release();
    auto next = budget.tryAcquire();
    if (!next || budget.tryAcquire()) return 5;
    controls.clear();
    next.reset();
    for (unsigned i = 0; i < 32; ++i) {
        auto lease = budget.tryAcquire();
        if (!lease) return 6;
        controls.push_back(std::move(*lease));
    }
    if (budget.tryAcquire()) return 7;
    std::cout << "Mac control workers retain bounded admission leases; promotion and teardown release exactly once\n";
}
