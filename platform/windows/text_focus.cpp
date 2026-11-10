#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <dm/text_focus.hpp>
#include <dm/text_focus_policy.hpp>
#include <dm/display.hpp>
#include <atomic>
#include <algorithm>
#include <functional>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <iostream>
#include <syncstream>
namespace dm::input {
namespace {
class HostKeyboard {
    HANDLE process_ = nullptr, lock_ = nullptr;
    HWND window_ = nullptr;

public:
    ~HostKeyboard() {
        if (window_ && process_) {
            DWORD pid = 0;
            GetWindowThreadProcessId(window_, &pid);
            if (pid == GetProcessId(process_)) PostMessageW(window_, WM_CLOSE, 0, 0);
        }
        if (process_) CloseHandle(process_);
        if (lock_) {
            ReleaseMutex(lock_);
            CloseHandle(lock_);
        }
    }
    void show(const display::Monitor& target, const std::function<bool()>& current) {
        if (!current()) return;
        if (process_ && WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) {
            CloseHandle(process_);
            process_ = nullptr;
            window_ = nullptr;
        }
        if (!process_) {
            if (!lock_) {
                lock_ = CreateMutexW(nullptr, FALSE, L"Local\\ExtraPane.RemoteKeyboard");
                const auto acquired = lock_ ? WaitForSingleObject(lock_, 0) : WAIT_FAILED;
                if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) {
                    if (lock_) CloseHandle(lock_);
                    lock_ = nullptr;
                    std::osyncstream(std::cout)
                        << "AUTO_KEYBOARD_UNAVAILABLE Another session owns the keyboard" << std::endl;
                    return;
                }
            }
            if (FindWindowW(L"OSKMainClass", nullptr)) {
                ReleaseMutex(lock_);
                CloseHandle(lock_);
                lock_ = nullptr;
                std::osyncstream(std::cout) << "AUTO_KEYBOARD_UNAVAILABLE Existing Windows keyboard preserved; close "
                                               "it to allow a remote keyboard"
                                            << std::endl;
                return;
            }
            wchar_t system[MAX_PATH]{};
            const auto size = GetSystemDirectoryW(system, MAX_PATH);
            if (!size || size >= MAX_PATH) {
                ReleaseMutex(lock_);
                CloseHandle(lock_);
                lock_ = nullptr;
                return;
            }
            const auto path = std::wstring(system) + L"\\osk.exe";
            SHELLEXECUTEINFOW launch{sizeof(launch)};
            launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
            launch.lpVerb = L"open";
            launch.lpFile = path.c_str();
            launch.nShow = SW_SHOWNOACTIVATE;
            if (!ShellExecuteExW(&launch) || !launch.hProcess) {
                if (lock_) {
                    ReleaseMutex(lock_);
                    CloseHandle(lock_);
                    lock_ = nullptr;
                }
                std::osyncstream(std::cout)
                    << "AUTO_KEYBOARD_UNAVAILABLE Windows keyboard launch failed error=" << GetLastError() << std::endl;
                return;
            }
            process_ = launch.hProcess;
            for (unsigned attempt = 0; attempt < 30; ++attempt) {
                const auto candidate = FindWindowW(L"OSKMainClass", nullptr);
                DWORD pid = 0;
                if (candidate) GetWindowThreadProcessId(candidate, &pid);
                if (candidate && pid == GetProcessId(process_)) {
                    window_ = candidate;
                    break;
                }
                if (WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        if (!window_) {
            std::osyncstream(std::cout)
                << "AUTO_KEYBOARD_UNAVAILABLE Windows delegated OSK to an unowned process; not repositioning it"
                << std::endl;
            return;
        }
        if (!current()) {
            PostMessageW(window_, WM_CLOSE, 0, 0);
            return;
        }
        RECT rect{};
        if (!GetWindowRect(window_, &rect)) return;
        const int width = std::min<int>(rect.right - rect.left, target.right - target.left),
                  height = std::min<int>(rect.bottom - rect.top, target.bottom - target.top);
        if (SetWindowPos(window_, HWND_TOPMOST, target.left + (target.right - target.left - width) / 2,
                         target.bottom - height, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW))
            std::osyncstream(std::cout) << "AUTO_KEYBOARD_SHOWN host-virtual-monitor" << std::endl;
        else
            std::osyncstream(std::cout) << "AUTO_KEYBOARD_UNAVAILABLE Cannot position owned keyboard error="
                                        << GetLastError() << std::endl;
    }
};
bool normal_desktop() {
    const auto desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desktop) return false;
    wchar_t name[128]{};
    DWORD size = 0;
    const bool ok =
        GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &size) && _wcsicmp(name, L"Default") == 0;
    CloseDesktop(desktop);
    return ok;
}
}
struct TextFocusProbe::State {
    std::wstring monitor;
    bool host_keyboard = false;
    std::mutex mutex;
    Event touch;
    std::uint64_t generation = 0, expires = 0;
    bool released = false;
    std::atomic_uint32_t confirmed = 0;
    std::jthread worker;
    explicit State(std::wstring source, bool keyboard)
        : monitor(std::move(source)), host_keyboard(keyboard), worker([this](std::stop_token stop) { run(stop); }) {}
    void run(std::stop_token stop) {
        const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialized)) return;
        struct Com {
            ~Com() { CoUninitialize(); }
        } com;
        Microsoft::WRL::ComPtr<IUIAutomation2> automation;
        if (FAILED(
                CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation))) ||
            FAILED(automation->put_ConnectionTimeout(150)) || FAILED(automation->put_TransactionTimeout(150))) {
            std::osyncstream(std::cout) << "AUTO_KEYBOARD_UNAVAILABLE UI Automation probe unavailable" << std::endl;
            return;
        }
        HostKeyboard keyboard;
        std::uint64_t handled = 0;
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            Event event;
            std::uint64_t request = 0, until = 0;
            bool up = false;
            {
                std::lock_guard lock(mutex);
                request = generation;
                event = touch;
                until = expires;
                up = released;
            }
            if (!request || request == handled || !until) continue;
            if (GetTickCount64() > until) {
                handled = request;
                std::osyncstream(std::cout)
                    << "AUTO_KEYBOARD_NO_EDITABLE_TARGET No writable focused UI Automation field confirmed for touch"
                    << std::endl;
                continue;
            }
            if (!up || !normal_desktop()) continue;
            try {
                std::optional<display::Monitor> target;
                for (const auto& candidate : display::active_monitors())
                    if (candidate.source == monitor) target = candidate;
                if (!target) continue;
                const long x =
                    target->left +
                    static_cast<long>((std::uint64_t(event.x) * (target->right - target->left - 1) + 32767) / 65535);
                const long y =
                    target->top +
                    static_cast<long>((std::uint64_t(event.y) * (target->bottom - target->top - 1) + 32767) / 65535);
                // A local foreground window outside this display never qualifies.
                const auto foreground = GetForegroundWindow();
                RECT foreground_rect{};
                if (!foreground || !GetWindowRect(foreground, &foreground_rect) ||
                    !PtInRect(&foreground_rect, POINT{x, y}))
                    continue;
                Microsoft::WRL::ComPtr<IUIAutomationElement> element;
                if (FAILED(automation->GetFocusedElement(&element)) || !element) continue;
                BOOL enabled = FALSE, focused = FALSE;
                RECT rect{};
                if (FAILED(element->get_CurrentIsEnabled(&enabled)) ||
                    FAILED(element->get_CurrentHasKeyboardFocus(&focused)) ||
                    FAILED(element->get_CurrentBoundingRectangle(&rect)))
                    continue;
                Microsoft::WRL::ComPtr<IUIAutomationValuePattern> value;
                BOOL read_only = TRUE;
                // ValuePattern.IsReadOnly is metadata. Never call CurrentValue,
                // CurrentName or TextPattern.GetText, including password fields.
                bool editable = SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) &&
                                value && SUCCEEDED(value->get_CurrentIsReadOnly(&read_only)) && !read_only;
                if (!editable) {
                    CONTROLTYPEID type = 0;
                    element->get_CurrentControlType(&type);
                    Microsoft::WRL::ComPtr<IUIAutomationTextPattern> text;
                    Microsoft::WRL::ComPtr<IUIAutomationTextRange> range;
                    if ((type == UIA_EditControlTypeId || type == UIA_DocumentControlTypeId) &&
                        SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text))) && text &&
                        SUCCEEDED(text->get_DocumentRange(&range)) && range) {
                        VARIANT attribute;
                        VariantInit(&attribute);
                        if (SUCCEEDED(range->GetAttributeValue(UIA_IsReadOnlyAttributeId, &attribute)))
                            editable = attribute.vt == VT_BOOL && attribute.boolVal == VARIANT_FALSE;
                        VariantClear(&attribute);
                    }
                }
                if (GetForegroundWindow() != foreground || !normal_desktop()) continue;
                if (!editable_at_touch({target->left, target->top, target->right, target->bottom},
                                       {rect.left, rect.top, rect.right, rect.bottom}, x, y, enabled != FALSE,
                                       focused != FALSE, editable))
                    continue;
                {
                    std::lock_guard lock(mutex);
                    if (generation != request || GetTickCount64() > expires) continue;
                    confirmed = touch_token(event.micros);
                    handled = request;
                }
                if (host_keyboard)
                    keyboard.show(*target, [&] {
                        std::lock_guard lock(mutex);
                        return !stop.stop_requested() && generation == request && GetTickCount64() <= expires + 3000 &&
                               GetForegroundWindow() == foreground && normal_desktop();
                    });
            } catch (...) { /* Unsupported providers or display changes never imply editable focus. */
            }
        }
    }
};
TextFocusProbe::TextFocusProbe(std::wstring monitor, bool host_keyboard)
    : state_(std::make_unique<State>(std::move(monitor), host_keyboard)) {}
TextFocusProbe::~TextFocusProbe() = default;
void TextFocusProbe::observe(std::span<const Event> events) {
    std::lock_guard lock(state_->mutex);
    for (const auto& event : events) {
        if (event.kind == Kind::reset || event.kind == Kind::key || event.kind == Kind::pen ||
            (event.kind == Kind::mouse && event.action == Action::down) ||
            (event.kind == Kind::touch && event.action == Action::cancel)) {
            ++state_->generation;
            state_->expires = 0;
            state_->confirmed = 0;
        }
        if (event.kind == Kind::touch && event.action == Action::down) {
            state_->touch = event;
            state_->released = false;
            ++state_->generation;
            state_->expires = GetTickCount64() + 2000;
            state_->confirmed = 0;
        }
        if (event.kind == Kind::touch && event.action == Action::up && event.id == state_->touch.id)
            state_->released = true;
    }
}
std::uint32_t TextFocusProbe::confirmed_touch() const noexcept {
    return state_->confirmed.load();
}
}
