#pragma once
#include <dm/usb_inventory.hpp>
#include <array>
#include <memory>
namespace dm::usb {
enum class RecoveryState:std::uint8_t {prepared=1,offline=2,binding_intent=3,bound=4,released=5,restored=6,ejected=7};
struct RecoveryRecord {
    RecoveryState state=RecoveryState::prepared;
    std::uint32_t disk_number=0;
    std::uint64_t prior_attributes=0;
    std::array<std::uint8_t,32> fingerprint{};
    std::array<std::uint8_t,16> owner{};
    std::string bus_id,binding_id;
    bool operator==(const RecoveryRecord&)const=default;
};
struct PendingRecovery {
    std::wstring journal_id;
    std::optional<RecoveryRecord> record;
    std::string error;
};
// Read-only registry queries. Throws if the protected registry cannot be read or
// trusted; a malformed individual journal is returned with an error, untouched.
std::vector<PendingRecovery> pending_recoveries();
// Match the stable journal key, not the full fingerprint: changed partitions or
// disk numbering must not make an unresolved journal disappear from export UI.
bool has_pending_recovery(const Disk&,std::span<const PendingRecovery>);
// Hash-only diagnostic for the exact current identity; no disk/registry mutation.
std::array<std::uint8_t,32> recovery_fingerprint(const Disk&);
bool matches_recovery(const Disk&,const PendingRecovery&);
// Pure journal-state policy; caller must additionally prove current local USB
// binding absence and revalidate the selected disk before recovery.
extrapane::Availability recovery_state_allows_restore(const RecoveryRecord&);
// Pure fixed-version, bounded journal codec. No registry or disk access.
std::vector<std::uint8_t> encode_recovery(const RecoveryRecord& record);
RecoveryRecord decode_recovery(std::span<const std::uint8_t> bytes);
bool valid_recovery_transition(RecoveryState from,RecoveryState to)noexcept;
class RecoveryGuard {
public:
    virtual ~RecoveryGuard()=default;
    virtual extrapane::Availability quarantine(const Disk&)=0;
    virtual extrapane::Availability verify(const Disk&)=0;
    virtual extrapane::Availability binding_intent(const Disk&,const std::string& bus)=0;
    virtual extrapane::Availability binding_acquired(const Disk&,const std::wstring& guid)=0;
    // Called only after verified native destination eject on the admitted session.
    virtual extrapane::Availability destination_ejected(const Disk&)=0;
    virtual extrapane::Availability binding_released(const Disk&,const std::wstring& guid)=0;
};
using LockedDiskVerifier=std::function<extrapane::Availability(const Disk&)>;
// Inert constructor. A trusted StorageHandles owner must prove volume locks and
// revalidate its physical handle immediately before quarantine. Never use a
// callback that merely returns true. Runtime methods require administrator rights.
std::unique_ptr<RecoveryGuard> native_recovery_guard(LockedDiskVerifier verify_locked);
// Explicit recovery only: caller must first obtain affirmative destination eject
// and exact backend binding-release evidence. The verifier is trusted local code,
// never a remote flag, and must inspect persisted bus/binding identity itself.
// No destructor invokes this. A changed disk number/layout/identity fails closed.
using RecoveryReleaseVerifier=std::function<extrapane::Availability(const RecoveryRecord&)>;
extrapane::Availability recover_quarantined_disk(const Disk& locally_selected,
    const RecoveryReleaseVerifier& verify_destination_ejected_and_binding_released);
}
