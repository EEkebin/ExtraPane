#pragma once
#include <dm/network.hpp>
#include <dm/microphone_endpoint.hpp>
#include <atomic>
#include <functional>
namespace dm::security {
class TlsStream;
}
namespace dm::microphone {
using Report = std::function<void(const std::string&)>;
// Dedicated PCM microphone-only mode. Configure TLS access-code policy first.
// This is not the legacy display listener; do not reuse its occupied port.
// The listener allocates a distinct 1-based slot per active receiving stream.
// Slot identity is preserved through native endpoint launch and readiness.
void receive_admitted(security::TlsStream&, std::atomic_bool& stop, const Report& report, unsigned slot = 1);
void serve(const net::Options& options, std::atomic_bool& stop, const Report& report);
void send(const net::Options& options, const std::wstring& local_device, std::atomic_bool& stop, std::atomic_bool& mute,
          const Report& report);
// One locally selected capture shared by bounded, independent network streams.
// This object is inert until an admitted recipient is ready. Credentials are
// supplied through local stdin by the CLI; they are never process arguments.
class SourceBroker final {
public:
    SourceBroker(std::wstring device, std::atomic_bool& stop, Report report);
    ~SourceBroker();
    SourceBroker(const SourceBroker&) = delete;
    SourceBroker& operator=(const SourceBroker&) = delete;
    void add(std::string row, net::Options options, std::string credential);
    void remove(const std::string& row);
    void mute(const std::string& row, bool value);

private:
    struct State;
    std::unique_ptr<State> state_;
};
}
