#include <windows.h>
#include <shlobj.h>
#include <sddl.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <filesystem>
#include <iostream>
#include <vector>
#include <span>
#include <iomanip>
#include <sstream>
#include <cstring>
#include "registration.hpp"

namespace fs = std::filesystem;
namespace setup = extrapane::camera_setup;
namespace {
constexpr wchar_t class_key[] = L"Software\\Classes\\CLSID\\{5A053AD3-02CD-4281-8367-CEF990182004}";
constexpr wchar_t inproc_key[] = L"Software\\Classes\\CLSID\\{5A053AD3-02CD-4281-8367-CEF990182004}\\InprocServer32";
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
    Handle(const Handle&) = delete;
};
struct Key {
    HKEY value = nullptr;
    ~Key() {
        if (value) RegCloseKey(value);
    }
};
struct Local {
    void* value = nullptr;
    ~Local() {
        if (value) LocalFree(value);
    }
};
std::wstring sha256(std::span<const unsigned char> bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
          "Open SHA256 provider failed");
    struct Algorithm {
        BCRYPT_ALG_HANDLE value;
        ~Algorithm() { BCryptCloseAlgorithmProvider(value, 0); }
    } cleanup{algorithm};
    BCRYPT_HASH_HANDLE hash = nullptr;
    check(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0, "Create SHA256 failed");
    struct Hash {
        BCRYPT_HASH_HANDLE value;
        ~Hash() { BCryptDestroyHash(value); }
    } hashCleanup{hash};
    check(bytes.size() <= 64 * 1024 * 1024, "Camera component exceeds package limit");
    check(BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0) >= 0,
          "Hash camera component failed");
    unsigned char digest[32]{};
    check(BCryptFinishHash(hash, digest, 32, 0) >= 0, "Finish camera hash failed");
    std::wostringstream text;
    text << std::hex << std::setfill(L'0');
    for (auto value : digest) text << std::setw(2) << unsigned(value);
    return text.str();
}
std::span<const unsigned char> payload() {
    const auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    check(resource != nullptr, "Embedded camera component is missing");
    const auto size = SizeofResource(nullptr, resource);
    const auto loaded = LoadResource(nullptr, resource);
    const auto* bytes = static_cast<const unsigned char*>(LockResource(loaded));
    check(bytes && size >= sizeof(IMAGE_DOS_HEADER) && size <= 64 * 1024 * 1024, "Invalid embedded camera component");
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, bytes, sizeof(dos));
    check(dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
              static_cast<unsigned long long>(dos.e_lfanew) + sizeof(IMAGE_NT_HEADERS64) <= size,
          "Invalid camera PE header");
    IMAGE_NT_HEADERS64 nt{};
    std::memcpy(&nt, bytes + dos.e_lfanew, sizeof(nt));
    check(nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
              (nt.FileHeader.Characteristics & IMAGE_FILE_DLL) &&
              nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC,
          "Camera payload is not an x64 DLL");
    return {bytes, size};
}
fs::path component_root() {
    PWSTR value = nullptr;
    check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX64, 0, nullptr, &value)),
          "Program Files location is unavailable");
    fs::path root(value);
    CoTaskMemFree(value);
    return root / L"ExtraPane" / L"Components" / L"Camera";
}
fs::path legacy_root() {
    return component_root().parent_path().parent_path().parent_path() / L"ExtraPane Components" / L"Camera";
}
bool trusted_writer(PSID sid) {
    if (IsWellKnownSid(sid, WinBuiltinAdministratorsSid) || IsWellKnownSid(sid, WinLocalSystemSid)) return true;
    Local installer;
    check(ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464", &installer.value),
          "Resolve TrustedInstaller SID failed");
    return EqualSid(sid, installer.value) != FALSE;
}
void no_reparse(const fs::path& path) {
    for (auto current = path; !current.empty();) {
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES)
            check(!(attributes & FILE_ATTRIBUTE_REPARSE_POINT), "Camera setup refuses reparse-point paths");
        auto parent = current.parent_path();
        if (parent == current) break;
        current = std::move(parent);
    }
}
void protected_directory(const fs::path& path) {
    no_reparse(path);
    Local security;
    check(ConvertStringSecurityDescriptorToSecurityDescriptorW(
              L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)(A;OICI;GRGX;;;LS)", SDDL_REVISION_1,
              reinterpret_cast<PSECURITY_DESCRIPTOR*>(&security.value), nullptr),
          "Camera directory security failed");
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), security.value, FALSE};
    if (CreateDirectoryW(path.c_str(), &attributes)) return;
    check(GetLastError() == ERROR_ALREADY_EXISTS, "Create protected camera directory failed");
    PSID owner = nullptr;
    PACL acl = nullptr;
    Local existing;
    check(GetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                                OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &acl, nullptr,
                                reinterpret_cast<PSECURITY_DESCRIPTOR*>(&existing.value)) == ERROR_SUCCESS,
          "Inspect camera directory security failed");
    check((IsWellKnownSid(owner, WinBuiltinAdministratorsSid) || IsWellKnownSid(owner, WinLocalSystemSid)) && acl,
          "Camera directory has an unexpected owner or permissions");
    const ACCESS_MASK writes = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
                               FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void* raw = nullptr;
        check(GetAce(acl, i, &raw), "Inspect camera directory ACL failed");
        const auto* header = static_cast<ACE_HEADER*>(raw);
        if (header->AceFlags & INHERIT_ONLY_ACE) continue;
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        check(header->AceType == ACCESS_ALLOWED_ACE_TYPE, "Unsupported camera directory ACL");
        auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        auto* principal = &ace->SidStart;
        if (ace->Mask & writes)
            check(trusted_writer(principal), "Camera directory is writable by an unprivileged principal");
    }
}
class Registry final : public setup::Store {
public:
    setup::Text read(setup::Value value) override {
        Key key;
        auto result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path(value), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key.value);
        if (result == ERROR_FILE_NOT_FOUND) return std::nullopt;
        check(result == ERROR_SUCCESS, "Open camera registration failed");
        DWORD type = 0, size = 0;
        result = RegQueryValueExW(key.value, name(value), nullptr, &type, nullptr, &size);
        if (result == ERROR_FILE_NOT_FOUND) return std::nullopt;
        check(result == ERROR_SUCCESS && type == REG_SZ && size >= sizeof(wchar_t) && size <= 65536 &&
                  size % sizeof(wchar_t) == 0,
              "Unexpected camera registration value");
        std::wstring text(size / sizeof(wchar_t), L'\0');
        check(RegQueryValueExW(key.value, name(value), nullptr, &type, reinterpret_cast<BYTE*>(text.data()), &size) ==
                  ERROR_SUCCESS,
              "Read camera registration failed");
        check(!text.empty() && text.back() == L'\0', "Unterminated camera registration value");
        text.pop_back();
        check(text.find(L'\0') == std::wstring::npos, "Invalid camera registration text");
        return text;
    }
    void write(setup::Value value, const setup::Text& text) override {
        Key key;
        if (text) {
            check(RegCreateKeyExW(HKEY_LOCAL_MACHINE, path(value), 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY,
                                  nullptr, &key.value, nullptr) == ERROR_SUCCESS,
                  "Create camera registration failed");
            check(RegSetValueExW(key.value, name(value), 0, REG_SZ, reinterpret_cast<const BYTE*>(text->c_str()),
                                 static_cast<DWORD>((text->size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS,
                  "Write camera registration failed");
        } else {
            auto result =
                RegOpenKeyExW(HKEY_LOCAL_MACHINE, path(value), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key.value);
            if (result == ERROR_FILE_NOT_FOUND) return;
            check(result == ERROR_SUCCESS, "Open camera registration for removal failed");
            result = RegDeleteValueW(key.value, name(value));
            check(result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND, "Remove camera registration value failed");
        }
    }
    static const wchar_t* path(setup::Value value) {
        return value == setup::Value::server || value == setup::Value::threading ? inproc_key : class_key;
    }
    static const wchar_t* name(setup::Value value) {
        return value == setup::Value::threading ? L"ThreadingModel"
               : value == setup::Value::owner   ? L"ExtraPaneOwner"
                                                : nullptr;
    }
};
bool owned_path(const fs::path& path, const fs::path& root) {
    return path.parent_path() == root && setup::payload_name(path.filename().wstring());
}
bool class_exists() {
    Key key;
    const auto result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, class_key, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key.value);
    if (result == ERROR_FILE_NOT_FOUND) return false;
    check(result == ERROR_SUCCESS, "Inspect camera COM class failed");
    return true;
}
// An empty class key is not an existing COM registration. Older UI status
// checks could create this key without writing any values. Preserve every
// nonempty key, including unknown values or subkeys.
bool empty_class() {
    Key key;
    const auto result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, class_key, 0,
                                      KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_WOW64_64KEY, &key.value);
    if (result == ERROR_FILE_NOT_FOUND) return true;
    check(result == ERROR_SUCCESS, "Inspect empty camera COM class failed");
    DWORD children = 0, values = 0;
    check(RegQueryInfoKeyW(key.value, nullptr, nullptr, nullptr, &children, nullptr, nullptr, &values, nullptr, nullptr,
                           nullptr, nullptr) == ERROR_SUCCESS,
          "Inspect camera COM contents failed");
    return children == 0 && values == 0;
}
void verify_ownership(const setup::Snapshot& state, const fs::path& root) {
    check(setup::owner_matches(state) &&
              (owned_path(fs::path(*state[0]), root) || owned_path(fs::path(*state[0]), legacy_root())),
          "Camera COM class belongs to another registration; no changes made");
}
std::vector<unsigned char> read_file(const fs::path& path) {
    no_reparse(path);
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    check(file.value != INVALID_HANDLE_VALUE, "Read installed camera payload failed");
    LARGE_INTEGER size{};
    check(GetFileSizeEx(file.value, &size) && size.QuadPart > 0 && size.QuadPart <= 64 * 1024 * 1024,
          "Invalid installed camera payload size");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD got = 0;
    check(ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr) && got == bytes.size(),
          "Read installed camera payload failed");
    return bytes;
}
bool write_payload(const fs::path& path, std::span<const unsigned char> bytes) {
    no_reparse(path);
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) {
        check(GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS,
              "Create camera payload failed");
        check(sha256(read_file(path)) == sha256(bytes), "Existing camera payload does not match package");
        return false;
    }
    DWORD written = 0;
    if (!WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
        written != bytes.size() || !FlushFileBuffers(file.value)) {
        CloseHandle(file.value);
        file.value = INVALID_HANDLE_VALUE;
        check(DeleteFileW(path.c_str()) != FALSE,
              "Camera payload write failed and the new partial file could not be removed");
        throw std::runtime_error("Write camera payload failed; partial file removed");
    }
    return true;
}
void remove_empty_key(const wchar_t* path) {
    Key key;
    auto result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key.value);
    if (result != ERROR_SUCCESS) return;
    DWORD children = 0, values = 0;
    if (RegQueryInfoKeyW(key.value, nullptr, nullptr, nullptr, &children, nullptr, nullptr, &values, nullptr, nullptr,
                         nullptr, nullptr) != ERROR_SUCCESS ||
        children || values)
        return;
    RegCloseKey(key.value);
    key.value = nullptr;
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, path, KEY_WOW64_64KEY, 0);
}
int status() {
    Registry store;
    const auto state = setup::snapshot(store);
    if (setup::absent(state)) {
        check(empty_class(), "Camera COM class exists without ExtraPane ownership");
        std::wcout << L"not-installed\n";
        return 3;
    }
    const auto root = component_root();
    verify_ownership(state, root);
    if (fs::path(*state[0]).parent_path() == legacy_root()) {
        std::wcout << L"migration-required\n";
        return 3;
    }
    const fs::path path(*state[0]);
    const auto digest = sha256(read_file(path));
    check(path.filename() == L"ExtraPane.CameraSource." + digest + L".dll", "Registered camera payload hash mismatch");
    std::wcout << L"installed\npath=" << path.wstring() << L"\nsha256=" << digest << L'\n';
    return 0;
}
int modify(bool install) {
    check(IsUserAnAdmin() != FALSE, "Camera setup needs administrator permission");
    SYSTEM_INFO native{};
    GetNativeSystemInfo(&native);
    check(native.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64,
          "This camera component requires native x64 Windows; ARM64 Frame Server is not supported");
    Handle mutex(CreateMutexW(nullptr, FALSE, L"Global\\ExtraPane.CameraSetup.5A053AD3"));
    check(mutex.value && mutex.value != INVALID_HANDLE_VALUE, "Camera setup lock unavailable");
    const auto wait = WaitForSingleObject(mutex.value, 0);
    check(wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED, "Another camera setup operation is running");
    struct Unlock {
        HANDLE value;
        ~Unlock() { ReleaseMutex(value); }
    } unlock{mutex.value};
    Registry store;
    const auto state = setup::snapshot(store);
    auto root = component_root();
    if (!setup::absent(state))
        verify_ownership(state, component_root());
    else if (install)
        check(empty_class(), "Camera COM class already exists without ExtraPane ownership; no changes made");
    if (!install && !setup::absent(state)) root = fs::path(*state[0]).parent_path();
    if (!install && setup::absent(state)) {
        std::wcout << L"not-installed\n";
        return 0;
    }
    if (root == component_root()) protected_directory(root.parent_path().parent_path());
    protected_directory(root.parent_path());
    protected_directory(root);
    if (install) {
        // Validate the old tree before changing COM registration; unknown files
        // or modified payloads must leave the prior installation intact.
        if (fs::exists(legacy_root())) {
            protected_directory(legacy_root());
            for (const auto& item : fs::directory_iterator(legacy_root())) {
                const auto name = item.path().filename().wstring();
                check(item.is_regular_file() && setup::payload_name(name),
                      "Unknown legacy camera file; migration stopped");
                check(sha256(read_file(item.path())) ==
                          name.substr(std::wstring(L"ExtraPane.CameraSource.").size(), 64),
                      "Legacy camera payload changed; migration stopped");
            }
        }
        const auto bytes = payload();
        const auto digest = sha256(bytes);
        const auto path = root / (L"ExtraPane.CameraSource." + digest + L".dll");
        bool created = false;
        try {
            created = write_payload(path, bytes);
            setup::replace(store, setup::registration(path.wstring()));
        } catch (...) {
            // A rollback failure may leave the new path referenced. Never remove
            // that DLL or guess when registry state cannot be inspected.
            bool referenced = true;
            try {
                const auto current = store.read(setup::Value::server);
                referenced = current && *current == path.wstring();
            } catch (...) {}
            if (created && !referenced) DeleteFileW(path.c_str());
            remove_empty_key(inproc_key);
            remove_empty_key(class_key);
            throw;
        }
        bool reboot = false;
        if (fs::exists(legacy_root())) {
            protected_directory(legacy_root());
            for (const auto& item : fs::directory_iterator(legacy_root())) {
                const auto name = item.path().filename().wstring();
                check(item.is_regular_file() && setup::payload_name(name),
                      "Unknown legacy camera file; cleanup stopped");
                check(sha256(read_file(item.path())) ==
                          name.substr(std::wstring(L"ExtraPane.CameraSource.").size(), 64),
                      "Legacy camera payload changed; cleanup stopped");
            }
            for (const auto& item : fs::directory_iterator(legacy_root()))
                if (!DeleteFileW(item.path().c_str())) {
                    check(MoveFileExW(item.path().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT),
                          "Schedule legacy camera cleanup failed");
                    reboot = true;
                }
            if (reboot)
                check(MoveFileExW(legacy_root().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT),
                      "Schedule legacy camera directory cleanup failed");
            else
                RemoveDirectoryW(legacy_root().c_str());
            RemoveDirectoryW(legacy_root().parent_path().c_str());
        }
        std::wcout << L"installed\nsha256=" << digest << L'\n';
        return reboot ? 3010 : 0;
    }
    std::vector<fs::path> files;
    for (const auto& item : fs::directory_iterator(root)) {
        const auto name = item.path().filename().wstring();
        if (!setup::payload_name(name)) continue;
        check(sha256(read_file(item.path())) == name.substr(std::wstring(L"ExtraPane.CameraSource.").size(), 64),
              "Camera payload was modified; refusing to remove it");
        files.push_back(item.path());
    }
    setup::replace(store, {});
    remove_empty_key(inproc_key);
    remove_empty_key(class_key);
    bool reboot = false;
    for (const auto& path : files) {
        if (!DeleteFileW(path.c_str())) {
            const auto error = GetLastError();
            check(error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED, "Remove camera payload failed");
            check(MoveFileExW(path.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT), "Schedule camera cleanup failed");
            reboot = true;
        }
    }
    if (reboot)
        check(MoveFileExW(root.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT),
              "Schedule empty camera directory cleanup failed");
    else
        RemoveDirectoryW(root.c_str());
    RemoveDirectoryW(root.parent_path().c_str());
    std::wcout << (reboot ? L"removed; restart required to delete loaded camera files\n" : L"removed\n");
    return reboot ? 3010 : 0;
}
}
int wmain(int argc, wchar_t** argv) {
    const bool quiet = argc == 3 && std::wstring_view(argv[2]) == L"--quiet";
    try {
        check(argc == 2 || quiet, "Use --status, --check-package, --install or --remove [--quiet]");
        const std::wstring mode = argv[1];
        if (mode == L"--status") return status();
        if (mode == L"--check-package") {
            const auto bytes = payload();
            std::wcout << L"valid-x64-camera-payload\nsha256=" << sha256(bytes) << L'\n';
            return 0;
        }
        if (mode == L"--install") return modify(true);
        if (mode == L"--remove") return modify(false);
        throw std::runtime_error("Unknown camera setup operation");
    } catch (const std::exception& error) {
        std::cerr << "Camera setup: " << error.what() << '\n';
        if (!quiet && argc == 2 &&
            (std::wstring_view(argv[1]) == L"--install" || std::wstring_view(argv[1]) == L"--remove"))
            MessageBoxA(nullptr, error.what(), "ExtraPane camera setup failed", MB_OK | MB_ICONERROR);
        return 1;
    }
}
