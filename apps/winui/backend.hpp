#pragma once
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
namespace
{
#pragma comment(                                                                                                       \
    linker,                                                                                                            \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
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
std::wstring command_quote(const std::wstring &s)
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
const wchar_t *settings(bool host=false)
{
    return host ? L"Software\\DM\\Host" : L"Software\\DM\\Receiver";
}
std::wstring setting(const wchar_t *name, const std::wstring &fallback,bool host=false)
{
    wchar_t v[256]{};
    DWORD size = sizeof(v);
    return RegGetValueW(HKEY_CURRENT_USER, settings(host), name, RRF_RT_REG_SZ, nullptr, v, &size) == ERROR_SUCCESS
               ? v
               : fallback;
}
std::wstring renderer_setting(){auto value=setting(L"Renderer",L"d3d11");return value==L"d3d12"||value==L"vulkan"||value==L"opengl"?value:L"d3d11";}
void save_setting(const wchar_t *name, const std::wstring &value,bool host=false)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, settings(host), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
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
        throw std::runtime_error("Enter a valid server IPv4 address.");
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
// Start Driver exposes only this executable and TCP port on active IPv4 addresses.
// Routed private LANs are supported without preconfiguring a particular client.
void firewall(bool add,unsigned port=43824,bool requireCode=false)
{
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
    std::wstring local;
    for(const auto& nic:dm::net::interfaces())if(nic.up && nic.address!="127.0.0.1") {
        if(!local.empty())local+=L",";local+=wide(nic.address);
    }
    if(local.empty())throw std::runtime_error("No connected LAN interface is available");
    ComPtr<INetFwRule> rule;
    check(CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&rule)));
    check(rule->put_Name(name));
    check(rule->put_ApplicationName(Bstr(exe)));
    check(rule->put_Protocol(NET_FW_IP_PROTOCOL_TCP));
    check(rule->put_LocalPorts(Bstr(std::to_wstring(port))));
    check(rule->put_LocalAddresses(Bstr(local)));
    check(rule->put_RemoteAddresses(Bstr(requireCode?L"*":L"LocalSubnet,10.0.0.0/8,172.16.0.0/12,192.168.0.0/16,169.254.0.0/16,100.64.0.0/10")));
    check(rule->put_Direction(NET_FW_RULE_DIR_IN));
    check(rule->put_Action(NET_FW_ACTION_ALLOW));
    check(rule->put_Profiles(NET_FW_PROFILE2_ALL));
    check(rule->put_Enabled(VARIANT_TRUE));
    check(rules->Add(rule.Get()));
}
struct Child
{
    HANDLE process = nullptr, read = nullptr;
    bool service=false;
    Child() = default;
    Child(const Child &) = delete;
    Child &operator=(const Child &) = delete;
    ~Child()
    {
        stop();
    }
    HANDLE input_write=nullptr;
    void send_control(const std::string& value){DWORD written=0;HANDLE target=service?read:input_write;if(!target || !WriteFile(target,value.data(),static_cast<DWORD>(value.size()),&written,nullptr) || written!=value.size())throw std::runtime_error("Cannot send engine control");}
    void stop()
    {
        if (process)
        {
            if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
            {
                if(service && read){DWORD written=0;const char stop='Q';WriteFile(read,&stop,1,&written,nullptr);WaitForSingleObject(process,10000);}
                if(WaitForSingleObject(process,0)==WAIT_TIMEOUT)TerminateProcess(process, 0);
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
        if(input_write){CloseHandle(input_write);input_write=nullptr;}
        service=false;
    }
    void start(const std::wstring &exe, const std::vector<std::wstring> &args)
    {
        stop();
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE write = nullptr;
        if (!CreatePipe(&read, &write, &security, 0))
            throw std::runtime_error("Cannot create app status pipe");
        SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
        HANDLE input=nullptr;
        if(!CreatePipe(&input,&input_write,&security,0)){CloseHandle(write);stop();throw std::runtime_error("Cannot create engine input pipe");}
        SetHandleInformation(input_write,HANDLE_FLAG_INHERIT,0);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = write;
        startup.hStdError = write;
        startup.hStdInput = input;
        std::wstring command = command_quote(exe);
        for (const auto &arg : args)
            command += L" " + command_quote(arg);
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
    void start_driver(bool input,bool debug,unsigned port=43824,bool require_code=false) {
        stop();
        std::array<unsigned char,16> random{};
        if(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot create driver channel");
        std::wstring pipe=L"\\\\.\\pipe\\ExtraPane-";for(auto b:random){pipe+=L"0123456789abcdef"[b>>4];pipe+=L"0123456789abcdef"[b&15];}
        HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw std::runtime_error("Cannot identify user");
        DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);std::vector<unsigned char> user(bytes);
        const bool token_ok=GetTokenInformation(token,TokenUser,user.data(),bytes,&bytes)!=FALSE;CloseHandle(token);
        if(!token_ok)throw std::runtime_error("Cannot identify user");
        LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,&sid))throw std::runtime_error("Cannot identify user SID");
        const std::wstring acl=L"D:P(A;;GA;;;SY)(A;;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))throw std::runtime_error("Cannot secure driver channel");
        SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};
        read=CreateNamedPipeW(pipe.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_NOWAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,4096,0,&security);LocalFree(descriptor);
        if(read==INVALID_HANDLE_VALUE){read=nullptr;throw std::runtime_error("Cannot create driver channel");}
        const auto exe=(directory()/L"ExtraPane.Service.exe").wstring();
        std::wstring args=L"--pipe "+command_quote(pipe)+L" --parent-pid "+std::to_wstring(GetCurrentProcessId());if(input)args+=L" --input";if(debug)args+=L" --debug";args+=L" --port "+std::to_wstring(port);if(require_code)args+=L" --require-code";
        SHELLEXECUTEINFOW launch{sizeof(launch)};launch.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;launch.lpVerb=L"runas";launch.lpFile=exe.c_str();launch.lpParameters=args.c_str();launch.nShow=SW_HIDE;
        if(!ShellExecuteExW(&launch)){const auto error=GetLastError();stop();throw std::runtime_error(error==ERROR_CANCELLED?"Driver start cancelled.":"Cannot elevate driver helper (Windows error "+std::to_string(error)+").");}
        process=launch.hProcess;
        bool connected=false;const auto until=GetTickCount64()+10000;
        while(GetTickCount64()<until && WaitForSingleObject(process,0)==WAIT_TIMEOUT){if(ConnectNamedPipe(read,nullptr)||GetLastError()==ERROR_PIPE_CONNECTED){connected=true;break;}Sleep(25);}
        ULONG client=0;
        if(!connected||!GetNamedPipeClientProcessId(read,&client)||client!=GetProcessId(process)){stop();throw std::runtime_error("Driver helper could not connect securely");}
        DWORD mode=PIPE_READMODE_BYTE|PIPE_WAIT;SetNamedPipeHandleState(read,&mode,nullptr,nullptr);service=true;
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

}
