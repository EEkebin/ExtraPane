#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace extrapane::peers {
using DeviceId = std::array<std::uint8_t,32>;
using Bytes = std::vector<std::uint8_t>;
using Milliseconds = std::uint64_t;
inline constexpr std::size_t max_resources = 128;
inline constexpr std::size_t max_catalog_bytes = 65536;
inline constexpr std::size_t max_discovered_routes = 256;
enum class Kind : std::uint8_t { display_destination=0, camera=1, microphone=2, usb=3, speaker=4, existing_screen=5 };
struct Offer {
    std::string id, label;
    Kind kind = Kind::display_destination;
    bool available = false;
    bool input_allowed = false; // Explicit; existing screen defaults to view-only.
    std::string unavailable_reason;
    // Optional native preset/actual existing-screen mode, not a throughput promise.
    // All zero means unknown/custom. Transport adapters negotiate their own limits.
    std::uint32_t width=0, height=0, refresh=0;
    bool operator==(const Offer&)const = default;
};
struct Announcement {
    DeviceId device{};
    std::uint64_t instance=0, sequence=0;
    std::string name;
    std::uint16_t port=0, lifetime_seconds=15;
    std::uint8_t offered_kinds=0;
    bool pairing_required=true;
    bool operator==(const Announcement&)const = default;
};
struct Withdrawal {
    DeviceId device{};
    std::uint64_t instance=0, sequence=0;
    bool operator==(const Withdrawal&)const = default;
};
struct Catalog {
    DeviceId device{};
    std::uint64_t instance=0, revision=0;
    std::string name;
    std::vector<Offer> offers;
    bool operator==(const Catalog&)const = default;
};
bool valid_id(const DeviceId&) noexcept;
void validate(const Catalog&);
Bytes encode(const Announcement&);
Bytes encode(const Withdrawal&);
Bytes encode(const Catalog&);
Announcement decode_announcement(std::span<const std::uint8_t>);
Withdrawal decode_withdrawal(std::span<const std::uint8_t>);
Catalog decode_catalog(std::span<const std::uint8_t>);

// Datagram metadata comes from the socket, never from an address in the packet.
// These are untrusted discovery hints, NOT authenticated device identities.
struct Route {
    Announcement announcement;
    std::string observed_ipv4;
    std::uint32_t interface_index=0;
    Milliseconds expires_at=0;
};
class DiscoveryTable {
    using Key = std::pair<DeviceId,std::uint32_t>;
    std::set<std::uint32_t> interfaces_;
    std::map<Key,Route> routes_;
public:
    void interfaces(std::set<std::uint32_t> selected);
    bool observe(const Announcement&, std::string observed_ipv4, std::uint32_t interface_index, Milliseconds now);
    bool withdraw(const Withdrawal&, const std::string& observed_ipv4, std::uint32_t interface_index);
    void expire(Milliseconds now);
    std::vector<Route> snapshot(Milliseconds now);
    void clear() noexcept { routes_.clear(); }
};

// ONLY the verified transport supplies this identity. Constructing this struct
// does not authenticate anyone. Discovery/JSON/request-supplied flags must never
// be used to set paired=true. Pairing/keys/persistence live in the auth adapter.
struct ConnectionIdentity { DeviceId device{}; bool paired=false; };
struct Grant {
    std::uint64_t id=0;
    DeviceId visitor{}, owner{};
    std::string resource;
    Kind kind=Kind::display_destination;
    bool input=false, delegated=false, paired=false;
};
struct Decision {
    std::optional<Grant> grant;
    std::string reason;
};
// Owner-executor-only authorization/lifetime policy, not cryptography or media.
// Returned canceled grants MUST be stopped by the transport/adapter immediately.
class SharingPolicy {
    struct Remote { Catalog catalog; Milliseconds expires; };
    Catalog local_;
    bool sharing_=false, pairing_required_=true, gateway_=false;
    std::set<DeviceId> trusted_;
    std::map<DeviceId,Remote> remotes_;
    std::map<std::uint64_t,Grant> grants_;
    std::uint64_t next_grant_=0;
    std::vector<Grant> cancel_matching(const DeviceId*, bool delegated_only);
public:
    explicit SharingPolicy(Catalog local);
    bool sharing()const noexcept{return sharing_;}
    bool pairing_required()const noexcept{return pairing_required_;}
    bool gateway_enabled()const noexcept{return gateway_;}
    void remember(const DeviceId&); // Call only after verified pairing succeeds.
    std::vector<Grant> forget(const DeviceId&);
    std::vector<Grant> sharing(bool);
    std::vector<Grant> pairing_required(bool);
    std::vector<Grant> gateway(bool);
    std::vector<Grant> offers(std::vector<Offer>);
    // Catalog must arrive on an authenticated DIRECT session to its owner.
    // Hops>0 are always rejected, even when that gateway is trusted.
    std::vector<Grant> remote(const ConnectionIdentity&, Catalog, unsigned hops,
                              Milliseconds now, std::uint16_t lifetime_seconds=30);
    std::vector<Grant> expire(Milliseconds now);
    std::vector<Grant> remote_removed(const DeviceId&);
    std::vector<Catalog> visible(const ConnectionIdentity&, Milliseconds now)const;
    Decision open(const ConnectionIdentity&, const DeviceId& owner, const std::string& resource,
                  bool wants_input, Milliseconds now);
    bool close(std::uint64_t grant) noexcept; // Local owner/transport teardown only.
    bool close(const ConnectionIdentity& requester, std::uint64_t grant) noexcept;
    std::vector<Grant> grants()const;
};

enum class Failure { transport_lost, explicit_stop, permission_revoked, identity_changed, resource_removed };
class Reconnect {
    Kind kind_;
    unsigned attempts_=0;
    std::optional<Milliseconds> interrupted_, due_;
    bool stopped_=false;
public:
    explicit Reconnect(Kind kind):kind_(kind){}
    // At most five retries within30s:1,2,4,8,8s. No automatic USB retry.
    std::optional<Milliseconds> failed(Failure, Milliseconds now);
    bool ready(Milliseconds now)const noexcept;
    void connected()noexcept;
    void stop()noexcept;
    unsigned attempts()const noexcept{return attempts_;}
};
}
