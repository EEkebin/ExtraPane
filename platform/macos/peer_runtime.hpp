#pragma once
#include <extrapane/service.hpp>
#include <memory>
#include <vector>
namespace dm::mac::peer {
class MacPeerRuntime {
 struct State;std::shared_ptr<State> state_;
public:
 MacPeerRuntime();
 ~MacPeerRuntime();
 // Call on a background/native worker; never activates capture.
 std::vector<extrapane::peers::Offer> refresh();
 extrapane::peers::Service::Hooks hooks();
 static std::string receiveUnavailable(extrapane::peers::Kind);
 // Shut Service down first, then runtime. Receiver UI teardown is queued safely.
 void shutdown();
};
}
