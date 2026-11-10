#include "../platform/linux/resource_fanout.hpp"
#include "../platform/linux/shared_capture_registry.hpp"
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <future>
#include <thread>
using namespace dm::linux_client;
void check(bool value) {
    if (!value) throw std::runtime_error("Fanout invariant failed");
}
struct FakeCapture {
    std::atomic_int& live;
    std::atomic_int& opened;
    int mode;
    std::promise<void>* stopping = nullptr;
    std::shared_future<void> release;
    FakeCapture(std::atomic_int& alive, std::atomic_int& starts, int selected)
        : live(alive), opened(starts), mode(selected) {
        ++live;
        ++opened;
    }
    ~FakeCapture() {
        if (stopping) {
            stopping->set_value();
            release.wait();
        }
        --live;
    }
};
void registryChecks() {
    SharedCaptureRegistry<int, FakeCapture> registry;
    std::atomic_int live = 0, opened = 0;
    auto make = [&] { return std::make_shared<FakeCapture>(live, opened, 30); };
    auto validate = [](FakeCapture& capture) {
        if (capture.mode != 30) throw std::runtime_error("mode");
    };
    {
        auto first = registry.acquire(1, make, validate);
        {
            auto second = registry.acquire(1, make, validate);
            check(live == 1 && opened == 1);
        }
        check(live == 1);
        bool rejected = false;
        try {
            auto wrong = registry.acquire(1, make, [](FakeCapture& capture) {
                if (capture.mode != 60) throw std::runtime_error("mode");
            });
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected && live == 1 && opened == 1 && first->mode == 30);
    }
    check(live == 0);
    std::promise<void> stopping, release;
    auto started = stopping.get_future();
    auto first = registry.acquire(1, make, validate);
    first->stopping = &stopping;
    first->release = release.get_future().share();
    std::thread teardown([lease = std::move(first)]() mutable {});
    check(started.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    std::promise<void> attempting;
    auto attempted = attempting.get_future();
    auto replacement = std::async(std::launch::async, [&] {
        attempting.set_value();
        return registry.acquire(1, make, validate);
    });
    attempted.wait();
    const bool waitsForOwnDevice = replacement.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    // Unrelated keys must remain usable while one provider is stopping.
    auto unrelated = std::async(std::launch::async, [&] { auto other = registry.acquire(2, make, validate); });
    const bool unrelatedReady = unrelated.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    release.set_value();
    teardown.join();
    unrelated.get();
    {
        auto next = replacement.get();
        check(waitsForOwnDevice && unrelatedReady && live == 1);
    }
    check(live == 0 && opened == 4);
}
int main() {
    registryChecks();
    const std::vector<std::uint8_t> configuredIdr{0, 0, 0, 1, 0x67, 42, 0, 0, 1, 0x68, 42, 0, 0, 0, 1, 0x65, 42};
    check(independentlyDecodable(configuredIdr));
    check(!independentlyDecodable(std::vector<std::uint8_t>{0, 0, 1, 0x65, 42}));
    check(!independentlyDecodable(std::vector<std::uint8_t>{0, 0, 1}));
    auto delta = std::make_shared<CapturePacket>();
    delta->bytes = {1, 2};
    auto idr = std::make_shared<CapturePacket>();
    idr->bytes = configuredIdr;
    idr->keyframe = true;
    CaptureQueue first(true), late(true);
    first.push(idr);
    check(first.pop() == idr);
    first.push(delta);
    late.push(delta);
    check(first.pop() == delta);
    check(!late.pop());
    late.push(idr);
    check(late.pop() == idr);
    CaptureQueue slow(false), fast(false);
    for (int i = 0; i < 40; ++i) {
        slow.push(delta);
        fast.push(delta);
        check(fast.pop() == delta);
    }
    check(!slow.error.empty());
    check(!slow.pop());
    check(fast.error.empty());
    auto huge = std::make_shared<CapturePacket>();
    huge->bytes.resize(8 * 1024 * 1024 + 1);
    fast.push(huge);
    check(!fast.error.empty());
    CaptureQueue removed(false), survivor(false);
    removed.fail("Disconnected");
    removed.push(delta);
    survivor.push(delta);
    check(!removed.pop());
    check(survivor.pop() == delta);
    std::cout << "Linux fanout keyframe/config, queue bounds and subscriber isolation checks passed\n";
}
