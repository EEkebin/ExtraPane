#pragma once
#include <extrapane/peer_transport.hpp>
#include <extrapane/peer_rpc.hpp>
#include <dm/peer_auth.hpp>
#include <deque>

namespace extrapane::peers {
// One I/O owner. Only cancellation may be called from another thread. Identity
// is derived from exporter-bound authentication, never discovery or request data.
class AuthenticatedConnection {
    transport::Stream stream_;
    ConnectionIdentity identity_;
    bool revocation_only_=false;
    std::optional<dm::peer_auth::Secret> revocation_secret_;
    rpc::Decoder decoder_;
    std::unique_ptr<std::array<std::uint8_t,rpc::data_chunk>> read_buffer_=
        std::make_unique<std::array<std::uint8_t,rpc::data_chunk>>();
    std::deque<Bytes> output_;
    std::deque<rpc::Message> input_;
    std::size_t output_bytes_=0,input_bytes_=0,offset_=0;
    AuthenticatedConnection(transport::Stream,dm::peer_auth::Identity);
public:
    static constexpr std::size_t max_queued=4*1024*1024,max_messages=256;
    static AuthenticatedConnection client(transport::Stream,dm::peer_auth::ClientOptions,
        const dm::peer_auth::Remember&,transport::Deadline);
    static AuthenticatedConnection server(transport::Stream,dm::peer_auth::Policy&,
        const dm::peer_auth::Lookup&,const dm::peer_auth::Remember&,transport::Deadline,const dm::peer_auth::Lookup& revocation_lookup={});
    AuthenticatedConnection(AuthenticatedConnection&&) noexcept=default;
    AuthenticatedConnection& operator=(AuthenticatedConnection&&) noexcept=default;
    const std::optional<dm::peer_auth::Secret>& revocation_secret()const noexcept{return revocation_secret_;}
    bool revocation_only()const noexcept{return revocation_only_;}
    const ConnectionIdentity& identity()const noexcept{return identity_;}
    const DeviceId& remote_id()const noexcept{return identity_.device;}
    void queue(const rpc::Message&);
    // Processes available TLS input/output without blocking. False means no progress.
    bool pump(transport::Deadline);
    std::optional<rpc::Message> next();
    bool pending_output()const noexcept{return !output_.empty();}
    void wait(transport::Deadline);
    void send(const rpc::Message&,transport::Deadline);
    rpc::Message receive(transport::Deadline);
    void cancel()noexcept{stream_.cancel();}
};
struct Adapter {
    // A grant selects the resource. No caller-provided destination address exists.
    // false/exception triggers stop too, to undo partial preparation.
    std::function<bool(const Grant&,const rpc::Open&,std::string& reason)> start;
    std::function<void(const Grant&)> stop;
    // Optional reverse-direction native ticket, generated only after authorization.
    std::function<std::string(const Grant&)> credential;
};
// Owner-thread policy executor. SharingPolicy and Adapter must outlive it; their
// other users must use the same owner thread or external synchronization.
class SessionDispatcher {
    SharingPolicy& policy_;
    ConnectionIdentity identity_;
    bool revocation_only_=false;
    std::optional<dm::peer_auth::Secret> revocation_secret_;
    Adapter adapter_;
    std::map<std::uint64_t,Grant> owned_;
    bool closed_=false;
    void stop(const Grant&)noexcept;
public:
    SessionDispatcher(SharingPolicy&,const AuthenticatedConnection&,Adapter);
    ~SessionDispatcher();
    SessionDispatcher(const SessionDispatcher&)=delete;
    SessionDispatcher& operator=(const SessionDispatcher&)=delete;
    rpc::Message handle(const rpc::Message&,Milliseconds now);
    // Pass cancellations returned by policy mutations (forget, offers, expiry,
    // stop sharing, gateway off). Only this connection's adapters are affected.
    std::vector<rpc::Message> revoked(std::span<const Grant>);
    bool owns(std::uint64_t grant)const noexcept;
    void close()noexcept;
};
}
