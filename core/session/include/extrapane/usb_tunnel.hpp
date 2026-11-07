#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace extrapane::usb::tunnel {
inline constexpr std::size_t maximum_payload=65536, capacity=262144, header_size=24;
struct Frame {std::uint64_t sequence=0;std::uint32_t credit=0;bool stop=false;std::vector<std::uint8_t> payload;};
using Header=std::array<std::uint8_t,header_size>;
Header encode(const Frame& frame);
Frame decode(const Header& header); // Returns validated metadata; caller reads length bytes into payload.
class Pump final {
public:
    // exporter_bus gates the first raw USB/IP operation. Empty is only for the
    // importer, which receives replies from its selected exporter.
    explicit Pump(std::string exporter_bus={});
    std::size_t read_limit()const;
    Frame next(std::span<const std::uint8_t> bytes={},bool stop=false);
    void accept(Frame frame);
    std::span<const std::uint8_t> pending()const;
    void consume(std::size_t count);
    bool stopped()const{return stopped_;}
private:
    std::vector<std::uint8_t> queue_;
    std::size_t offset_=0;
    std::uint64_t sent_=0,received_=0;
    std::uint32_t peer_credit_=capacity,advertised_=capacity;
    std::string bus_;
    bool gate_open_=false,stopped_=false;
};
}
