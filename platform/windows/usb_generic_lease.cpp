#include <dm/usb_generic_lease.hpp>
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <objbase.h>
#include <algorithm>
#include <stdexcept>
namespace dm::usb {
namespace {
constexpr auto path = LR"(SOFTWARE\ExtraPane\Components\UsbGenericLeases)";
void need(bool value) {
    if (!value) throw std::runtime_error("Generic USB ownership lease is unavailable or changed");
}
struct Key {
    HKEY h = nullptr;
    ~Key() {
        if (h) RegCloseKey(h);
    }
};
void secure(HKEY key) {
    DWORD linkType = 0, linkLength = 0;
    need(RegQueryValueExW(key, L"SymbolicLinkValue", nullptr, &linkType, nullptr, &linkLength) == ERROR_FILE_NOT_FOUND);
    DWORD length = 0;
    RegGetKeySecurity(key, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, nullptr, &length);
    need(length && length <= 65536);
    std::vector<unsigned char> bytes(length);
    auto sd = reinterpret_cast<PSECURITY_DESCRIPTOR>(bytes.data());
    need(RegGetKeySecurity(key, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, sd, &length) == ERROR_SUCCESS);
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    need(GetSecurityDescriptorControl(sd, &control, &revision) && (control & SE_DACL_PROTECTED));
    PSID owner = nullptr;
    BOOL defaulted = FALSE, present = FALSE;
    PACL acl = nullptr;
    need(GetSecurityDescriptorOwner(sd, &owner, &defaulted) && owner &&
         (IsWellKnownSid(owner, WinBuiltinAdministratorsSid) || IsWellKnownSid(owner, WinLocalSystemSid)));
    need(GetSecurityDescriptorDacl(sd, &present, &acl, &defaulted) && present && acl);
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void* raw = nullptr;
        need(GetAce(acl, i, &raw));
        auto* head = static_cast<ACE_HEADER*>(raw);
        if (head->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        need(head->AceType == ACCESS_ALLOWED_ACE_TYPE);
        auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        if (ace->Mask & (KEY_SET_VALUE | KEY_CREATE_SUB_KEY | KEY_CREATE_LINK | DELETE | WRITE_DAC | WRITE_OWNER |
                         GENERIC_WRITE | GENERIC_ALL))
            need(IsWellKnownSid(&ace->SidStart, WinBuiltinAdministratorsSid) ||
                 IsWellKnownSid(&ace->SidStart, WinLocalSystemSid));
    }
}
bool open(Key& key, bool write) {
    auto status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, REG_OPTION_OPEN_LINK,
                                (write ? KEY_ALL_ACCESS : KEY_READ) | KEY_WOW64_64KEY, &key.h);
    if ((status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) && !write) return false;
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
        PSECURITY_DESCRIPTOR sd = nullptr;
        need(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;KA;;;SY)(A;;KA;;;BA)",
                                                                  SDDL_REVISION_1, &sd, nullptr));
        SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
        status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_ALL_ACCESS | KEY_WOW64_64KEY, &sa, &key.h,
                                 nullptr);
        LocalFree(sd);
    }
    need(status == ERROR_SUCCESS);
    secure(key.h);
    return true;
}
std::wstring encoded(const GenericLease& lease) {
    need(valid_generic_lease(lease));
    if (lease.parent.empty())
        return L"ExtraPane generic USB lease 1\n" + lease.instance + L"\n" +
               std::wstring(lease.bus.begin(), lease.bus.end());
    return L"ExtraPane generic USB lease 2\n" + lease.instance + L"\n" +
           std::wstring(lease.bus.begin(), lease.bus.end()) + L"\n" + lease.parent + L"\n" +
           std::to_wstring(lease.port);
}
GenericLease decode(const std::wstring& guid, const std::wstring& text) {
    const std::wstring prefix = L"ExtraPane generic USB lease ";
    need(text.starts_with(prefix));
    const auto version = text.substr(prefix.size(), 2);
    need(version == L"1\n" || version == L"2\n");
    const auto start = prefix.size() + 2, at = text.find(L'\n', start);
    need(at != text.npos);
    GenericLease result{guid, text.substr(start, at - start), {}};
    auto bus = text.substr(at + 1);
    if (version == L"2\n") {
        const auto split = bus.find(L'\n'), end = bus.find(L'\n', split + 1);
        need(split != bus.npos && end != bus.npos);
        result.parent = bus.substr(split + 1, end - split - 1);
        const auto port = bus.substr(end + 1);
        need(!port.empty() && port.size() <= 3 &&
             std::all_of(port.begin(), port.end(), [](auto c) { return c >= L'0' && c <= L'9'; }));
        result.port = std::stoul(port);
        bus.resize(split);
    }
    for (auto c : bus) {
        need(c < 128);
        result.bus.push_back(static_cast<char>(c));
    }
    need(encoded(result) == text);
    return result;
}
std::wstring read(HKEY key, const std::wstring& name) {
    DWORD size = 0, type = 0;
    need(RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &size) == ERROR_SUCCESS && type == REG_SZ &&
         size >= 2 && size <= 16384 && size % 2 == 0);
    const auto original = size;
    std::vector<wchar_t> value(size / 2);
    need(RegQueryValueExW(key, name.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &size) ==
             ERROR_SUCCESS &&
         type == REG_SZ && size == original && value.back() == 0);
    need(std::find(value.begin(), value.end() - 1, L'\0') == value.end() - 1);
    return {value.data(), value.size() - 1};
}
}
std::wstring generic_lease_guid() {
    GUID guid{};
    need(SUCCEEDED(CoCreateGuid(&guid)));
    wchar_t text[40]{};
    need(StringFromGUID2(guid, text, 40) > 0);
    return text;
}
void save_generic_lease(const GenericLease& lease) {
    const auto text = encoded(lease);
    Key key;
    open(key, true);
    DWORD type = 0, length = 0;
    need(RegQueryValueExW(key.h, lease.guid.c_str(), nullptr, &type, nullptr, &length) == ERROR_FILE_NOT_FOUND);
    need(RegSetValueExW(key.h, lease.guid.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                        DWORD((text.size() + 1) * 2)) == ERROR_SUCCESS);
    need(RegFlushKey(key.h) == ERROR_SUCCESS);
}
std::vector<GenericLease> generic_leases() {
    Key key;
    if (!open(key, false)) return {};
    std::vector<GenericLease> result;
    for (DWORD i = 0; i < 128; ++i) {
        wchar_t name[64]{};
        DWORD length = 64;
        auto code = RegEnumValueW(key.h, i, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (code == ERROR_NO_MORE_ITEMS) return result;
        need(code == ERROR_SUCCESS);
        result.push_back(decode(name, read(key.h, name)));
    }
    throw std::runtime_error("Generic USB lease count exceeds limit");
}
void retire_generic_lease(const GenericLease& lease) {
    Key key;
    if (!open(key, false)) return;
    const auto text = encoded(lease);
    secure(key.h);
    Key writable;
    open(writable, true);
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(writable.h, lease.guid.c_str(), nullptr, &type, nullptr, &size) == ERROR_FILE_NOT_FOUND)
        return;
    need(read(writable.h, lease.guid) == text);
    need(RegDeleteValueW(writable.h, lease.guid.c_str()) == ERROR_SUCCESS);
    need(RegFlushKey(writable.h) == ERROR_SUCCESS);
}
}
