#pragma once
#include "windows_peer_media.hpp"
#include <extrapane/service.hpp>
namespace extrapane::windows_peer {
// Shared ownership in hooks keeps native children alive until Service teardown.
// refresh() enumerates only; invoke off the UI thread and publish returned offers
// through the user's selected SharingPolicy settings, not automatically.
class WindowsPeerRuntime {
    struct State;
    std::shared_ptr<State> state_;

public:
    using Inventory = std::function<std::vector<Source>()>;
    using Port = std::function<std::uint16_t()>;
    explicit WindowsPeerRuntime(Factory, Inventory = {}, Port = {});
    std::vector<peers::Offer> refresh();
    std::vector<Source> sources() const;
    peers::Service::Hooks hooks() const;
};
}
