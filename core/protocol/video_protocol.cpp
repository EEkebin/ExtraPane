#include <dm/video_protocol.hpp>
#include <algorithm>
#include <stdexcept>

namespace dm::video::wire {
namespace {
void put32(std::span<std::uint8_t> out, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out[offset + i] = static_cast<std::uint8_t>(value >> ((3 - i) * 8));
}
std::uint32_t get32(std::span<const std::uint8_t> in, std::size_t offset) {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) result = (result << 8) | in[offset + i];
    return result;
}
void validate(const Offer& offer) {
    if(offer.codec==Codec::jpeg && (offer.width>1280 || offer.height>720))throw std::runtime_error("JPEG is limited to 1280x720");
    if (!offer.width || offer.width > 4096 || !offer.height || offer.height > 4096 || static_cast<std::uint64_t>(offer.width)*offer.height > 8847360 ||
        !offer.fps || offer.fps > 60 || offer.frames > 36000) throw std::runtime_error("Video offer exceeds supported limits");
    if (offer.codec!=Codec::jpeg && offer.codec!=Codec::h264) throw std::runtime_error("Unsupported video codec");
    if (offer.codec==Codec::h264 && (offer.width%2 || offer.height%2)) throw std::runtime_error("H264 requires even video dimensions");
    if(offer.input_caps>15 || (offer.input_caps!=0)!=(offer.input_epoch!=0))throw std::runtime_error("Invalid input grant");
}
}
void set_client_display(std::span<std::uint8_t> bytes, ClientDisplay display) {
    if(bytes.size()!=request.size() || ((display.width||display.height||display.hz) &&
       (!display.width || !display.height || !display.hz || display.width>16384 || display.height>16384 || display.hz>1000)))
        throw std::runtime_error("Invalid client display preference");
    if(display.exact_mode && (!display.width || display.width>4096 || display.height>4096 || display.width%2 || display.height%2 || display.hz>60 || static_cast<std::uint64_t>(display.width)*display.height>8847360))
        throw std::runtime_error("Custom display mode exceeds supported limits");
    put32(bytes,16,display.width);put32(bytes,20,display.height);put32(bytes,24,display.hz);bytes[28]=display.exact_mode?1:0;
}
ClientDisplay take_client_display(std::span<std::uint8_t> bytes) {
    if(bytes.size()!=request.size())throw std::runtime_error("Invalid request size");
    if(bytes[28]>1)throw std::runtime_error("Unknown client display mode flags");
    ClientDisplay result{get32(bytes,16),get32(bytes,20),get32(bytes,24),bytes[28]==1};
    set_client_display(bytes,result);std::fill(bytes.begin()+16,bytes.begin()+29,std::uint8_t{0});return result;
}
OfferBytes encode_offer(const Offer& offer) {
    validate(offer);
    OfferBytes out{'D','M','V','O',9,1,static_cast<std::uint8_t>(offer.codec),offer.input_caps};
    put32(out, 8, offer.width); put32(out, 12, offer.height);
    put32(out, 16, offer.fps); put32(out, 20, offer.frames);
    put32(out,24,static_cast<std::uint32_t>(offer.input_epoch>>32));put32(out,28,static_cast<std::uint32_t>(offer.input_epoch));
    return out;
}
Offer decode_offer(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 32 || bytes[0] != 'D' || bytes[1] != 'M' || bytes[2] != 'V' || bytes[3] != 'O' ||
        bytes[4] != 9 || bytes[5] != 1) throw std::runtime_error("Invalid or unsupported video offer (update both PCs)");
    Offer result{get32(bytes, 8), get32(bytes, 12), get32(bytes, 16), get32(bytes, 20),static_cast<Codec>(bytes[6]),bytes[7],(static_cast<std::uint64_t>(get32(bytes,24))<<32)|get32(bytes,28)};
    validate(result);
    return result;
}
HeaderBytes encode_header(const FrameHeader& header) {
    if (header.bytes > max_jpeg) throw std::runtime_error("Video payload length exceeds limit");
    HeaderBytes out{'D','M','V','F'};
    put32(out, 4, header.sequence); put32(out, 8, header.bytes);
    put32(out, 12, static_cast<std::uint32_t>(header.micros >> 32));
    put32(out, 16, static_cast<std::uint32_t>(header.micros));put32(out,20,header.editable_touch);
    return out;
}
FrameHeader decode_header(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 24 || bytes[0] != 'D' || bytes[1] != 'M' || bytes[2] != 'V' || bytes[3] != 'F') throw std::runtime_error("Invalid video frame header");
    FrameHeader result{get32(bytes, 4), get32(bytes, 8), (static_cast<std::uint64_t>(get32(bytes, 12)) << 32) | get32(bytes, 16),get32(bytes,20)};
    if (result.bytes > max_jpeg) throw std::runtime_error("Video payload length exceeds limit");
    return result;
}
}
