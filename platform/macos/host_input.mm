#include "host_input.hpp"
#include "keys.hpp"
#include <map>
#include <mutex>
#include <cmath>
#include <stdexcept>
namespace dm::mac {
namespace {
std::recursive_mutex inputMutex;
std::map<unsigned, std::set<unsigned>> keyOwners, buttonOwners;
CGEventFlags flags() {
    CGEventFlags result = CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState);
    for (auto& [key, owners] : keyOwners)
        if (!owners.empty()) switch (key) {
            case 56:
            case 60:
                result |= kCGEventFlagMaskShift;
                break;
            case 59:
            case 62:
                result |= kCGEventFlagMaskControl;
                break;
            case 58:
            case 61:
                result |= kCGEventFlagMaskAlternate;
                break;
            case 54:
            case 55:
                result |= kCGEventFlagMaskCommand;
                break;
            }
    return result;
}
void post(CGEventRef event) {
    if (!event) throw std::runtime_error("Cannot allocate remote input event");
    CGEventSetIntegerValueField(event, kCGEventSourceUserData, 0x4558504e);
    CGEventSetFlags(event, flags());
    CGEventPost(kCGSessionEventTap, event);
    CFRelease(event);
}
CGEventType mouseType(unsigned button, bool down) {
    return button == 1   ? (down ? kCGEventLeftMouseDown : kCGEventLeftMouseUp)
           : button == 2 ? (down ? kCGEventRightMouseDown : kCGEventRightMouseUp)
                         : (down ? kCGEventOtherMouseDown : kCGEventOtherMouseUp);
}
}
HostInput::HostInput(unsigned slot, CGDirectDisplayID display) : slot_(slot), display_(display) {
    source_ = CGEventSourceCreate(kCGEventSourceStatePrivate);
    if (!source_) throw std::runtime_error("Cannot create input source");
}
HostInput::~HostInput() {
    try {
        reset();
    } catch (...) {}
    if (source_) CFRelease(source_);
}
void HostInput::reset() {
    std::lock_guard lock(inputMutex);
    for (auto key : keys_) {
        auto& owners = keyOwners[key];
        owners.erase(slot_);
        if (owners.empty() && !CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, key))
            post(CGEventCreateKeyboardEvent(source_, key, false));
    }
    keys_.clear();
    for (auto button : buttons_) {
        auto& owners = buttonOwners[button];
        owners.erase(slot_);
        if (owners.empty() && !CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, CGMouseButton(button - 1)))
            post(CGEventCreateMouseEvent(source_, mouseType(button, false), last_, CGMouseButton(button - 1)));
    }
    buttons_.clear();
}
void HostInput::inject(const input::Event& event) {
    std::lock_guard lock(inputMutex);
    input::validate(event);
    using input::Action;
    using input::Kind;
    if (event.kind == Kind::reset) {
        reset();
        return;
    }
    if (event.kind == Kind::key) {
        unsigned mac = mapHostKey(event.code, unsigned(event.value));
        if (mac == 256) return;
        bool down = event.action == Action::down;
        bool repeat = keys_.contains(mac);
        if (down) {
            if (!repeat && CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, mac) && keyOwners[mac].empty())
                return;
            keys_.insert(mac);
            keyOwners[mac].insert(slot_);
        } else {
            if (!keys_.erase(mac)) return;
            keyOwners[mac].erase(slot_);
            if (!keyOwners[mac].empty() || CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, mac)) return;
        }
        auto e = CGEventCreateKeyboardEvent(source_, mac, down);
        if (e) CGEventSetIntegerValueField(e, kCGKeyboardEventAutorepeat, repeat && down);
        post(e);
        return;
    }
    if (event.kind != Kind::mouse) throw std::runtime_error("Mac host does not support native touch/pen injection");
    auto rect = CGDisplayBounds(display_);
    if (rect.size.width <= 0 || rect.size.height <= 0) throw std::runtime_error("Input monitor disappeared");
    last_ = CGPointMake(rect.origin.x + event.x / 65535.0 * (rect.size.width - 1),
                        rect.origin.y + event.y / 65535.0 * (rect.size.height - 1));
    if (event.action == Action::wheel) {
        post(CGEventCreateScrollWheelEvent(source_, kCGScrollEventUnitPixel, 2, event.code == 0 ? event.value / 3 : 0,
                                           event.code == 1 ? event.value / 3 : 0));
        return;
    }
    unsigned button = event.code;
    CGEventType type = kCGEventMouseMoved;
    if (event.action == Action::down) {
        if (CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, CGMouseButton(button - 1)) &&
            buttonOwners[button].empty())
            return;
        buttons_.insert(button);
        buttonOwners[button].insert(slot_);
        type = mouseType(button, true);
        auto now = std::chrono::steady_clock::now();
        click_ = (button == lastButton_ && now - lastClick_ < std::chrono::milliseconds(500) &&
                  std::abs(last_.x - clickPoint_.x) < 4 && std::abs(last_.y - clickPoint_.y) < 4)
                     ? click_ + 1
                     : 1;
        lastClick_ = now;
        lastButton_ = button;
        clickPoint_ = last_;
    } else if (event.action == Action::up) {
        if (!buttons_.erase(button)) return;
        buttonOwners[button].erase(slot_);
        if (!buttonOwners[button].empty() ||
            CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, CGMouseButton(button - 1)))
            return;
        type = mouseType(button, false);
    } else if (!buttons_.empty()) {
        button = *buttons_.begin();
        type = button == 1   ? kCGEventLeftMouseDragged
               : button == 2 ? kCGEventRightMouseDragged
                             : kCGEventOtherMouseDragged;
    }
    auto e = CGEventCreateMouseEvent(source_, type, last_, CGMouseButton(button ? button - 1 : 0));
    if (e && event.action != Action::move) CGEventSetIntegerValueField(e, kCGMouseEventClickState, click_);
    post(e);
}
}
