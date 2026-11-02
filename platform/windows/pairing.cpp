#include <windows.h>
#include <wincred.h>
#include <dm/pairing.hpp>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace dm::security {
namespace {
struct CredDeleter { void operator()(CREDENTIALW* p) const { CredFree(p); } };
std::wstring target(const std::string& name) {
    validate_name(name);
    return L"DM/peers/" + std::wstring(name.begin(), name.end());
}
[[noreturn]] void failure(const char* operation) {
    throw std::runtime_error(std::string(operation) + " failed (Windows error " + std::to_string(GetLastError()) + ")");
}
}
void validate_name(const std::string& name) {
    if (name.empty() || name.size() > 64 || !std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    })) throw std::invalid_argument("Pair name must be 1-64 ASCII letters, digits, '-' or '_'");
}
Key parse_key(const std::string& hex) {
    if (hex.size() != 64) throw std::invalid_argument("Pairing key must be exactly 64 hex characters (32 random bytes)");
    const auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
        throw std::invalid_argument("Pairing key contains a non-hex character");
    };
    Key result{};
    try {
        for (std::size_t i = 0; i < result.size(); ++i) result[i] = static_cast<std::uint8_t>((digit(hex[i * 2]) << 4) | digit(hex[i * 2 + 1]));
    } catch (...) { SecureZeroMemory(result.data(), result.size()); throw; }
    return result;
}
void save_pairing(const std::string& name, const Key& key) {
    auto resource = target(name);
    CREDENTIALW* existing = nullptr;
    if (CredReadW(resource.c_str(), CRED_TYPE_GENERIC, 0, &existing)) {
        CredFree(existing);
        throw std::runtime_error("Pairing already exists; revoke it explicitly before replacing it");
    }
    if (GetLastError() != ERROR_NOT_FOUND) failure("Read pairing");
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = resource.data();
    credential.CredentialBlobSize = static_cast<DWORD>(key.size());
    credential.CredentialBlob = const_cast<BYTE*>(key.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t*>(L"DM paired peer");
    if (!CredWriteW(&credential, 0)) failure("Save pairing");
}
Key load_pairing(const std::string& name) {
    const auto resource = target(name);
    CREDENTIALW* value = nullptr;
    if (!CredReadW(resource.c_str(), CRED_TYPE_GENERIC, 0, &value)) failure("Load pairing (missing or revoked)");
    const std::unique_ptr<CREDENTIALW, CredDeleter> guard(value);
    if (value->CredentialBlobSize != Key{}.size()) throw std::runtime_error("Invalid pairing credential length");
    Key key{};
    std::copy_n(value->CredentialBlob, key.size(), key.begin());
    SecureZeroMemory(value->CredentialBlob, value->CredentialBlobSize);
    return key;
}
void revoke_pairing(const std::string& name) {
    const auto resource = target(name);
    if (!CredDeleteW(resource.c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND) failure("Revoke pairing");
}
void list_pairings() {
    DWORD count = 0;
    CREDENTIALW** values = nullptr;
    if (!CredEnumerateW(L"DM/peers/*", 0, &count, &values)) {
        if (GetLastError() == ERROR_NOT_FOUND) return;
        failure("List pairings");
    }
    struct Deleter { void operator()(CREDENTIALW** p) const { CredFree(p); } };
    const std::unique_ptr<CREDENTIALW*, Deleter> guard(values);
    for (DWORD i = 0; i < count; ++i) {
        SecureZeroMemory(values[i]->CredentialBlob, values[i]->CredentialBlobSize);
        std::wcout << values[i]->TargetName + 9 << L'\n';
    }
}
}
