#include <extrapane/usb.hpp>

namespace extrapane::usb {
Availability eligible(const Device& d) {
    if(d.id.empty() || !d.generation || !d.inventory_complete)
        return {false,"USB device ownership or disk inventory is incomplete"};
    if(!d.removable_storage || d.composite)
        return {false,"Only dedicated removable storage is supported"};
    if(d.system_disk || d.boot_disk || d.paging_disk)
        return {false,"System, boot and paging disks cannot be shared"};
    if(d.owned_elsewhere) return {false,"USB device is already owned by another session"};
    return {true,{}};
}
Transfer::~Transfer(){ if(state_!=State::idle) abandon(); }
void Transfer::abandon() noexcept {
    operations_.close_transport();
    operations_.stop_export();
    if(claimed_)operations_.release_claim();
    claimed_=false;
    state_=dismount_attempted_?State::recovery_required:State::idle;
}
Availability Transfer::start(const std::string& id,std::uint64_t generation,bool admitted) {
    if(state_!=State::idle)return {false,"USB transfer requires explicit recovery or is already active"};
    if(!admitted)return {false,"USB connection has not been admitted"};
    if(id.empty()||!generation)return {false,"Select a current local USB device"};
    state_=State::preparing;
    try {
        auto device=operations_.inspect();
        if(device.id!=id||device.generation!=generation){state_=State::idle;return {false,"USB selection changed; refresh devices"};}
        auto result=eligible(device);
        if(!result.available){state_=State::idle;return result;}
        result=operations_.claim(device);
        if(!result.available){state_=State::idle;return result;}
        claimed_=true;
        // Revalidate under the claim before touching volumes.
        auto current=operations_.inspect();
        result=eligible(current);
        if(current.id!=id||current.generation!=generation||!result.available){abandon();return {false,"USB inventory changed while acquiring ownership"};}
        dismount_attempted_=true;
        result=operations_.lock_and_dismount(current);
        if(!result.available){abandon();return result;}
        result=operations_.export_admitted(current);
        if(!result.available){abandon();return result;}
        state_=State::shared;return {true,{}};
    }catch(...){abandon();return {false,"USB preparation failed; check device state before retrying"};}
}
Availability Transfer::eject(){
    if(state_!=State::shared)return {false,"No active USB transfer to eject"};
    state_=State::ejecting;
    try {
        auto result=operations_.flush_and_eject_remote();
        if(!result.available){state_=State::shared;return result;}
        abandon();
        return {true,{}};
    }catch(...){state_=State::shared;return {false,"Remote eject failed; device remains shared"};}
}
void Transfer::transport_lost() noexcept {
    if(state_==State::idle||state_==State::recovery_required)return;
    abandon();
}
}
