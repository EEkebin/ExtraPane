#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <dm/network.hpp>
#include <dm/pairing.hpp>
#include <filesystem>
#include <netfw.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
#ifdef DM_HOST_APP
constexpr bool host = true;
#else
constexpr bool host = false;
#endif
namespace
{
#pragma comment(                                                                                                       \
    linker,                                                                                                            \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
int scaled(int value)
{
    return MulDiv(value, static_cast<int>(GetDpiForSystem()), 96);
}
enum ControlId
{
    Bind = 101,
    Peer,
    Resolution,
    Fps,
    Input,
    Debug,
    Start,
    Stop,
    Code,
    Generate,
    Save,
    Forget,
    Status,
    Log,
    Footer
};
std::wstring wide(const std::string &s)
{
    return {s.begin(), s.end()};
}
std::string ascii(const std::wstring &s)
{
    std::string result;
    for (auto c : s)
    {
        if (c > 127)
            throw std::runtime_error("Use an IPv4 address or ASCII pairing code.");
        result.push_back(static_cast<char>(c));
    }
    return result;
}
std::wstring text(HWND h)
{
    const int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}
std::wstring quoted(const std::wstring &s)
{ // Windows CreateProcess quoting, never a shell.
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (auto c : s)
    {
        if (c == L'\\')
        {
            ++slashes;
            continue;
        }
        if (c == L'\"')
            out.append(slashes * 2 + 1, L'\\');
        else
            out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'\"';
}
std::filesystem::path directory()
{
    std::array<wchar_t, 32768> p{};
    auto n = GetModuleFileNameW(nullptr, p.data(), static_cast<DWORD>(p.size()));
    if (!n || n == p.size())
        throw std::runtime_error("Cannot locate app files");
    return std::filesystem::path(p.data()).parent_path();
}
const wchar_t *settings()
{
    return host ? L"Software\\DM\\Host" : L"Software\\DM\\Receiver";
}
std::wstring setting(const wchar_t *name, const std::wstring &fallback)
{
    wchar_t v[256]{};
    DWORD size = sizeof(v);
    return RegGetValueW(HKEY_CURRENT_USER, settings(), name, RRF_RT_REG_SZ, nullptr, v, &size) == ERROR_SUCCESS
               ? v
               : fallback;
}
void save_setting(const wchar_t *name, const std::wstring &value)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, settings(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        throw std::runtime_error("Cannot save settings");
    const auto result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()),
                                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (result != ERROR_SUCCESS)
        throw std::runtime_error("Cannot save settings");
}
void ipv4(const std::wstring &value)
{
    IN_ADDR address{};
    if (InetPtonW(AF_INET, value.c_str(), &address) != 1)
        throw std::runtime_error("Enter a valid IPv4 address for each computer.");
}
void check(HRESULT hr)
{
    if (FAILED(hr))
        throw std::runtime_error("Windows firewall setup failed (HRESULT " +
                                 std::to_string(static_cast<unsigned long>(hr)) +
                                 "). Start the host as administrator.");
}
struct Bstr
{
    BSTR value;
    explicit Bstr(const std::wstring &s) : value(SysAllocString(s.c_str()))
    {
        if (!value)
            throw std::bad_alloc();
    }
    ~Bstr()
    {
        SysFreeString(value);
    }
    operator BSTR() const
    {
        return value;
    }
};
// Explicit Start on the host grants only this executable, chosen local interface,
// peer IPv4, TCP port. Never opens all remote addresses.
void firewall(bool add, const std::wstring &local = L"", const std::wstring &peer = L"")
{
    if (!host)
        return;
    ComPtr<INetFwPolicy2> policy;
    check(CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&policy)));
    ComPtr<INetFwRules> rules;
    check(policy->get_Rules(&rules));
    const Bstr name(L"DM Host LAN display");
    const auto exe = (directory() / L"dm-view.exe").wstring();
    ComPtr<INetFwRule> existing;
    if (SUCCEEDED(rules->Item(name, &existing)))
    {
        BSTR app = nullptr;
        check(existing->get_ApplicationName(&app));
        const bool owned = app && _wcsicmp(app, exe.c_str()) == 0;
        SysFreeString(app);
        if (!owned)
            throw std::runtime_error("A different DM firewall rule exists; its application path needs review.");
        check(rules->Remove(name));
    }
    if (!add)
        return;
    ComPtr<INetFwRule> rule;
    check(CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&rule)));
    check(rule->put_Name(name));
    check(rule->put_ApplicationName(Bstr(exe)));
    check(rule->put_Protocol(NET_FW_IP_PROTOCOL_TCP));
    check(rule->put_LocalPorts(Bstr(L"43824")));
    check(rule->put_LocalAddresses(Bstr(local)));
    check(rule->put_RemoteAddresses(Bstr(peer)));
    check(rule->put_Direction(NET_FW_RULE_DIR_IN));
    check(rule->put_Action(NET_FW_ACTION_ALLOW));
    check(rule->put_Profiles(NET_FW_PROFILE2_ALL));
    check(rule->put_Enabled(VARIANT_TRUE));
    check(rules->Add(rule.Get()));
}
struct Child
{
    HANDLE process = nullptr, read = nullptr;
    Child() = default;
    Child(const Child &) = delete;
    Child &operator=(const Child &) = delete;
    ~Child()
    {
        stop();
    }
    void stop()
    {
        if (process)
        {
            if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
            {
                TerminateProcess(process, 0);
                WaitForSingleObject(process, 3000);
            }
            CloseHandle(process);
            process = nullptr;
        }
        if (read)
        {
            CloseHandle(read);
            read = nullptr;
        }
    }
    void start(const std::wstring &exe, const std::vector<std::wstring> &args)
    {
        stop();
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE write = nullptr;
        if (!CreatePipe(&read, &write, &security, 0))
            throw std::runtime_error("Cannot create app status pipe");
        SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
        HANDLE input =
            CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = write;
        startup.hStdError = write;
        startup.hStdInput = input;
        std::wstring command = quoted(exe);
        for (const auto &arg : args)
            command += L" " + quoted(arg);
        PROCESS_INFORMATION pi{};
        const auto ok = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                       directory().c_str(), &startup, &pi);
        const auto error = GetLastError();
        CloseHandle(write);
        if (input != INVALID_HANDLE_VALUE)
            CloseHandle(input);
        if (!ok)
        {
            stop();
            throw std::runtime_error("Cannot start display engine (Windows error " + std::to_string(error) +
                                     "). Keep the app and dm-view.exe in the same folder.");
        }
        process = pi.hProcess;
        CloseHandle(pi.hThread);
    }
    std::string drain()
    {
        std::string output;
        DWORD available = 0;
        while (read && PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr) && available &&
               output.size() < 16384)
        {
            char buffer[2048];
            DWORD count = 0;
            if (!ReadFile(read, buffer, std::min<DWORD>(available, sizeof(buffer)), &count, nullptr) || !count)
                break;
            output.append(buffer, count);
        }
        return output;
    }
};
struct App
{
    HWND window = nullptr;
    HFONT font = nullptr, title_font = nullptr;
    Child child, pattern;
    bool debug = false, active = false, pattern_started = false;
    std::wstring logs;
    std::string pending;
    HWND item(int id)
    {
        return GetDlgItem(window, id);
    }
    void status(const std::wstring &value)
    {
        SetWindowTextW(item(Status), value.c_str());
    }
    void log(const std::wstring &value)
    {
        logs += value + L"\r\n";
        if (logs.size() > 24000)
            logs.erase(0, logs.size() - 18000);
        SetWindowTextW(item(Log), logs.c_str());
        SendMessageW(item(Log), EM_SETSEL, logs.size(), logs.size());
        SendMessageW(item(Log), EM_SCROLLCARET, 0, 0);
    }
    HWND control(const wchar_t *cls, const wchar_t *label, int id, int x, int y, int w, int h, DWORD style = 0)
    {
        auto handle =
            CreateWindowExW((wcscmp(cls, L"EDIT") == 0) ? WS_EX_CLIENTEDGE : 0, cls, label,
                            WS_CHILD | WS_VISIBLE | style, scaled(x), scaled(y), scaled(w), scaled(h), window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return handle;
    }
    void paired()
    {
        try
        {
            auto key = dm::security::load_pairing("dm-lan");
            SecureZeroMemory(key.data(), key.size());
            status(L"Paired and ready. Settings are saved on this computer.");
        }
        catch (...)
        {
            status(L"Pair once: enter the same generated code on both computers, then save.");
        }
    }
    void create()
    {
        font = CreateFontW(scaled(-17), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, L"Segoe UI");
        title_font = CreateFontW(scaled(-28), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                 CLEARTYPE_QUALITY, 0, L"Segoe UI");
        auto title = control(L"STATIC", host ? L"DM  /  Host" : L"DM  /  Receiver", 0, 24, 20, 600, 38);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(title_font), TRUE);
        control(L"STATIC",
                host ? L"Use another computer as an extended display over your LAN."
                     : L"Turn this screen into your host computer's extra display.",
                0, 24, 65, 660, 25);
        control(L"STATIC", L"This computer's LAN address", 0, 24, 108, 310, 24);
        auto bind = control(L"COMBOBOX", L"", Bind, 24, 136, 310, 180, CBS_DROPDOWN | WS_TABSTOP | WS_VSCROLL);
        std::wstring initial;
        for (const auto &nic : dm::net::interfaces())
            if (nic.up && nic.address != "127.0.0.1")
            {
                auto address = wide(nic.address);
                SendMessageW(bind, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(address.c_str()));
                if (initial.empty())
                    initial = address;
            }
        SetWindowTextW(bind, setting(L"Bind", initial).c_str());
        control(L"STATIC", host ? L"Receiver's LAN address" : L"Host's LAN address", 0, 358, 108, 310, 24);
        control(L"EDIT", setting(L"Peer", L"").c_str(), Peer, 358, 136, 310, 28, ES_AUTOHSCROLL | WS_TABSTOP);
        if (host)
        {
            control(L"STATIC", L"Extended display resolution", 0, 24, 184, 310, 24);
            auto resolution = control(L"COMBOBOX", L"", Resolution, 24, 212, 310, 160, CBS_DROPDOWNLIST | WS_TABSTOP);
            for (const auto *label : {L"3000 x 2000 (Surface Book)", L"1920 x 1080", L"1280 x 720"})
                SendMessageW(resolution, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
            const auto saved = setting(L"Resolution", L"0");
            SendMessageW(resolution, CB_SETCURSEL, saved == L"1" ? 1 : saved == L"2" ? 2 : 0, 0);
            control(L"STATIC", L"Frame-rate target", 0, 358, 184, 310, 24);
            auto fps = control(L"COMBOBOX", L"", Fps, 358, 212, 310, 100, CBS_DROPDOWNLIST | WS_TABSTOP);
            SendMessageW(fps, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"60 fps"));
            SendMessageW(fps, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"30 fps"));
            SendMessageW(fps, CB_SETCURSEL, setting(L"Fps", L"60") == L"30" ? 1 : 0, 0);
        }
        else
            control(L"STATIC",
                    L"Fullscreen at the host's chosen resolution.\r\nF8 pauses input. F9 returns to this window.", 0,
                    24, 190, 640, 54);
        control(L"BUTTON",
                host ? L"Allow receiver mouse, keyboard, touch and pen" : L"Send mouse, keyboard, touch and pen", Input,
                24, 260, 640, 28, BS_AUTOCHECKBOX | WS_TABSTOP);
        SendMessageW(item(Input), BM_SETCHECK, BST_CHECKED, 0);
        control(L"BUTTON", host ? L"Debug: 60-second session and color test window" : L"Debug: show connection details",
                Debug, 24, 298, 640, 28, BS_AUTOCHECKBOX | WS_TABSTOP);
        control(L"BUTTON", host ? L"Start display" : L"Connect", Start, 24, 346, 310, 42,
                BS_DEFPUSHBUTTON | WS_TABSTOP);
        control(L"BUTTON", L"Disconnect", Stop, 358, 346, 310, 42, WS_TABSTOP);
        EnableWindow(item(Stop), FALSE);
        control(L"STATIC", L"", Status, 24, 405, 644, 54);
        control(L"BUTTON", L"Pairing (one-time setup)", 0, 20, 470, 652, 144, BS_GROUPBOX);
        control(L"EDIT", L"", Code, 36, 501, 620, 28, ES_AUTOHSCROLL | WS_TABSTOP);
        SendMessageW(item(Code), EM_SETLIMITTEXT, 64, 0);
        control(L"BUTTON", L"Generate code", Generate, 36, 548, 190, 32, WS_TABSTOP);
        control(L"BUTTON", L"Save pairing", Save, 246, 548, 190, 32, WS_TABSTOP);
        control(L"BUTTON", L"Forget pairing", Forget, 456, 548, 200, 32, WS_TABSTOP);
        control(L"STATIC", L"Copy the generated code to the other computer before saving it here.", 0, 36, 585, 620,
                22);
        control(L"EDIT", L"", Log, 24, 630, 644, 150, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL);
        ShowWindow(item(Log), SW_HIDE);
        control(L"STATIC",
                host ? L"Start shares only the extra display. Audio is not available yet."
                     : L"Direct LAN connection. No SSH or internet service required.",
                Footer, 24, 628, 644, 25);
        paired();
        SetTimer(window, 1, 200, nullptr);
    }
    bool checked(int id)
    {
        return SendMessageW(item(id), BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    void enabled(bool running)
    {
        for (int id : {Bind, Peer, Resolution, Fps, Input, Debug, Start, Generate, Save, Forget, Code})
            if (item(id))
                EnableWindow(item(id), !running);
        EnableWindow(item(Stop), running);
    }
    void stop()
    {
        child.stop();
        pattern.stop();
        active = false;
        enabled(false);
        if (host)
            firewall(false);
        status(L"Disconnected. Ready for another session.");
    }
    void start()
    {
        const auto local = text(item(Bind)), peer = text(item(Peer));
        ipv4(local);
        ipv4(peer);
        auto key = dm::security::load_pairing("dm-lan");
        SecureZeroMemory(key.data(), key.size());
        save_setting(L"Bind", local);
        save_setting(L"Peer", peer);
        debug = checked(Debug);
        pending.clear();
        logs.clear();
        SetWindowTextW(item(Log), L"");
        pattern_started = false;
        std::vector<std::wstring> args{
            host ? L"host" : L"receive",           L"--name", L"dm-lan", L"--bind", local, L"--parent-pid",
            std::to_wstring(GetCurrentProcessId())};
        if (host)
        {
            const auto res = SendMessageW(item(Resolution), CB_GETCURSEL, 0, 0);
            const auto fps = SendMessageW(item(Fps), CB_GETCURSEL, 0, 0) == 1 ? 30 : 60;
            save_setting(L"Resolution", std::to_wstring(res));
            save_setting(L"Fps", std::to_wstring(fps));
            const unsigned widths[]{3000, 1920, 1280}, heights[]{2000, 1080, 720};
            const size_t index = res >= 0 && res <= 2 ? static_cast<size_t>(res) : 0;
            args.insert(args.end(), {L"--allow-screen", L"--extend", L"--keep-layout", L"--width", std::to_wstring(widths[index]),
                                     L"--height", std::to_wstring(heights[index]), L"--fps", std::to_wstring(fps),
                                     L"--frames", std::to_wstring(debug ? 60 * fps : 0), L"--timeout-ms", L"0"});
            if (checked(Input))
                args.push_back(L"--allow-input");
            firewall(true, local, peer);
        }
        else
        {
            args.insert(args.end(), {L"--peer", peer, L"--fullscreen"});
            if (checked(Input))
                args.push_back(L"--control");
        }
        try
        {
            child.start((directory() / L"dm-view.exe").wstring(), args);
        }
        catch (...)
        {
            if (host)
                firewall(false);
            throw;
        }
        active = true;
        enabled(true);
        status(host ? L"Waiting for the receiver. Open DM Receiver and click Connect." : L"Connecting to host...");
    }
    void poll()
    {
        if (!active)
            return;
        pending += child.drain();
        size_t end = 0;
        while ((end = pending.find('\n')) != std::string::npos)
        {
            auto line = pending.substr(0, end);
            pending.erase(0, end + 1);
            log(wide(line));
            if (line.find("VIDEO_STARTED") != std::string::npos || line.find("VIDEO_RECEIVING") != std::string::npos)
            {
                status(debug ? L"Connected. Debug diagnostics enabled."
                             : L"Connected. Runs until you disconnect; no session time limit.");
                if (host && debug && !pattern_started)
                {
                    pattern.start((directory() / L"dm-display.exe").wstring(), {L"pattern", L"60"});
                    pattern_started = true;
                }
            }
            if(line.starts_with("DISPLAY_RECONFIGURING"))status(L"Applying Windows display changes...");
            if(line.starts_with("DISPLAY_RECONFIGURED"))status(L"Connected. Windows display changes applied.");
            if (line.starts_with("error:"))
                status(wide(line));
        }
        if (pending.size() > 16384)
            pending.clear();
        if (WaitForSingleObject(child.process, 0) == WAIT_OBJECT_0)
        {
            DWORD exit = 0;
            GetExitCodeProcess(child.process, &exit);
            child.stop();
            pattern.stop();
            active = false;
            enabled(false);
            if (host)
                firewall(false);
            if (exit == 0)
                status(L"Session ended. Ready to connect again.");
            else if (logs.find(L"error:") == std::wstring::npos)
                status(L"Connection ended. Check the other computer, or enable Debug for details.");
        }
    }
    void command(int id)
    {
        if (id == Start)
            start();
        else if (id == Stop)
            stop();
        else if (id == Generate)
        {
            std::array<unsigned char, 32> key{};
            if (BCryptGenRandom(nullptr, key.data(), static_cast<ULONG>(key.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) <
                0)
                throw std::runtime_error("Cannot generate pairing code");
            std::wstring code;
            for (auto b : key)
            {
                code += L"0123456789abcdef"[b >> 4];
                code += L"0123456789abcdef"[b & 15];
            }
            SetWindowTextW(item(Code), code.c_str());
            SecureZeroMemory(key.data(), key.size());
            SecureZeroMemory(code.data(), code.size() * sizeof(wchar_t));
            status(L"Copy this code to the other computer, then Save pairing on both.");
        }
        else if (id == Save)
        {
            auto code = ascii(text(item(Code)));
            auto key = dm::security::parse_key(code);
            SecureZeroMemory(code.data(), code.size());
            try
            {
                dm::security::save_pairing("dm-lan", key);
            }
            catch (...)
            {
                SecureZeroMemory(key.data(), key.size());
                throw;
            }
            SecureZeroMemory(key.data(), key.size());
            SetWindowTextW(item(Code), L"");
            paired();
        }
        else if (id == Forget)
        {
            if (MessageBoxW(window, L"Forget this computer's saved DM pairing?", L"DM pairing",
                            MB_YESNO | MB_ICONQUESTION) == IDYES)
            {
                dm::security::revoke_pairing("dm-lan");
                paired();
            }
        }
        else if (id == Debug)
        {
            const bool show = checked(Debug);
            ShowWindow(item(Log), show ? SW_SHOW : SW_HIDE);
            ShowWindow(item(Footer), show ? SW_HIDE : SW_SHOW);
            SetWindowPos(window, nullptr, 0, 0, scaled(710), scaled(show ? 840 : 720), SWP_NOMOVE | SWP_NOZORDER);
        }
    }
    ~App()
    {
        child.stop();
        pattern.stop();
        if (font)
            DeleteObject(font);
        if (title_font)
            DeleteObject(title_font);
    }
};
LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto *app = reinterpret_cast<App *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        app = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    try
    {
        if (message == WM_CREATE)
        {
            app->create();
            return 0;
        }
        if (message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED)
        {
            app->command(LOWORD(wparam));
            return 0;
        }
        if (message == WM_TIMER)
        {
            app->poll();
            return 0;
        }
        if (message == WM_CLOSE)
        {
            app->stop();
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY)
        {
            PostQuitMessage(0);
            return 0;
        }
    }
    catch (const std::exception &e)
    {
        if (app)
        {
            app->status(wide(e.what()));
            app->log(wide(e.what()));
        }
        MessageBoxW(window, wide(e.what()).c_str(), L"DM", MB_OK | MB_ICONERROR);
        if (message == WM_CREATE)
            return -1;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try
    {
        const auto mutex = CreateMutexW(nullptr, TRUE, host ? L"Local\\DMHostWindow" : L"Local\\DMReceiverWindow");
        if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
        {
            if (mutex)
                CloseHandle(mutex);
            throw std::runtime_error("This DM app is already open.");
        }
        struct Close
        {
            HANDLE handle;
            ~Close()
            {
                CloseHandle(handle);
            }
        } close{mutex};
        App app;
        WNDCLASSW cls{};
        cls.lpfnWndProc = procedure;
        cls.hInstance = instance;
        cls.lpszClassName = host ? L"DMHostApp" : L"DMReceiverApp";
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&cls);
        const auto window = CreateWindowExW(0, cls.lpszClassName, host ? L"DM Host" : L"DM Receiver",
                                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                                            CW_USEDEFAULT, scaled(710), scaled(720), nullptr, nullptr, instance, &app);
        if (!window)
            throw std::runtime_error("Cannot create DM window");
        ShowWindow(window, SW_SHOWNORMAL);
        SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
        if (std::wstring_view(arguments).starts_with(L"--start"))
            app.start();
        if (std::wstring_view(arguments).starts_with(L"--smoke-test"))
        {
            if (app.checked(Debug) || !IsWindowVisible(window) || !IsWindowEnabled(app.item(Start)) ||
                IsWindowEnabled(app.item(Stop)) || GetWindowTextLengthW(app.item(Code)) != 0)
                throw std::runtime_error("Initial UI state failed verification");
            SendMessageW(app.item(Debug), BM_SETCHECK, BST_CHECKED, 0);
            app.command(Debug);
            if (!IsWindowVisible(app.item(Log)))
                throw std::runtime_error("Debug details did not open");
            SendMessageW(app.item(Debug), BM_SETCHECK, BST_UNCHECKED, 0);
            app.command(Debug);
            if (IsWindowVisible(app.item(Log)))
                throw std::runtime_error("Debug details remained visible");
            DestroyWindow(window);
        }
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            if (!IsDialogMessageW(window, &message))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }
    catch (const std::exception &e)
    {
        MessageBoxW(nullptr, wide(e.what()).c_str(), L"DM", MB_OK | MB_ICONERROR);
        if (SUCCEEDED(initialized))
            CoUninitialize();
        return 1;
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return 0;
}
