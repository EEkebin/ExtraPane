#include <windows.h>
#include <dm/display.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <string>
struct Search {
    DWORD pid;
    HWND window = nullptr, tray = nullptr;
    bool server;
};
BOOL CALLBACK collect(HWND h, LPARAM p) {
    auto& s = *reinterpret_cast<Search*>(p);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != s.pid) return TRUE;
    wchar_t cls[100]{}, title[100]{};
    GetClassNameW(h, cls, 100);
    GetWindowTextW(h, title, 100);
    if (wcscmp(cls, L"ExtraPane.Tray") == 0)
        s.tray = h;
    else if (wcscmp(title, L"ExtraPane") == 0)
        s.window = h;
    return TRUE;
}
void need(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}
void pause() {
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
}
bool monitor() {
    try {
        for (const auto& m : dm::display::active_monitors())
            if (m.dm_virtual) return true;
    } catch (...) {}
    return false;
}
void wait_monitor(bool expected) {
    for (unsigned i = 0; i < 50; ++i) {
        if (monitor() == expected) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    throw std::runtime_error("Driver topology did not reach expected state");
}
int main(int argc, char** argv) {
    try {
        need(argc >= 3, "Supply launcher PID and server/client");
        Search s{static_cast<DWORD>(std::stoul(argv[1])), nullptr, nullptr, std::string(argv[2]) == "server"};
        EnumWindows(collect, reinterpret_cast<LPARAM>(&s));
        need(s.window && s.tray, "App/tray window missing");
        const bool running = GetPropW(s.tray, L"ExtraPane.Running") != nullptr;
        if (!running) {
            need(GetPropW(s.tray, L"ExtraPane.TrayReady") == nullptr, "Idle/client-only app must not add a tray icon");
            HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, s.pid);
            need(process != nullptr, "Cannot wait for app exit");
            PostMessageW(s.window, WM_CLOSE, 0, 0);
            need(WaitForSingleObject(process, 12000) == WAIT_OBJECT_0, "Closing without a server must exit the app");
            CloseHandle(process);
            need(!IsWindow(s.tray), "Closed app left a tray window");
            std::cout << "CLOSE_EXIT passed without server runtime\n";
            return 0;
        }
        need(GetPropW(s.tray, L"ExtraPane.TrayReady") != nullptr, "Running server notification icon missing");
        SendMessageW(s.window, WM_CLOSE, 0, 0);
        pause();
        need(IsWindow(s.window) && !IsWindowVisible(s.window), "Close did not hide to tray");
        need((GetPropW(s.tray, L"ExtraPane.Running") != nullptr) == running, "Close changed driver/session state");
        if (argc > 3 && std::string(argv[3]) == "--stop-hidden") {
            HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, s.pid);
            need(process != nullptr, "Cannot wait for app exit");
            PostMessageW(s.tray, WM_COMMAND, 2, 0);
            need(WaitForSingleObject(process, 15000) == WAIT_OBJECT_0, "Stopping a hidden server must exit");
            CloseHandle(process);
            wait_monitor(false);
            std::cout << "HIDDEN_STOP_EXIT passed\n";
            return 0;
        }
        SendMessageW(s.tray, WM_COMMAND, 1, 0);
        pause();
        need(IsWindowVisible(s.window) != FALSE, "Tray Open did not restore window");
        std::cout << "TRAY_CHECK close-to-tray and reopen passed\n";
        SendMessageW(s.tray, WM_COMMAND, 4, 0);
        pause();
        SendMessageW(s.tray, WM_COMMAND, 1, 0);
        pause();
        need((GetPropW(s.tray, L"ExtraPane.Running") != nullptr) == running, "Page changes stopped server");
        if (argc > 3 && std::string(argv[3]) == "--keep-running") return 0;
        if (s.server) {
            if (running) {
                SendMessageW(s.tray, WM_COMMAND, 2, 0);
                wait_monitor(false);
            }
            SendMessageW(s.tray, WM_COMMAND, 2, 0);
            pause();
            wait_monitor(false);
            need(GetPropW(s.tray, L"ExtraPane.Running") != nullptr, "Tray Start Driver failed");
            std::cout << "TRAY_DRIVER start/stop passed\n";
        }
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, s.pid);
        need(process != nullptr, "Cannot wait for app exit");
        SendMessageW(s.tray, WM_COMMAND, 3, 0);
        need(WaitForSingleObject(process, 12000) == WAIT_OBJECT_0, "Tray Exit did not terminate app");
        CloseHandle(process);
        if (s.server) wait_monitor(false);
        std::cout << "TRAY_EXIT cleanup passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
