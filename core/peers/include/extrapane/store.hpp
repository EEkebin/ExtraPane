#pragma once
#include "peers.hpp"
#include <filesystem>

namespace extrapane::peers {
using PairSecret = std::array<std::uint8_t,32>;
struct RememberedDevice {
    DeviceId id{};
    std::string name, address_hint;
    std::uint16_t port_hint=0;
};
// User-private store. Windows encrypts with user-scope DPAPI; POSIX uses private
// 0700/0600 storage (not OS-keychain encryption). The supplied path MUST be the
// application's user-private directory, never removable/shared/public storage.
// Mutations are lock-serialized, atomic and durable before returning. A failed
// save throws: pairing must not acknowledge a remembered grant before it returns.
class TrustStore {
    std::filesystem::path directory_;
    DeviceId identity_{};
public:
    explicit TrustStore(std::filesystem::path private_directory);
    const DeviceId& identity()const noexcept{return identity_;}
    std::vector<RememberedDevice> devices()const;
    std::optional<PairSecret> secret(const DeviceId&)const;
    // Insert-only, including when the same key already exists. Forget/re-pair
    // explicitly; never silently replace a remembered device based on IP/name.
    void remember(RememberedDevice, const PairSecret&);
    void update_hint(RememberedDevice);
    bool forget(const DeviceId&);
};
}
