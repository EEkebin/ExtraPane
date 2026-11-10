#pragma once
#include "../shared/windows_child.hpp"
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
#include <sddl.h>
#include <shellapi.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
namespace {
#pragma comment(                                                                                                       \
    linker,                                                                                                            \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
std::wstring wide(const std::string& s) {
    return {s.begin(), s.end()};
}
std::string ascii(const std::wstring& s) {
    std::string result;
    for (auto c : s) {
        if (c > 127) throw std::runtime_error("Use an IPv4 address or ASCII pairing code.");
        result.push_back(static_cast<char>(c));
    }
    return result;
}
std::wstring command_quote(const std::wstring& s) { // Windows CreateProcess quoting, never a shell.
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (auto c : s) {
        if (c == L'\\') {
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
std::filesystem::path directory() {
    std::array<wchar_t, 32768> p{};
    auto n = GetModuleFileNameW(nullptr, p.data(), static_cast<DWORD>(p.size()));
    if (!n || n == p.size()) throw std::runtime_error("Cannot locate app files");
    return std::filesystem::path(p.data()).parent_path();
}
const wchar_t* settings(bool host = false) {
    return host ? L"Software\\DM\\Host" : L"Software\\DM\\Receiver";
}
std::wstring setting(const wchar_t* name, const std::wstring& fallback, bool host = false) {
    wchar_t v[256]{};
    DWORD size = sizeof(v);
    return RegGetValueW(HKEY_CURRENT_USER, settings(host), name, RRF_RT_REG_SZ, nullptr, v, &size) == ERROR_SUCCESS
               ? v
               : fallback;
}
std::wstring renderer_setting() {
    auto value = setting(L"Renderer", L"d3d11");
    return value == L"d3d12" || value == L"vulkan" || value == L"opengl" ? value : L"d3d11";
}
void save_setting(const wchar_t* name, const std::wstring& value, bool host = false) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, settings(host), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        throw std::runtime_error("Cannot save settings");
    const auto result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) throw std::runtime_error("Cannot save settings");
}
void ipv4(const std::wstring& value) {
    IN_ADDR address{};
    if (InetPtonW(AF_INET, value.c_str(), &address) != 1)
        throw std::runtime_error("Enter a valid server IPv4 address.");
}
void check(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("Windows firewall setup failed (HRESULT " +
                                 std::to_string(static_cast<unsigned long>(hr)) +
                                 "). Start the host as administrator.");
}
struct Bstr {
    BSTR value;
    explicit Bstr(const std::wstring& s) : value(SysAllocString(s.c_str())) {
        if (!value) throw std::bad_alloc();
    }
    ~Bstr() { SysFreeString(value); }
    operator BSTR() const { return value; }
};
// Start Driver exposes only this executable and TCP port on active IPv4 addresses.
// Routed private LANs are supported without preconfiguring a particular client.
void firewall(bool add, unsigned port = 43824, bool requireCode = false, const std::wstring& bind = L"auto") {
    ComPtr<INetFwPolicy2> policy;
    check(CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&policy)));
    ComPtr<INetFwRules> rules;
    check(policy->get_Rules(&rules));
    const Bstr name(L"DM Host LAN display");
    const auto exe = (directory() / L"dm-view.exe").wstring();
    ComPtr<INetFwRule> existing;
    if (SUCCEEDED(rules->Item(name, &existing))) {
        BSTR app = nullptr;
        check(existing->get_ApplicationName(&app));
        const bool owned = app && _wcsicmp(app, exe.c_str()) == 0;
        SysFreeString(app);
        if (!owned)
            throw std::runtime_error(
                "A conflicting legacy ExtraPane firewall rule exists; its application path needs review.");
        check(rules->Remove(name));
    }
    if (!add) return;
    std::wstring local;
    for (const auto& nic : dm::net::interfaces())
        if (nic.up && nic.address != "127.0.0.1" && (bind == L"auto" || wide(nic.address) == bind)) {
            if (!local.empty()) local += L",";
            local += wide(nic.address);
        }
    if (local.empty()) throw std::runtime_error("No connected LAN interface is available");
    ComPtr<INetFwRule> rule;
    check(CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&rule)));
    check(rule->put_Name(name));
    check(rule->put_ApplicationName(Bstr(exe)));
    check(rule->put_Protocol(NET_FW_IP_PROTOCOL_TCP));
    check(rule->put_LocalPorts(Bstr(std::to_wstring(port))));
    check(rule->put_LocalAddresses(Bstr(local)));
    check(rule->put_RemoteAddresses(Bstr(
        requireCode ? L"*" : L"LocalSubnet,10.0.0.0/8,172.16.0.0/12,192.168.0.0/16,169.254.0.0/16,100.64.0.0/10")));
    check(rule->put_Direction(NET_FW_RULE_DIR_IN));
    check(rule->put_Action(NET_FW_ACTION_ALLOW));
    check(rule->put_Profiles(NET_FW_PROFILE2_ALL));
    check(rule->put_Enabled(VARIANT_TRUE));
    check(rules->Add(rule.Get()));
}
using Child = extrapane::windows_bridge::Child;
}
