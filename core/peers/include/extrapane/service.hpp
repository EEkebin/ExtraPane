#pragma once
#include "peer_session.hpp"
#include "store.hpp"
#include <filesystem>

namespace extrapane::peers {
class Service {
public:
    using Token=std::uint64_t;
    struct Options {std::filesystem::path private_directory;std::string name;std::vector<Offer> offers;bool pairing_required=true;};
    struct ConnectOptions {std::string address;std::uint16_t port=0;std::string local;DeviceId expected_peer{};std::string code;bool open_mode=false;};
    struct Context {Token connection=0,request=0,grant=0;DeviceId peer{};bool incoming=false;Kind kind=Kind::display_destination;std::string failure_detail;};
    enum class Role {listen_local,connect_local};
    struct Endpoint {Role role=Role::listen_local;std::uint16_t port=0;};
    struct Hooks {
        // All callbacks run on one dedicated serial native executor, never the TLS
        // IO worker or UI thread. Endpoint values must come
        // from trusted local native adapters; no remote address is accepted.
        std::function<void(const Context&,rpc::Open&)> prepare_request;
        std::function<void(const Context&)> cancel_request;
        std::function<Endpoint(const Context&,rpc::Open&)> prepare;
        std::function<void(const Context&,const rpc::Open&,std::uint16_t)> ready;
        std::function<void(const Context&)> stopped;
        std::function<std::vector<Context>()> poll;
    };
    enum class EventKind {listening,connected,catalog,granted,stopped,disconnected,error,trust_changed,reconnecting};
    struct Event {EventKind kind;Context context;std::uint16_t port=0;std::string detail;std::vector<Catalog> catalogs;bool verified=false;std::optional<rpc::Open> resource;Token replaces_grant=0;Token sharing_generation=0;Token via_connection=0;};
    Service(Options,Hooks);
    ~Service();
    Service(const Service&)=delete;
    DeviceId identity()const;
    std::string pairing_code()const;
    std::vector<RememberedDevice> remembered()const;
    std::vector<RememberedDevice> pending_forgets()const;
    // Start/stop sharing preserves process-lifetime pairing code. stop withdraws
    // local offers/incoming use; independent outgoing resources remain active.
    // Methods enqueue bounded commands; poll drains UI events.
    // Returned generation identifies the matching asynchronous listening event.
    Token start(std::string bind,std::uint16_t port);
    struct ListenerChange {Token generation=0;std::string address;std::uint16_t port=0;std::string error;};
    // Replace only the acceptor, preserving paired links and active resources.
    // On failure report the actual restored endpoint (port zero if unavailable).
    ListenerChange rebind(std::string bind,std::uint16_t port);
    void stop();
    Token connect(ConnectOptions);
    // End-to-end endpoint authentication through one paired gateway. No media starts.
    Token connect_via(Token gateway_connection,DeviceId owner,std::string code={});
    Token catalog(Token connection);
    Token open(Token connection,rpc::Open);
    void close(Token connection,Token grant);
    void cancel(Token connection,Token request);
    void disconnect(Token connection);
    void offers(std::vector<Offer>);
    // Immediately remove ordinary local trust; retain a protected revocation-only
    // outbox until the other device durably acknowledges removal. Older/offline
    // peers remain pending. Dismiss is explicit local cleanup, not a remote ACK.
    void forget(DeviceId);
    void dismiss_forget(DeviceId);
    void pairing_required(bool);
    void gateway(bool enabled);
    // Catalog-only remembered device presence. Never opens a media resource.
    void auto_connect(bool enabled);
    void observe_route(DeviceId peer,std::string address,std::uint16_t port);
    std::vector<Event> poll();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
