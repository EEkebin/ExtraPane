#include <extrapane/store.hpp>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <limits>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <bcrypt.h>
#include <dpapi.h>
#else
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <openssl/sha.h>
#endif

namespace extrapane::peers {
namespace {
constexpr std::size_t limit = 1024 * 1024, max_peers = 256;
struct Entry {
    RememberedDevice device;
    PairSecret secret;
};
struct State {
    DeviceId identity;
    std::map<DeviceId, Entry> peers, pending, tombstones;
};
void need(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void clear(Bytes& value) noexcept {
    volatile std::uint8_t* p = value.data();
    for (std::size_t i = 0; i < value.size(); ++i) p[i] = 0;
}
struct Sensitive {
    Bytes value;
    ~Sensitive() { clear(value); }
};
bool nonzero(const PairSecret& s) {
    return std::any_of(s.begin(), s.end(), [](auto n) { return n != 0; });
}
void string_valid(const std::string& value, std::size_t maximum, bool allow_empty) {
    need((allow_empty || !value.empty()) && value.size() <= maximum, "Invalid remembered device text");
    for (unsigned char c : value) need(c >= 32 && c != 127, "Invalid remembered device control text");
}
void valid(const RememberedDevice& d, const PairSecret& key, const DeviceId& self) {
    need(valid_id(d.id) && d.id != self && nonzero(key), "Invalid remembered identity or secret");
    string_valid(d.name, 128, false);
    validate(Catalog{d.id, 1, 1, d.name, {}});
    string_valid(d.address_hint, 255, true);
    for (unsigned char c : d.address_hint) need(c > 32 && c < 127, "Invalid remembered address hint");
    need(d.address_hint.empty() || d.port_hint != 0, "Invalid remembered port");
}
#ifdef _WIN32
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE v = INVALID_HANDLE_VALUE) : value(v) {}
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value);
    }
    Handle(const Handle&) = delete;
};
struct Sid {
    Bytes token;
    Sid() {
        Handle process;
        need(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process.value), "Read user token failed");
        DWORD length = 0;
        GetTokenInformation(process.value, TokenUser, nullptr, 0, &length);
        need(length > 0, "Read user identity failed");
        token.resize(length);
        need(GetTokenInformation(process.value, TokenUser, token.data(), length, &length), "Read user identity failed");
    }
    PSID value() { return reinterpret_cast<TOKEN_USER*>(token.data())->User.Sid; }
};
struct Security {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    Security() {
        Sid sid;
        LPWSTR text = nullptr;
        need(ConvertSidToStringSidW(sid.value(), &text), "Format user identity failed");
        std::wstring sddl = L"O:" + std::wstring(text) + L"D:P(A;;FA;;;SY)(A;;FA;;;" + std::wstring(text) + L")";
        LocalFree(text);
        need(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr),
             "Prepare private trust permissions failed");
        attributes.lpSecurityDescriptor = descriptor;
    }
    ~Security() {
        if (descriptor) LocalFree(descriptor);
    }
};
void private_handle(HANDLE handle, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};
    need(GetFileInformationByHandle(handle, &info), "Inspect trust path failed");
    need(!(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT), "Trust path cannot be a reparse point");
    need(bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory, "Unexpected trust path type");
    if (!directory) need(info.nNumberOfLinks == 1, "Trust file cannot have hard links");
    PSID owner = nullptr;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    need(GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
                         nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS,
         "Read trust permissions failed");
    struct Free {
        PSECURITY_DESCRIPTOR p;
        ~Free() { LocalFree(p); }
    } free{descriptor};
    Sid sid;
    need(owner && EqualSid(owner, sid.value()) && acl, "Trust storage must belong to this user");
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    need(GetSecurityDescriptorControl(descriptor, &control, &revision) && (control & SE_DACL_PROTECTED),
         "Trust storage must have private explicit permissions");
    BYTE systemBytes[SECURITY_MAX_SID_SIZE];
    DWORD systemSize = sizeof(systemBytes);
    need(CreateWellKnownSid(WinLocalSystemSid, nullptr, systemBytes, &systemSize), "Read system identity failed");
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void* item = nullptr;
        need(GetAce(acl, i, &item), "Read trust permission entry failed");
        auto header = static_cast<ACE_HEADER*>(item);
        need(header->AceType == ACCESS_ALLOWED_ACE_TYPE, "Unexpected trust access rule");
        auto ace = static_cast<ACCESS_ALLOWED_ACE*>(item);
        auto principal = reinterpret_cast<PSID>(&ace->SidStart);
        need(EqualSid(principal, sid.value()) || EqualSid(principal, systemBytes),
             "Trust storage is accessible to another principal");
    }
}
void directory(const std::filesystem::path& path) {
    Security security;
    if (!CreateDirectoryW(path.c_str(), &security.attributes))
        need(GetLastError() == ERROR_ALREADY_EXISTS, "Create private trust directory failed");
    Handle h(CreateFileW(path.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    need(h.value != INVALID_HANDLE_VALUE, "Open private trust directory failed");
    private_handle(h.value, true);
}
struct Lock {
    Handle handle;
    explicit Lock(const std::filesystem::path& path) {
        Security security;
        handle.value = CreateFileW((path / L"trust.lock").c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, &security.attributes, OPEN_ALWAYS,
                                   FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        need(handle.value != INVALID_HANDLE_VALUE, "Open trust lock failed");
        private_handle(handle.value, false);
        OVERLAPPED overlap{};
        need(LockFileEx(handle.value, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlap), "Lock trust store failed");
    }
};
PairSecret random() {
    PairSecret result{};
    need(BCryptGenRandom(nullptr, result.data(), result.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0,
         "Generate device identity failed");
    need(nonzero(result), "Random identity was zero");
    return result;
}
Bytes digest(std::span<const std::uint8_t> bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    need(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "Open trust digest failed");
    struct Close {
        BCRYPT_ALG_HANDLE h;
        ~Close() { BCryptCloseAlgorithmProvider(h, 0); }
    } close{algorithm};
    Bytes out(32);
    need(BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()),
                    out.data(), out.size()) >= 0,
         "Hash trust store failed");
    return out;
}
struct LocalBlob {
    DATA_BLOB blob{};
    ~LocalBlob() {
        if (blob.pbData) {
            SecureZeroMemory(blob.pbData, blob.cbData);
            LocalFree(blob.pbData);
        }
    }
};
Bytes protect(Bytes bytes) {
    Sensitive plaintext{std::move(bytes)};
    DATA_BLOB input{static_cast<DWORD>(plaintext.value.size()), plaintext.value.data()};
    LocalBlob output;
    need(CryptProtectData(&input, L"ExtraPane remembered devices", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &output.blob),
         "Protect remembered device secrets failed");
    return Bytes(output.blob.pbData, output.blob.pbData + output.blob.cbData);
}
Bytes unprotect(Bytes bytes) {
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), bytes.data()};
    LocalBlob output;
    need(CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output.blob),
         "Cannot decrypt trust store for this user");
    need(output.blob.cbData <= limit, "Decrypted trust store is oversized");
    return Bytes(output.blob.pbData, output.blob.pbData + output.blob.cbData);
}
std::optional<Bytes> read_file(const std::filesystem::path& path) {
    Handle h(CreateFileW(path.c_str(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (h.value == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return {};
        throw std::runtime_error("Read trust store failed");
    }
    private_handle(h.value, false);
    LARGE_INTEGER length{};
    need(GetFileSizeEx(h.value, &length) && length.QuadPart > 0 && length.QuadPart <= limit,
         "Invalid trust store size");
    Bytes bytes(static_cast<std::size_t>(length.QuadPart));
    DWORD got = 0;
    need(ReadFile(h.value, bytes.data(), bytes.size(), &got, nullptr) && got == bytes.size(),
         "Read complete trust store failed");
    return bytes;
}
void atomic_file(const std::filesystem::path& path, const Bytes& bytes) {
    if (auto existing = read_file(path); existing) {} // Reject foreign/reparse/hard-linked destinations.
    auto nonce = random();
    std::wstring suffix = L".tmp-";
    for (auto b : nonce) {
        suffix += L"0123456789abcdef"[b >> 4];
        suffix += L"0123456789abcdef"[b & 15];
    }
    auto temporary = path;
    temporary += suffix;
    Security security;
    Handle h(CreateFileW(temporary.c_str(), GENERIC_WRITE | READ_CONTROL, 0, &security.attributes, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL, nullptr));
    need(h.value != INVALID_HANDLE_VALUE, "Create private trust replacement failed");
    try {
        private_handle(h.value, false);
        DWORD wrote = 0;
        need(WriteFile(h.value, bytes.data(), bytes.size(), &wrote, nullptr) && wrote == bytes.size() &&
                 FlushFileBuffers(h.value),
             "Persist trust replacement failed");
        CloseHandle(h.value);
        h.value = INVALID_HANDLE_VALUE;
        need(MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
             "Commit trust replacement failed");
    } catch (...) {
        if (h.value != INVALID_HANDLE_VALUE) {
            CloseHandle(h.value);
            h.value = INVALID_HANDLE_VALUE;
        }
        DeleteFileW(temporary.c_str());
        throw;
    }
}
#else
struct Fd {
    int value = -1;
    explicit Fd(int n = -1) : value(n) {}
    ~Fd() {
        if (value >= 0) ::close(value);
    }
    Fd(const Fd&) = delete;
};
void private_fd(int fd, bool directory) {
    struct stat s{};
    need(fstat(fd, &s) == 0, "Inspect trust path failed");
    need(s.st_uid == geteuid() && (s.st_mode & 0777) == (directory ? 0700 : 0600),
         "Trust storage must be private to this user");
    need(directory ? S_ISDIR(s.st_mode) : (S_ISREG(s.st_mode) && s.st_nlink == 1),
         "Unexpected trust path type or links");
}
void directory(const std::filesystem::path& path) {
    if (mkdir(path.c_str(), 0700) != 0) need(errno == EEXIST, "Create private trust directory failed");
    Fd fd(open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    need(fd.value >= 0, "Open private trust directory failed");
    private_fd(fd.value, true);
}
struct Lock {
    Fd fd;
    explicit Lock(const std::filesystem::path& path)
        : fd(open((path / "trust.lock").c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600)) {
        need(fd.value >= 0, "Open trust lock failed");
        private_fd(fd.value, false);
        while (flock(fd.value, LOCK_EX) != 0) need(errno == EINTR, "Lock trust store failed");
    }
};
PairSecret random() {
    Fd fd(open("/dev/urandom", O_RDONLY | O_CLOEXEC));
    need(fd.value >= 0, "Open secure random source failed");
    PairSecret out{};
    std::size_t done = 0;
    while (done < out.size()) {
        const auto n = read(fd.value, out.data() + done, out.size() - done);
        if (n < 0 && errno == EINTR) continue;
        need(n > 0, "Generate device identity failed");
        done += n;
    }
    need(nonzero(out), "Random identity was zero");
    return out;
}
Bytes digest(std::span<const std::uint8_t> bytes) {
    Bytes out(32);
    need(SHA256(bytes.data(), bytes.size(), out.data()) != nullptr, "Hash trust store failed");
    return out;
}
Bytes protect(Bytes bytes) {
    return bytes;
}
Bytes unprotect(Bytes bytes) {
    return bytes;
}
std::optional<Bytes> read_file(const std::filesystem::path& path) {
    Fd fd(open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if (fd.value < 0) {
        if (errno == ENOENT) return {};
        throw std::runtime_error("Read trust store failed");
    }
    private_fd(fd.value, false);
    struct stat s{};
    need(fstat(fd.value, &s) == 0 && s.st_size > 0 && s.st_size <= limit, "Invalid trust store size");
    Bytes out(s.st_size);
    std::size_t done = 0;
    while (done < out.size()) {
        auto n = read(fd.value, out.data() + done, out.size() - done);
        if (n < 0 && errno == EINTR) continue;
        need(n > 0, "Read complete trust store failed");
        done += n;
    }
    return out;
}
void atomic_file(const std::filesystem::path& path, const Bytes& bytes) {
    if (auto existing = read_file(path); existing) {};
    auto nonce = random();
    std::string suffix = ".tmp-";
    for (auto b : nonce) {
        suffix += "0123456789abcdef"[b >> 4];
        suffix += "0123456789abcdef"[b & 15];
    }
    auto temporary = path;
    temporary += suffix;
    Fd fd(open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    need(fd.value >= 0, "Create private trust replacement failed");
    bool renamed = false;
    try {
        private_fd(fd.value, false);
        std::size_t done = 0;
        while (done < bytes.size()) {
            auto n = write(fd.value, bytes.data() + done, bytes.size() - done);
            if (n < 0 && errno == EINTR) continue;
            need(n > 0, "Write trust replacement failed");
            done += n;
        }
        need(fsync(fd.value) == 0, "Flush trust replacement failed");
        need(rename(temporary.c_str(), path.c_str()) == 0, "Commit trust replacement failed");
        renamed = true;
        Fd dir(open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        need(dir.value >= 0 && fsync(dir.value) == 0, "Flush trust directory failed");
    } catch (...) {
        if (!renamed) unlink(temporary.c_str());
        throw;
    }
}
#endif
void put(Bytes& out, std::uint64_t n, unsigned size) {
    for (unsigned i = size; i; --i) out.push_back(static_cast<std::uint8_t>(n >> ((i - 1) * 8)));
}
void put(Bytes& out, const std::string& s) {
    put(out, s.size(), 2);
    out.insert(out.end(), s.begin(), s.end());
}
struct Reader {
    std::span<const std::uint8_t> in;
    std::uint64_t number(unsigned n) {
        need(in.size() >= n, "Truncated trust store");
        std::uint64_t v = 0;
        for (unsigned i = 0; i < n; ++i) v = (v << 8) | in[i];
        in = in.subspan(n);
        return v;
    }
    PairSecret key() {
        need(in.size() >= 32, "Truncated trust key");
        PairSecret s{};
        std::copy_n(in.begin(), 32, s.begin());
        in = in.subspan(32);
        return s;
    }
    std::string text(unsigned limit) {
        auto size = number(2);
        need(size <= limit && size <= in.size(), "Invalid trust text length");
        std::string s(reinterpret_cast<const char*>(in.data()), size);
        in = in.subspan(size);
        return s;
    }
};
void encode_entries(Bytes& bytes, const std::map<DeviceId, Entry>& entries, const DeviceId& identity) {
    need(entries.size() <= max_peers, "Trust record count exceeds limit");
    put(bytes, entries.size(), 2);
    for (const auto& [id, e] : entries) {
        need(id == e.device.id, "Trust entry identity mismatch");
        valid(e.device, e.secret, identity);
        bytes.insert(bytes.end(), id.begin(), id.end());
        bytes.insert(bytes.end(), e.secret.begin(), e.secret.end());
        put(bytes, e.device.name);
        put(bytes, e.device.address_hint);
        put(bytes, e.device.port_hint, 2);
    }
}
Bytes encode(const State& state) {
    need(valid_id(state.identity), "Invalid trust store identity");
    Bytes bytes{'E', 'P', 'T', 'S', 0, 2, 0, 0};
    bytes.insert(bytes.end(), state.identity.begin(), state.identity.end());
    encode_entries(bytes, state.peers, state.identity);
    encode_entries(bytes, state.pending, state.identity);
    encode_entries(bytes, state.tombstones, state.identity);
    auto sum = digest(bytes);
    bytes.insert(bytes.end(), sum.begin(), sum.end());
    need(bytes.size() <= limit, "Trust store exceeds limit");
    return protect(std::move(bytes));
}
void decode_entries(Reader& in, std::map<DeviceId, Entry>& entries, const DeviceId& identity,
                    std::set<DeviceId>& seen) {
    const auto count = in.number(2);
    need(count <= max_peers, "Trust record count exceeds limit");
    for (unsigned i = 0; i < count; ++i) {
        Entry e;
        e.device.id = in.key();
        e.secret = in.key();
        e.device.name = in.text(128);
        e.device.address_hint = in.text(255);
        e.device.port_hint = static_cast<std::uint16_t>(in.number(2));
        valid(e.device, e.secret, identity);
        need(seen.insert(e.device.id).second, "Duplicate trusted identity");
        entries.emplace(e.device.id, std::move(e));
    }
}
State decode(Bytes bytes) {
    Sensitive plain{unprotect(std::move(bytes))};
    need(plain.value.size() >= 74 && plain.value.size() <= limit, "Invalid trust store size");
    const auto all = std::span(plain.value);
    const auto payload = all.first(all.size() - 32);
    auto sum = digest(payload);
    need(std::equal(sum.begin(), sum.end(), all.end() - 32), "Trust store integrity check failed");
    Reader in{payload};
    need(in.number(4) == 0x45505453, "Unknown trust store format");
    const auto version = in.number(2);
    need((version == 1 || version == 2) && in.number(2) == 0, "Unknown trust store format");
    State state;
    state.identity = in.key();
    need(valid_id(state.identity), "Invalid stored device identity");
    std::set<DeviceId> seen;
    decode_entries(in, state.peers, state.identity, seen);
    if (version == 2) {
        decode_entries(in, state.pending, state.identity, seen);
        decode_entries(in, state.tombstones, state.identity, seen);
    }
    need(in.in.empty(), "Trailing trust data");
    return state;
}
void tombstone(State& state, Entry entry) {
    // A deterministic bounded replay cache. Eviction never restores normal trust.
    if (!state.tombstones.contains(entry.device.id) && state.tombstones.size() >= max_peers)
        state.tombstones.erase(state.tombstones.begin());
    const auto id = entry.device.id;
    state.tombstones[id] = std::move(entry);
}
State load(const std::filesystem::path& directory, const DeviceId& expected) {
    auto bytes = read_file(directory / "trust-v1.bin");
    need(bytes.has_value(), "Trust store disappeared; refusing to reset identity");
    auto state = decode(std::move(*bytes));
    need(state.identity == expected, "Local device identity changed; reopen explicitly");
    return state;
}
}
TrustStore::TrustStore(std::filesystem::path path)
    : directory_(std::filesystem::absolute(std::move(path)).lexically_normal()) {
    need(directory_.has_filename(), "Invalid trust directory");
    directory(directory_);
    Lock lock(directory_);
    auto bytes = read_file(directory_ / "trust-v1.bin");
    if (bytes) {
        identity_ = decode(std::move(*bytes)).identity;
        return;
    }
    State state{random(), {}};
    atomic_file(directory_ / "trust-v1.bin", encode(state));
    identity_ = state.identity;
}
std::string TrustStore::pairing_password() const {
    directory(directory_);
    Lock lock(directory_);
    (void)load(directory_, identity_);
    auto bytes = read_file(directory_ / "pairing-v1.bin");
    if (!bytes) return {};
    Sensitive plain{unprotect(std::move(*bytes))};
    need(plain.value.size() >= 70 && plain.value.size() <= 1094, "Invalid pairing configuration size");
    const auto all = std::span(plain.value), payload = all.first(all.size() - 32);
    const auto sum = digest(payload);
    need(std::equal(sum.begin(), sum.end(), all.end() - 32), "Pairing configuration integrity check failed");
    Reader in{payload};
    need(in.number(4) == 0x45505031 && in.key() == identity_, "Pairing configuration identity changed");
    auto value = in.text(1024);
    need(in.in.empty(), "Trailing pairing configuration data");
    return value;
}
void TrustStore::set_pairing_password(const std::string& value) {
    need(value.size() <= 1024, "Pairing password exceeds 1024 bytes");
    directory(directory_);
    Lock lock(directory_);
    (void)load(directory_, identity_);
    Sensitive plain{Bytes{'E', 'P', 'P', '1'}};
    plain.value.insert(plain.value.end(), identity_.begin(), identity_.end());
    put(plain.value, value);
    auto sum = digest(plain.value);
    plain.value.insert(plain.value.end(), sum.begin(), sum.end());
    atomic_file(directory_ / "pairing-v1.bin", protect(std::move(plain.value)));
}
std::vector<RememberedDevice> TrustStore::devices() const {
    directory(directory_);
    Lock lock(directory_);
    const auto state = load(directory_, identity_);
    std::vector<RememberedDevice> result;
    for (const auto& [id, e] : state.peers) result.push_back(e.device);
    return result;
}
std::optional<PairSecret> TrustStore::secret(const DeviceId& id) const {
    directory(directory_);
    Lock lock(directory_);
    const auto state = load(directory_, identity_);
    auto found = state.peers.find(id);
    return found == state.peers.end() ? std::optional<PairSecret>{} : found->second.secret;
}
void TrustStore::remember(RememberedDevice peer, const PairSecret& secret) {
    valid(peer, secret, identity_);
    directory(directory_);
    Lock lock(directory_);
    auto state = load(directory_, identity_);
    need(!state.peers.contains(peer.id), "Device is already remembered; forget it before pairing again");
    need(!state.pending.contains(peer.id), "Device revocation is pending; finish forgetting before pairing again");
    need(state.peers.size() < max_peers, "Remembered device limit reached");
    state.tombstones.erase(peer.id);
    state.peers[peer.id] = {std::move(peer), secret};
    atomic_file(directory_ / "trust-v1.bin", encode(state));
}
void TrustStore::update_hint(RememberedDevice peer) {
    directory(directory_);
    Lock lock(directory_);
    auto state = load(directory_, identity_);
    auto old = state.peers.find(peer.id);
    need(old != state.peers.end(), "Cannot update an unknown device");
    valid(peer, old->second.secret, identity_);
    old->second.device = std::move(peer);
    atomic_file(directory_ / "trust-v1.bin", encode(state));
}
bool TrustStore::forget(const DeviceId& id) {
    directory(directory_);
    Lock lock(directory_);
    auto state = load(directory_, identity_);
    if (!state.peers.erase(id)) return false;
    atomic_file(directory_ / "trust-v1.bin", encode(state));
    return true;
}
bool TrustStore::begin_forget(const DeviceId& id) {
    directory(directory_);
    Lock lock(directory_);
    auto state = load(directory_, identity_);
    if (state.pending.contains(id)) return true;
    auto found = state.peers.find(id);
    if (found == state.peers.end()) return false;
    need(state.pending.size() < max_peers, "Pending revocation limit reached");
    state.pending.emplace(id, found->second);
    state.peers.erase(found);
    atomic_file(directory_ / "trust-v1.bin", encode(state));
    return true;
}
bool TrustStore::dismiss_forget(const DeviceId& id) {
    directory(directory_);
    Lock lock(directory_);
    auto state = load(directory_, identity_);
    auto found = state.pending.find(id);
    if (found == state.pending.end()) return false;
    tombstone(state, found->second);
    state.pending.erase(found);
    atomic_file(directory_ / "trust-v1.bin", encode(state));
    return true;
}
std::vector<RememberedDevice> TrustStore::pending_forgets() const {
    directory(directory_);
    Lock lock(directory_);
    const auto state = load(directory_, identity_);
    std::vector<RememberedDevice> result;
    for (const auto& [id, e] : state.pending) result.push_back(e.device);
    return result;
}
std::optional<PairSecret> TrustStore::revocation_secret(const DeviceId& id) const {
    directory(directory_);
    Lock lock(directory_);
    const auto state = load(directory_, identity_);
    if (auto found = state.pending.find(id); found != state.pending.end()) return found->second.secret;
    if (auto found = state.tombstones.find(id); found != state.tombstones.end()) return found->second.secret;
    return {};
}
bool TrustStore::acknowledge_forget(const DeviceId& id, const PairSecret& expected) {
    directory(directory_);
    Lock lock(directory_);
    auto state = load(directory_, identity_);
    auto found = state.pending.find(id);
    if (found == state.pending.end()) {
        auto old = state.tombstones.find(id);
        return old != state.tombstones.end() && old->second.secret == expected;
    }
    if (found->second.secret != expected) return false;
    tombstone(state, found->second);
    state.pending.erase(found);
    atomic_file(directory_ / "trust-v1.bin", encode(state));
    return true;
}
bool TrustStore::receive_forget(const DeviceId& id, const PairSecret& expected) {
    directory(directory_);
    Lock lock(directory_);
    auto state = load(directory_, identity_);
    if (auto found = state.peers.find(id); found != state.peers.end()) {
        if (found->second.secret != expected) return false;
        tombstone(state, found->second);
        state.peers.erase(found);
    } else if (auto found = state.pending.find(id); found != state.pending.end()) {
        if (found->second.secret != expected) return false;
        tombstone(state, found->second);
        state.pending.erase(found);
    } else {
        auto old = state.tombstones.find(id);
        return old != state.tombstones.end() && old->second.secret == expected;
    }
    atomic_file(directory_ / "trust-v1.bin", encode(state));
    return true;
}

}
