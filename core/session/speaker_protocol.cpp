#include <extrapane/speaker_protocol.hpp>
#include <limits>
#include <stdexcept>
namespace extrapane::speaker_wire {
namespace {
void put(std::span<std::uint8_t> b, unsigned o, std::uint64_t v, unsigned n) {
    for (unsigned i = 0; i < n; ++i) b[o + n - i - 1] = static_cast<std::uint8_t>(v >> (i * 8));
}
std::uint64_t get(std::span<const std::uint8_t> b, unsigned o, unsigned n) {
    std::uint64_t v = 0;
    for (unsigned i = 0; i < n; ++i) v = (v << 8) | b[o + i];
    return v;
}
void binding(media::Binding value) {
    if (!value.resource || !value.generation)
        throw std::invalid_argument("Speaker needs a nonzero resource generation");
}
}
OfferBytes encode_offer(media::Binding value) {
    binding(value);
    OfferBytes b{};
    b[0] = 'E';
    b[1] = 'P';
    b[2] = 'S';
    b[3] = 'O';
    b[5] = 1;
    b[6] = 1;
    b[7] = 2;
    put(b, 8, 48000, 4);
    put(b, 12, sample_count, 2);
    put(b, 16, value.resource, 8);
    put(b, 24, value.generation, 8);
    return b;
}
media::Binding decode_offer(std::span<const std::uint8_t> b) {
    if (b.size() != 32 || b[0] != 'E' || b[1] != 'P' || b[2] != 'S' || b[3] != 'O' || get(b, 4, 2) != 1 || b[6] != 1 ||
        b[7] != 2 || get(b, 8, 4) != 48000 || get(b, 12, 2) != sample_count || get(b, 14, 2) != 0)
        throw std::invalid_argument("Unsupported speaker offer (requires PCM16 stereo 48 kHz)");
    media::Binding value{get(b, 16, 8), get(b, 24, 8)};
    binding(value);
    return value;
}
Header encode_header(const Frame& f) {
    binding(f.binding);
    Header b{};
    b[0] = 'E';
    b[1] = 'P';
    b[2] = 'S';
    b[3] = 'D';
    b[5] = 1;
    b[7] = (f.muted ? 1 : 0) | (f.discontinuity ? 2 : 0);
    put(b, 8, f.binding.resource, 8);
    put(b, 16, f.binding.generation, 8);
    put(b, 24, f.sequence, 8);
    put(b, 32, f.position, 8);
    return b;
}
Frame decode_header(std::span<const std::uint8_t> b) {
    if (b.size() != 40 || b[0] != 'E' || b[1] != 'P' || b[2] != 'S' || b[3] != 'D' || get(b, 4, 2) != 1 || b[6] ||
        b[7] > 3)
        throw std::invalid_argument("Invalid speaker frame header");
    Frame f;
    f.binding = {get(b, 8, 8), get(b, 16, 8)};
    binding(f.binding);
    f.sequence = get(b, 24, 8);
    f.position = get(b, 32, 8);
    f.muted = (b[7] & 1) != 0;
    f.discontinuity = (b[7] & 2) != 0;
    return f;
}
Channel::Channel(media::Binding binding, wire::ControlChannel::Read read, wire::ControlChannel::Write write)
    : binding_(binding), read_(std::move(read)), write_(std::move(write)) {
    if (!binding.resource || !binding.generation || !read_ || !write_)
        throw std::invalid_argument("Invalid speaker channel");
}
void Channel::validate(const Frame& f, std::uint64_t sequence) const {
    if (failed_ || f.binding != binding_ || f.sequence != sequence ||
        sequence == std::numeric_limits<std::uint64_t>::max())
        throw std::invalid_argument("Speaker frame binding or sequence mismatch");
}
void Channel::send(const Frame& f) {
    try {
        validate(f, sent_);
        if (f.position > std::numeric_limits<std::uint64_t>::max() - sample_count ||
            (sent_ && (f.position < sent_position_ || (!f.discontinuity && f.position != sent_position_))))
            throw std::invalid_argument("Speaker sample position mismatch");
        write_(encode_header(f));
        if (f.muted) {
            const std::array<std::uint8_t, payload_size> silence{};
            write_(silence);
        } else
            write_(f.pcm);
        ++sent_;
        sent_position_ = f.position + sample_count;
    } catch (...) {
        failed_ = true;
        throw;
    }
}
Frame Channel::receive() {
    try {
        if (failed_) throw std::runtime_error("Speaker channel failed");
        Header h{};
        read_(h);
        auto f = decode_header(h);
        validate(f, received_);
        if (f.position > std::numeric_limits<std::uint64_t>::max() - sample_count ||
            (received_ && (f.position < received_position_ || (!f.discontinuity && f.position != received_position_))))
            throw std::invalid_argument("Speaker sample position mismatch");
        read_(f.pcm);
        if (f.muted) f.pcm.fill(0);
        ++received_;
        received_position_ = f.position + sample_count;
        return f;
    } catch (...) {
        failed_ = true;
        throw;
    }
}
}
