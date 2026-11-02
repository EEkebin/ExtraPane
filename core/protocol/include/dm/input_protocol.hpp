#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>
namespace dm::input {
constexpr unsigned mouse_cap=1, keyboard_cap=2, touch_cap=4, pen_cap=8, all_caps=15;
constexpr unsigned max_events=128, event_size=64, header_size=32;
enum class Kind:std::uint8_t { reset, mouse, key, touch, pen };
enum class Action:std::uint8_t { move, down, up, cancel, wheel };
struct Event {
    Kind kind=Kind::reset; Action action=Action::move;
    std::uint16_t flags=0; // bit 0 in range, bit 1 in contact
    std::uint32_t id=0,x=0,y=0,code=0;
    std::int32_t value=0;
    std::uint32_t pressure=0;
    std::int32_t tilt_x=0,tilt_y=0;
    std::uint32_t rotation=0,mask=0,contact_x=0,contact_y=0;
    std::uint64_t micros=0;
};
struct Header { std::uint64_t epoch; std::uint32_t sequence,count; };
using HeaderBytes=std::array<std::uint8_t,header_size>;
HeaderBytes encode_header(Header header);
Header decode_header(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> encode_events(std::span<const Event> events);
std::vector<Event> decode_events(std::span<const std::uint8_t> bytes,unsigned count);
unsigned capability(Kind kind);
void validate(const Event& event);
}
