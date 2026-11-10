#pragma once
#include <array>
#include <span>
#include <string_view>
namespace extrapane::usb_host_setup {
// Reviewed payload embedded in the delivered 2026.11.09-pre.112.1 helper
// (helper SHA256 7f19c49dda79b45e7ba571ab37f73fc934fcc8215ebc84d42daf0ec76c5feeda).
// This is a complete profile, never an arbitrary installed manifest allowlist.
inline constexpr std::array<std::wstring_view, 8> previous_payload{
    {L"ef38de703f6a0afee556dd833715847f7ace150a3c0f5782b563345edbd9a07a",
     L"f5a48b0cc8dae1607c2f0bae6b8dc13f2ecef69dbeaeaf34b4be8e280d34dba4",
     L"3bf49c01a0e9fb20f518bbd77bbc666f84bb5e99dacec27ca5a49176866859bc",
     L"107f8599ded6e8025f9518bdb9c4c94392c22df37207f887314d573d0fdf0484",
     L"d81d9e1f27a26a3a91b3e159d1d8f8351075fe6fb536032e0ba3102749eaeef5",
     L"774036a89c26228e29ed5cf19e8bf1ab43e65ef6948b325ff12ca6d9842f2e81",
     L"58a79965b610ba8b0778a228324dd21cbd8b3eae1eb3db05113951b10c41bf5f",
     L"a876b74be4367a37742e944ed47c534cd40b83088d2304cb469c7862747e126b"}};
inline bool reviewed_migration_payload(std::span<const std::wstring_view> installed,
                                       std::span<const std::wstring_view> current, bool previous_manifest) {
    if (!previous_manifest || installed.size() != previous_payload.size() || current.size() != previous_payload.size())
        return false;
    for (std::size_t i = 0; i < installed.size(); ++i) {
        if (i == 0) {
            if (installed[i] != previous_payload[i] && installed[i] != current[i]) return false;
        } else if (current[i] != previous_payload[i] || installed[i] != previous_payload[i])
            return false;
    }
    return true;
}
// Verification precedes any replacement. An exact old manifest with the new
// executable is the only resumable intermediate state; the driver set is fixed.
template <class Migration>
void migrate_reviewed_backend(Migration& migration) {
    migration.payload();
    migration.ownership();
    migration.idle();
    migration.replace();
}
// A present marker means verification failures must propagate, never fall through
// to a mutating repair. The backend exposes only read-only verification here;
// recovery records/bindings deliberately are not required to be empty for reuse.
template <class Verification>
bool reuse_exact_backend(Verification& verification) {
    if (!verification.has_manifest()) return false;
    verification.payload();
    verification.ownership();
    verification.registration();
    verification.monitor();
    return true;
}
}
