#include <dm/input_protocol.hpp>
#include <algorithm>
#include <bit>
#include <stdexcept>
namespace dm::input {
namespace {
void put(std::span<std::uint8_t> out, unsigned offset, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) out[offset + i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
}
std::uint32_t get(std::span<const std::uint8_t> in, unsigned offset) {
    std::uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) v = (v << 8) | in[offset + i];
    return v;
}
void need(bool ok) {
    if (!ok) throw std::runtime_error("Invalid input packet or event");
}
}
unsigned capability(Kind kind) {
    switch (kind) {
    case Kind::reset:
        return 0;
    case Kind::mouse:
        return mouse_cap;
    case Kind::key:
        return keyboard_cap;
    case Kind::touch:
        return touch_cap;
    case Kind::pen:
        return pen_cap;
    default:
        throw std::runtime_error("Unknown input family");
    }
}
void validate(const Event& e) {
    (void)capability(e.kind);
    need(e.x <= 65535 && e.y <= 65535 && e.flags <= 3 && e.pressure <= 1024 && e.tilt_x >= -90 && e.tilt_x <= 90 &&
         e.tilt_y >= -90 && e.tilt_y <= 90 && e.rotation <= 359 && e.contact_x <= 65535 && e.contact_y <= 65535);
    need(static_cast<unsigned>(e.action) <= static_cast<unsigned>(Action::wheel));
    if (e.kind == Kind::reset)
        need(e.action == Action::move && !e.flags && !e.id && !e.x && !e.y && !e.code && !e.value && !e.mask);
    if (e.kind == Kind::key)
        need((e.action == Action::down || e.action == Action::up) && e.id >= 1 && e.id <= 255 && e.code >= 1 &&
             e.code <= 255 && e.value >= 0 && e.value <= 1 && !e.flags && !e.x && !e.y && !e.mask);
    if (e.kind == Kind::mouse)
        need(!e.flags && !e.mask && !e.id &&
             ((e.action == Action::move && !e.code && !e.value) ||
              ((e.action == Action::down || e.action == Action::up) && e.code >= 1 && e.code <= 5 && !e.value) ||
              (e.action == Action::wheel && e.code <= 1 && e.value >= -32767 && e.value <= 32767)));
    if (e.kind == Kind::touch || e.kind == Kind::pen)
        need(e.id >= 1 && e.id <= 65535 && e.action != Action::wheel && !e.value);
    if (e.kind == Kind::touch || e.kind == Kind::pen) {
        need(!(e.flags & 2) || (e.flags & 1));
        if (e.action == Action::down) need(e.flags == 3);
        if (e.action == Action::up || e.action == Action::cancel) need(!(e.flags & 2));
        if (e.kind == Kind::touch && e.action == Action::move) need(e.flags == 3);
    }
    if (e.kind == Kind::touch) need(e.mask <= 7 && !e.code && !e.tilt_x && !e.tilt_y);
    if (e.kind == Kind::pen) need(e.mask <= 15 && e.code <= 7 && !e.contact_x && !e.contact_y);
    if (e.kind != Kind::touch && e.kind != Kind::pen)
        need(!e.pressure && !e.tilt_x && !e.tilt_y && !e.rotation && !e.contact_x && !e.contact_y);
}
HeaderBytes encode_header(Header h) {
    need(h.epoch && h.count <= max_events);
    HeaderBytes out{'D', 'M', 'I', 'N'};
    put(out, 4, 1);
    put(out, 8, static_cast<std::uint32_t>(h.epoch >> 32));
    put(out, 12, static_cast<std::uint32_t>(h.epoch));
    put(out, 16, h.sequence);
    put(out, 20, h.count);
    put(out, 24, h.count * event_size);
    return out;
}
Header decode_header(std::span<const std::uint8_t> b) {
    need(b.size() == header_size && b[0] == 'D' && b[1] == 'M' && b[2] == 'I' && b[3] == 'N' && get(b, 4) == 1 &&
         get(b, 28) == 0);
    Header h{(static_cast<std::uint64_t>(get(b, 8)) << 32) | get(b, 12), get(b, 16), get(b, 20)};
    need(h.epoch && h.count <= max_events && get(b, 24) == h.count * event_size);
    return h;
}
std::vector<std::uint8_t> encode_events(std::span<const Event> events) {
    need(events.size() <= max_events);
    std::vector<std::uint8_t> out(events.size() * event_size);
    for (unsigned i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        validate(e);
        auto b = std::span(out).subspan(i * event_size, event_size);
        b[0] = static_cast<std::uint8_t>(e.kind);
        b[1] = static_cast<std::uint8_t>(e.action);
        b[2] = static_cast<std::uint8_t>(e.flags >> 8);
        b[3] = static_cast<std::uint8_t>(e.flags);
        const std::uint32_t fields[] = {e.id,
                                        e.x,
                                        e.y,
                                        e.code,
                                        std::bit_cast<std::uint32_t>(e.value),
                                        e.pressure,
                                        std::bit_cast<std::uint32_t>(e.tilt_x),
                                        std::bit_cast<std::uint32_t>(e.tilt_y),
                                        e.rotation,
                                        e.mask,
                                        e.contact_x,
                                        e.contact_y,
                                        0,
                                        static_cast<std::uint32_t>(e.micros >> 32),
                                        static_cast<std::uint32_t>(e.micros)};
        for (unsigned n = 0; n < 15; ++n) put(b, 4 + 4 * n, fields[n]);
    }
    return out;
}
std::vector<Event> decode_events(std::span<const std::uint8_t> bytes, unsigned count) {
    need(count <= max_events && bytes.size() == count * event_size);
    std::vector<Event> result;
    for (unsigned i = 0; i < count; ++i) {
        const auto b = bytes.subspan(i * event_size, event_size);
        need(get(b, 52) == 0);
        Event e{static_cast<Kind>(b[0]),
                static_cast<Action>(b[1]),
                static_cast<std::uint16_t>((b[2] << 8) | b[3]),
                get(b, 4),
                get(b, 8),
                get(b, 12),
                get(b, 16),
                std::bit_cast<std::int32_t>(get(b, 20)),
                get(b, 24),
                std::bit_cast<std::int32_t>(get(b, 28)),
                std::bit_cast<std::int32_t>(get(b, 32)),
                get(b, 36),
                get(b, 40),
                get(b, 44),
                get(b, 48),
                (static_cast<std::uint64_t>(get(b, 56)) << 32) | get(b, 60)};
        validate(e);
        result.push_back(e);
    }
    return result;
}
}
