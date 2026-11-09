#pragma once
#include "wire.hpp"
#include "capabilities.hpp"
#include <functional>

namespace extrapane::wire {
using Bytes = std::vector<std::uint8_t>;
struct Message { Header header; Bytes payload; };
// Callbacks transfer the entire span or throw; the native transport enforces
// deadlines/cancellation. Supply an ALREADY admitted encrypted stream only.
class ControlChannel final {
public:
    using Read = std::function<void(std::span<std::uint8_t>)>;
    using Write = std::function<void(std::span<const std::uint8_t>)>;
    ControlChannel(Read read, Write write);
    Message receive();
    void send(const Message& message);
    bool failed() const noexcept { return failed_; }
private:
    Read read_; Write write_; bool failed_=false;
};
// Default is byte-compatible with existing four-resource peers. Only a new
// speaker session opts into the fifth capability; legacy sessions keep v1 layout.
enum class CapabilityFormat { legacy_four, with_speakers };
Bytes encode_capabilities(const Capabilities& capabilities,CapabilityFormat format=CapabilityFormat::legacy_four);
Capabilities decode_capabilities(std::span<const std::uint8_t> bytes);
Bytes encode_request(const ResourceRequest& request);
ResourceRequest decode_request(const Header& header, std::span<const std::uint8_t> bytes);
}
