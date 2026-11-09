#pragma once
#include "media.hpp"
namespace extrapane::microphone_wire {
// Initial native slice is PCM16 LE, 48 kHz mono, 10 ms/packet. Not Opus.
using OfferBytes=std::array<std::uint8_t,32>;
OfferBytes encode_offer(media::Binding binding);
media::Binding decode_offer(std::span<const std::uint8_t> bytes);
constexpr std::size_t sample_count=480,payload_size=960;
struct Frame {
    media::Binding binding;
    std::uint64_t sequence=0,position=0;
    bool muted=false,discontinuity=false;
    std::array<std::uint8_t,payload_size> pcm{};
};
using Header=std::array<std::uint8_t,40>;
Header encode_header(const Frame& frame);
Frame decode_header(std::span<const std::uint8_t> bytes);
class Channel {
public:
    Channel(media::Binding binding,wire::ControlChannel::Read read,wire::ControlChannel::Write write);
    void send(const Frame& frame);
    Frame receive();
private:
    void validate(const Frame& frame,std::uint64_t sequence)const;
    media::Binding binding_;wire::ControlChannel::Read read_;wire::ControlChannel::Write write_;
    std::uint64_t sent_=0,received_=0;bool failed_=false;
};
}
