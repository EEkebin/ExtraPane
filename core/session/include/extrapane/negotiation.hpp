#pragma once
#include "channel.hpp"
#include "server.hpp"

namespace extrapane {
// Runs after transport admission, on the server executor. Peer identifiers are
// metadata only: factories receive a resource KIND, never a peer-controlled path.
class Negotiation final {
public:
    using Factory = std::function<std::unique_ptr<Resource>(ResourceKind)>;
    Negotiation(Server& server, std::uint64_t admitted_id, Capabilities local, Factory factory,
                wire::CapabilityFormat format = wire::CapabilityFormat::legacy_four);
    wire::Message handle(const wire::Message& message);
    // Process one message; read/decrypt/write failures revoke this session.
    void serve_one(wire::ControlChannel& channel);

private:
    Server& server_;
    std::uint64_t id_;
    Capabilities local_, peer_ = unavailable_capabilities();
    wire::CapabilityFormat format_;
    Factory factory_;
    bool exchanged_ = false;
};
}
