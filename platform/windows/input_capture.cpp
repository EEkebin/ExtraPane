#include <dm/branding.hpp>
#include <dm/input.hpp>
#include <dm/video_layout.hpp>
#include <windowsx.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <map>
namespace dm::input {
unsigned receiver_capabilities() {
    const auto digitizer = GetSystemMetrics(SM_DIGITIZER);
    return mouse_cap | keyboard_cap | ((digitizer & (NID_INTEGRATED_TOUCH | NID_EXTERNAL_TOUCH)) ? touch_cap : 0) |
           ((digitizer & (NID_INTEGRATED_PEN | NID_EXTERNAL_PEN)) ? pen_cap : 0);
}
struct ReceiverInput::State {
    bool requested = false, paused = false, focused = false;
    std::atomic<unsigned> caps{0};
    std::atomic<bool> reset_pending{false};
    unsigned width = 0, height = 0, buttons = 0;
    HWND owner_window = nullptr;
    std::set<unsigned> keys;
    std::map<UINT32, Event> pointers;
    std::mutex mutex;
    std::vector<Event> queue;
    using Clock = std::chrono::steady_clock;
    Clock::time_point start = Clock::now(), last_pump = start;
    explicit State(bool enabled) : requested(enabled) {}
    void push(Event event) {
        std::lock_guard lock(mutex);
        event.micros = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
        if (queue.size() >= max_events) {
            queue.clear();
            queue.push_back(Event{});
            paused = true;
            keys.clear();
            pointers.clear();
            buttons = 0;
            return;
        }
        if (event.kind == Kind::mouse && event.action == Action::move && !queue.empty() &&
            queue.back().kind == Kind::mouse && queue.back().action == Action::move)
            queue.back() = event;
        else
            queue.push_back(event);
    }
    void reset() {
        keys.clear();
        pointers.clear();
        buttons = 0;
        if (owner_window && GetCapture() == owner_window) ReleaseCapture();
        {
            std::lock_guard lock(mutex);
            queue.clear();
        }
        push(Event{});
    }
    bool position(HWND window, POINT p, Event& event, bool clamp) {
        RECT area{};
        GetClientRect(window, &area);
        if (area.right < 2 || area.bottom < 2 || !width || !height) return false;
        const auto rect =
            video::content_rect(width, height, static_cast<unsigned>(area.right), static_cast<unsigned>(area.bottom));
        p.x -= static_cast<LONG>(rect.x);
        p.y -= static_cast<LONG>(rect.y);
        const auto w = static_cast<LONG>(rect.width), h = static_cast<LONG>(rect.height);
        if (w < 2 || h < 2 || (!clamp && (p.x < 0 || p.y < 0 || p.x >= w || p.y >= h))) return false;
        event.x = static_cast<unsigned>(static_cast<std::int64_t>(std::clamp(p.x, 0L, w - 1)) * 65535 / (w - 1));
        event.y = static_cast<unsigned>(static_cast<std::int64_t>(std::clamp(p.y, 0L, h - 1)) * 65535 / (h - 1));
        return true;
    }
    void pointer(HWND window, UINT message, UINT32 id) {
        if (message == WM_POINTERCAPTURECHANGED) {
            const auto found = pointers.find(id);
            if (found != pointers.end() && (found->second.flags & 2)) {
                auto cancel = found->second;
                cancel.action = Action::cancel;
                cancel.flags = 0;
                pointers.erase(found);
                push(cancel);
            }
            return;
        }
        POINTER_INPUT_TYPE type{};
        if (!GetPointerType(id, &type) || (type != PT_TOUCH && type != PT_PEN)) return;
        if (!(caps.load() & (type == PT_PEN ? pen_cap : touch_cap))) return;
        auto sample = [&](const POINTER_INFO& p, Event event) {
            event.id = id;
            event.kind = type == PT_PEN ? Kind::pen : Kind::touch;
            const auto f = p.pointerFlags;
            event.flags = static_cast<std::uint16_t>(((f & POINTER_FLAG_INRANGE) ? 1 : 0) |
                                                     ((f & POINTER_FLAG_INCONTACT) ? 2 : 0));
            event.action =
                (f & POINTER_FLAG_CANCELED)
                    ? Action::cancel
                    : ((f & POINTER_FLAG_DOWN) ? Action::down : ((f & POINTER_FLAG_UP) ? Action::up : Action::move));
            if (message == WM_POINTERENTER) {
                if (type == PT_TOUCH || (event.flags & 2)) return;
                event.action = Action::move;
            }
            if (message == WM_POINTERLEAVE) {
                event.flags = 0;
                event.action = (f & POINTER_FLAG_INCONTACT) ? Action::cancel : Action::move;
            }
            POINT point = p.ptPixelLocation;
            ScreenToClient(window, &point);
            if (!position(window, point, event, pointers.contains(id))) return;
            if (type == PT_TOUCH && event.action == Action::move && !(event.flags & 2)) return;
            if (event.action == Action::move && (event.flags & 2) && !pointers.contains(id)) return;
            if (event.action == Action::down) SetFocus(window);
            if (event.flags)
                pointers[id] = event;
            else
                pointers.erase(id);
            push(event);
        };
        if (type == PT_PEN) {
            // Windows returns most recent first; send actual coalesced samples oldest first.
            UINT32 count = 64;
            POINTER_PEN_INFO history[64]{};
            if (message == WM_POINTERUPDATE && GetPointerPenInfoHistory(id, &count, history)) {
                for (UINT32 i = count; i > 0; --i) {
                    const auto& p = history[i - 1];
                    Event e;
                    e.code = p.penFlags;
                    e.mask = p.penMask;
                    e.pressure = p.pressure;
                    e.rotation = p.rotation;
                    e.tilt_x = p.tiltX;
                    e.tilt_y = p.tiltY;
                    sample(p.pointerInfo, e);
                }
            } else {
                POINTER_PEN_INFO p{};
                if (GetPointerPenInfo(id, &p)) {
                    Event e;
                    e.code = p.penFlags;
                    e.mask = p.penMask;
                    e.pressure = p.pressure;
                    e.rotation = p.rotation;
                    e.tilt_x = p.tiltX;
                    e.tilt_y = p.tiltY;
                    sample(p.pointerInfo, e);
                }
            }
        } else {
            POINTER_TOUCH_INFO p{};
            if (!GetPointerTouchInfo(id, &p)) return;
            RECT area{};
            GetClientRect(window, &area);
            const auto content = video::content_rect(width, height, static_cast<unsigned>(std::max(1L, area.right)),
                                                     static_cast<unsigned>(std::max(1L, area.bottom)));
            Event e;
            e.mask = p.touchMask;
            e.pressure = p.pressure;
            e.rotation = p.orientation;
            e.contact_x = static_cast<unsigned>(std::clamp<std::int64_t>(
                static_cast<std::int64_t>(p.rcContact.right - p.rcContact.left) * 65535 / std::max(1u, content.width),
                0, 65535));
            e.contact_y = static_cast<unsigned>(std::clamp<std::int64_t>(
                static_cast<std::int64_t>(p.rcContact.bottom - p.rcContact.top) * 65535 / std::max(1u, content.height),
                0, 65535));
            sample(p.pointerInfo, e);
        }
    }
};
ReceiverInput::ReceiverInput(bool enabled) : state_(std::make_unique<State>(enabled)) {}
ReceiverInput::~ReceiverInput() = default;
void ReceiverInput::grant(unsigned caps) {
    state_->caps.store(state_->requested ? caps : 0);
    state_->reset_pending.store(true);
}
void ReceiverInput::dimensions(unsigned width, unsigned height) {
    state_->width = width;
    state_->height = height;
    if (state_->owner_window && state_->caps.load())
        SetPropW(state_->owner_window, L"DM.InputReady",
                 reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(state_->caps.load())));
}
void ReceiverInput::pump() {
    if (state_->reset_pending.exchange(false)) state_->reset();
    std::lock_guard lock(state_->mutex);
    state_->last_pump = State::Clock::now();
}
std::vector<Event> ReceiverInput::drain() {
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    if (s.reset_pending.load()) {
        s.queue.clear();
        return {Event{}};
    }
    if (State::Clock::now() - s.last_pump > std::chrono::milliseconds(500)) {
        s.queue.clear();
        return {Event{}};
    }
    std::vector<Event> out;
    out.swap(s.queue);
    return out;
}
bool ReceiverInput::message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto& s = *state_;
    if (!s.requested) return false;
    s.owner_window = window;
    if (message == WM_SETFOCUS) {
        s.focused = true;
        return false;
    }
    if (message == WM_KILLFOCUS || message == WM_DESTROY || (message == WM_CAPTURECHANGED && s.buttons)) {
        s.reset();
        s.focused = GetFocus() == window;
        return false;
    }
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_KEYUP || message == WM_SYSKEYUP) &&
        wparam == VK_F8) {
        if (message == WM_KEYDOWN && !(lparam & (1LL << 30))) {
            s.reset();
            s.paused = !s.paused;
            SetWindowTextW(window, s.paused ? branding::display_paused_title : branding::display_title);
        }
        return true;
    }
    if (message == WM_POINTERDOWN && s.caps.load() && !s.paused) {
        SetFocus(window);
        s.focused = true;
    }
    if (!s.caps.load() || !s.focused || s.paused) {
        if (s.paused && GetCapture() == window) ReleaseCapture();
        return false;
    }
    if (message == WM_POINTERDOWN || message == WM_POINTERUPDATE || message == WM_POINTERUP ||
        message == WM_POINTERENTER || message == WM_POINTERLEAVE || message == WM_POINTERCAPTURECHANGED) {
        s.pointer(window, message, GET_POINTERID_WPARAM(wparam));
        return true;
    }
    if ((message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) &&
        (s.caps.load() & keyboard_cap)) {
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const auto scan = static_cast<unsigned>((lparam >> 16) & 255);
        const bool extended = (lparam & (1LL << 24)) != 0;
        if (!scan) return true;
        unsigned vk = MapVirtualKeyW(scan | (extended ? 0xe000 : 0), MAPVK_VSC_TO_VK_EX);
        if (!vk) vk = static_cast<unsigned>(wparam);
        if (down) {
            if ((lparam & (1LL << 30)) && !s.keys.contains(vk)) return true;
            s.keys.insert(vk);
        } else {
            if (!s.keys.erase(vk)) return true;
        }
        Event e;
        e.kind = Kind::key;
        e.action = down ? Action::down : Action::up;
        e.id = vk;
        e.code = scan;
        e.value = extended ? 1 : 0;
        s.push(e);
        return true;
    }
    if (message == WM_CHAR || message == WM_SYSCHAR)
        return true; // Physical scancodes only; no duplicate text injection.
    if (!(s.caps.load() & mouse_cap)) return false;
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONDOWN ||
        message == WM_RBUTTONUP || message == WM_MBUTTONDOWN || message == WM_MBUTTONUP || message == WM_XBUTTONDOWN ||
        message == WM_XBUTTONUP || message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
        if ((static_cast<ULONG_PTR>(GetMessageExtraInfo()) & 0xffffff00) == 0xff515700)
            return true; // Suppress pointer-promoted mouse duplicates.
        Event e;
        e.kind = Kind::mouse;
        POINT p{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
            ScreenToClient(window, &p);
            e.action = Action::wheel;
            e.code = message == WM_MOUSEHWHEEL ? 1 : 0;
            e.value = GET_WHEEL_DELTA_WPARAM(wparam);
        } else if (message != WM_MOUSEMOVE) {
            const bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN ||
                              message == WM_XBUTTONDOWN;
            e.action = down ? Action::down : Action::up;
            e.code = (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)
                         ? 1
                         : ((message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)
                                ? 2
                                : ((message == WM_MBUTTONDOWN || message == WM_MBUTTONUP)
                                       ? 3
                                       : (GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? 4 : 5)));
            if (down) {
                SetCapture(window);
                s.buttons |= 1u << e.code;
            } else
                s.buttons &= ~(1u << e.code);
        }
        if (s.position(window, p, e, s.buttons || e.action == Action::up)) s.push(e);
        if (e.action == Action::up && !s.buttons && GetCapture() == window) ReleaseCapture();
        return true;
    }
    return false;
}
}
