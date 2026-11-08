#pragma once
#include "peers.hpp"
#include <deque>
#include <functional>

namespace extrapane::peers::rpc {
// Used only after peer_auth succeeds on this TLS connection. No address or port
// exists in a tunnel request: the authorized grant selects a local media adapter.
enum class Operation:std::uint8_t { catalog=1, catalog_reply=2, open=3, granted=4,
    rejected=5, close=6, stopped=7, stream_open=8, stream_data=9,
    stream_credit=10, stream_close=11, ping=12, pong=13, cancel_request=14 };
// Existing peers echo the probe; only upgraded peers return the distinct ack.
inline constexpr std::uint64_t cancel_probe=0x455043414e43454cULL, cancel_ack=0x455043414e41434bULL;
inline constexpr std::size_t header_size=24, max_payload=65536, data_chunk=16384;
struct Message {Operation operation;std::uint64_t id=0;Bytes payload;};
using Header=std::array<std::uint8_t,header_size>;
Header header(const Message&);
Message decode_header(std::span<const std::uint8_t>,std::size_t& length);
Bytes encode(const Message&);
struct Open {
    DeviceId owner{};
    std::string resource;
    bool input=false;
    // Zero means use the provider's offered native preset. Otherwise all three
    // values must be supplied. Existing-screen/camera modes validate separately.
    std::uint32_t width=0,height=0,refresh=0;
    // Kind-scoped, short-lived native admission ticket. Never a pairing code.
    std::string native_credential;
};
struct Granted { std::uint64_t grant=0; std::string native_credential; };
Bytes encode(const Granted&);
Granted decode_granted(std::span<const std::uint8_t>);
Bytes encode(const Open&);
Open decode_open(std::span<const std::uint8_t>);
Bytes integer(std::uint64_t);
std::uint64_t integer(std::span<const std::uint8_t>);
Bytes detail(const std::string&);
std::string detail(std::span<const std::uint8_t>);
Bytes encode_catalogs(const std::vector<Catalog>&);
std::vector<Catalog> decode_catalogs(std::span<const std::uint8_t>);

// Incremental parser: bounded allocation, handles partial TLS reads, and remains
// poisoned after any malformed message so stream bytes cannot be reinterpreted.
class Decoder {
    Bytes pending_;
    bool failed_=false;
public:
    std::vector<Message> feed(std::span<const std::uint8_t>);
    bool failed()const noexcept{return failed_;}
};

// Per authenticated peer connection. Each stream is attached to an authorized
// resource grant; revoking that grant returns all stream IDs for local closure.
// No decoder/network assertion can establish grant authorization.
class Multiplexer {
public:
    static constexpr std::size_t window=65536,max_streams=256,max_queued=window*max_streams;
    using Authorize=std::function<bool(std::uint64_t grant)>;
    struct StreamInfo {std::uint64_t grant=0;std::size_t send_credit=window,receive_credit=window;Bytes pending;};
private:
    bool initiator_;
    std::uint64_t next_;
    std::uint64_t last_remote_=0;
    std::map<std::uint64_t,StreamInfo> streams_;
    std::size_t queued_=0;
    Authorize authorize_;
public:
    Multiplexer(bool connection_initiator,Authorize);
    Message open(std::uint64_t grant);
    void opened(const Message&);
    std::optional<Message> send(std::uint64_t stream,std::span<const std::uint8_t>);
    void receive(const Message&);
    std::span<const std::uint8_t> pending(std::uint64_t stream)const;
    Message consumed(std::uint64_t stream,std::size_t count);
    void credited(const Message&);
    bool close(std::uint64_t stream)noexcept;
    std::vector<std::uint64_t> revoke(std::uint64_t grant);
    std::vector<std::uint64_t> ids()const;
    std::size_t queued()const noexcept{return queued_;}
    bool contains(std::uint64_t stream)const{return streams_.contains(stream);}
};
}
