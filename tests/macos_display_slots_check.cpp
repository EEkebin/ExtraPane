#include "../platform/macos/display_slots.hpp"
#include <thread>
#include <vector>
#include <mutex>
#include <set>
#include <iostream>
int main() {
    dm::mac::DisplaySlots slots;
    std::atomic_uint attempted = 0, accepted = 0;
    std::atomic_bool release = false, duplicate = false;
    std::mutex mutex;
    std::set<unsigned> identities;
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 8; ++i)
        workers.emplace_back([&] {
            try {
                auto lease = slots.acquire();
                {
                    std::lock_guard lock(mutex);
                    if (!identities.insert(lease.index()).second) duplicate = true;
                }
                ++accepted;
                ++attempted;
                while (!release) std::this_thread::yield();
            } catch (const std::runtime_error&) {
                ++attempted;
            }
        });
    while (attempted != 8) std::this_thread::yield();
    release = true;
    for (auto& worker : workers) worker.join();
    if (accepted != 4 || duplicate) return 1;
    std::vector<dm::mac::DisplaySlots::Lease> leases;
    for (unsigned i = 0; i < 4; ++i) leases.push_back(slots.acquire());
    bool full = false;
    try {
        auto fifth = slots.acquire();
    } catch (const std::runtime_error&) {
        full = true;
    }
    if (!full) return 2;
    leases.clear();
    {
        auto released = slots.acquire();
        if (released.index() != 0) return 3;
    }
    std::cout << "Mac display identities remain exclusive and reusable independently of control connections\n";
}
