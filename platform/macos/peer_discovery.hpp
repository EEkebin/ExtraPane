#pragma once
#include <extrapane/peers.hpp>
#include <memory>
namespace dm::mac {
class PeerDiscovery {
    struct State;std::unique_ptr<State> state_;
public:
    PeerDiscovery();~PeerDiscovery();
    void start(const std::string& local={});
    void advertise(extrapane::peers::Announcement);
    void withdraw();
    void stop();
    std::vector<extrapane::peers::Route> poll();
};
}
