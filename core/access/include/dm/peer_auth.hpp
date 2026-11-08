#pragma once
#include <dm/access.hpp>
#include <set>
#include <atomic>
namespace dm::peer_auth {
using DeviceId=std::array<std::uint8_t,32>;
struct Secret {
    std::array<std::uint8_t,32> bytes{};
    ~Secret();
};
using Lookup=std::function<std::optional<Secret>(const DeviceId&)>;
// Must atomically insert only when absent; never replace an existing peer key.
// Persist securely before returning, or throw. A failed two-sided commit may
// require explicit Forget/re-pair; no automatic fallback weakens remembered trust.
using Remember=std::function<void(const DeviceId&,const Secret&)>;
enum class Mode:std::uint8_t {open=0,pair=1,remembered=2};
struct Identity {
    DeviceId claimed_peer{}; // Untrusted in open mode.
    std::optional<DeviceId> verified_peer; // Only set after PAKE and commit succeed.
    std::optional<Secret> pair_secret;
    bool newly_paired=false; // Self sessions authenticate without persistent trust.
};
// One instance per sharing-process lifetime. Codes never rotate on connection or pairing.
// Five failed new-pair attempts lock pairing until restart; remembered access still works.
class Policy {
    struct Attempts {unsigned failures=0;};
    DeviceId local_;std::atomic_bool required_;bool active_=true;std::string code_;
    std::function<access::Clock::time_point()> clock_;access::Clock::time_point window_;
    std::mutex mutex_;std::map<DeviceId,Attempts> attempts_;std::set<DeviceId> pending_;
    unsigned global_failures_=0,pair_failures_=0,pair_pending_=0;
public:
    Policy(DeviceId local,bool pairing_required=true,std::function<access::Clock::time_point()> now=access::Clock::now);
    ~Policy();
    Policy(const Policy&)=delete;
    std::string code();
    bool pairing_available();
    void stop();
    const DeviceId& local()const{return local_;}
    bool required()const{return required_.load();}
    void set_required(bool required);
    // Internal admission accounting also exposed for isolated fake-clock checks.
    void begin(const DeviceId&,Mode);
    void finish(const DeviceId&,Mode,bool success)noexcept;
    void ensure_active();
};
struct ClientOptions {
    DeviceId local{},expected_peer{}; // expected_peer may be zero only for a new pairing/open lookup.
    Mode mode=Mode::pair;
    std::string code;
    std::optional<Secret> remembered;
    bool outer_encrypted=false;
    DeviceId binding{}; // TLS exporter for this exact outer connection.
};
// Callbacks must read/write the complete span or throw, with an absolute handshake
// deadline and cancellation supplied by the transport. They must be serialized.
// Binding is a nonzero 32-byte TLS exporter, never an advertised or random ID.
class Channel {
    access::Read raw_read_;access::Write raw_write_;std::unique_ptr<access::Stream> protected_;
    bool attempted_=false,ready_=false;
public:
    Channel(access::Read read,access::Write write);
    Identity client(ClientOptions options,const Remember& remember);
    Identity server(Policy&,const Lookup&,const Remember&,bool outer_encrypted,const DeviceId& binding);
    void read(std::span<std::uint8_t>);
    void write(std::span<const std::uint8_t>);
};
DeviceId random_identity();
}
