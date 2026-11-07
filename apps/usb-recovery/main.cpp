#include <dm/usb_recovery.hpp>
#include <dm/usb_runner.hpp>
#include <windows.h>
#include <objbase.h>
#include <winioctl.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>

using namespace dm::usb;
using extrapane::Availability;
namespace {
std::string utf8(const std::wstring& value){
    const auto length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(!length&&!value.empty())throw std::runtime_error("Invalid local recovery identifier");
    std::string text(length,0);if(length)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),text.data(),length,nullptr,nullptr);return text;
}
std::string json(const std::string& value){
    std::string text="\"";constexpr char digits[]="0123456789abcdef";
    for(const unsigned char c:value){
        if(c=='"'||c=='\\'){text+='\\';text+=static_cast<char>(c);}
        else if(c<32){text+="\\u00";text+=digits[c>>4];text+=digits[c&15];}
        else text+=static_cast<char>(c);
    }
    return text+'"';
}
const char* state_name(RecoveryState state){
    switch(state){case RecoveryState::prepared:return "prepared";case RecoveryState::offline:return "offline";
    case RecoveryState::binding_intent:return "binding-intent";case RecoveryState::bound:return "bound";
    case RecoveryState::released:return "released";case RecoveryState::restored:return "restored";case RecoveryState::ejected:return "ejected";default:return "invalid";}
}
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool equal_id(const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())==0;}
void list(){
    const auto pending=pending_recoveries();std::cout<<"{\"journals\":[";bool first=true;
    for(const auto& item:pending){
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"id\":"<<json(utf8(item.journal_id));
        if(item.record){
            const auto policy=recovery_state_allows_restore(*item.record);
            std::cout<<",\"disk\":"<<item.record->disk_number<<",\"state\":"<<json(state_name(item.record->state))
                <<",\"canAttemptRecovery\":"<<(policy.available?"true":"false")<<",\"reason\":"<<json(policy.reason);
        }else std::cout<<",\"state\":\"invalid\",\"canAttemptRecovery\":false,\"reason\":"<<json(item.error);
        std::cout<<'}';
    }
    std::cout<<"]}\n";
}
std::string hex_bytes(std::span<const std::uint8_t> bytes){std::string text;for(auto b:bytes){text+="0123456789abcdef"[b>>4];text+="0123456789abcdef"[b&15];}return text;}
void inspect(const std::wstring& id){
    const auto pending=pending_recoveries();std::optional<PendingRecovery> selected;
    for(const auto& item:pending)if(item.journal_id==id){require(!selected,"Duplicate journal identity");selected=item;}
    require(selected&&selected->record,"Recovery journal unavailable or malformed");
    // Inspection never reserves a backend, binds USB, opens a writable disk,
    // changes attributes or treats the dummy ownership probe as recovery proof.
    const auto snapshot=inventory([](const std::wstring&)->std::optional<bool>{return false;});
    std::cout<<"{\"journal\":"<<json(utf8(id))<<",\"expectedDisk\":"<<selected->record->disk_number
        <<",\"journalVersion\":"<<static_cast<unsigned>(selected->record->version)
        <<",\"expectedFingerprint\":"<<json(hex_bytes(selected->record->fingerprint))<<",\"ownershipChecked\":false,\"disks\":[";
    bool first=true;
    for(const auto& disk:snapshot.disks){
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"number\":"<<disk.disk_number<<",\"instance\":"<<json(utf8(disk.instance_id))
            <<",\"usbInstance\":"<<json(utf8(disk.usb_instance_id))<<",\"sameJournalKey\":"<<(has_pending_recovery(disk,std::span(&*selected,1))?"true":"false")
            <<",\"descriptorBytes\":"<<disk.descriptor_bytes.size()<<",\"layoutBytes\":"<<disk.layout_bytes.size();
        std::cout<<",\"matchesRecordedIdentity\":"<<(matches_recovery(disk,*selected)?"true":"false");
        try{const auto digest=recovery_fingerprint(disk);std::cout<<",\"legacyV1Fingerprint\":"<<json(hex_bytes(digest));}catch(const std::exception& error){std::cout<<",\"legacyFingerprintError\":"<<json(error.what());}
        if(selected->record->version==2)try{const auto digest=stable_recovery_fingerprint(disk);std::cout<<",\"stableV2Fingerprint\":"<<json(hex_bytes(digest));}catch(const std::exception& error){std::cout<<",\"stableFingerprintError\":"<<json(error.what());}
        const auto handle=CreateFileW(disk.interface_path.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        STORAGE_HOTPLUG_INFO hotplug{};DWORD returned=0;
        if(handle!=INVALID_HANDLE_VALUE){
            if(DeviceIoControl(handle,IOCTL_STORAGE_GET_HOTPLUG_INFO,nullptr,0,&hotplug,sizeof(hotplug),&returned,nullptr)&&returned>=sizeof(hotplug))
                std::cout<<",\"mediaRemovable\":"<<(hotplug.MediaRemovable?"true":"false")<<",\"mediaHotplug\":"<<(hotplug.MediaHotplug?"true":"false")<<",\"deviceHotplug\":"<<(hotplug.DeviceHotplug?"true":"false");
            CloseHandle(handle);
        }
        std::cout<<",\"descriptorHex\":"<<json(hex_bytes(disk.descriptor_bytes))<<",\"layoutHex\":"<<json(hex_bytes(disk.layout_bytes))<<",\"unresolved\":[";
        bool firstReason=true;for(const auto& reason:disk.unresolved){if(!firstReason)std::cout<<',';firstReason=false;std::cout<<json(reason);}std::cout<<"]}";
    }
    std::cout<<"],\"inventoryErrors\":[";first=true;for(const auto& error:snapshot.errors){if(!first)std::cout<<',';first=false;std::cout<<json(error);}std::cout<<"]}\n";
}
void recover(const std::wstring& id){
    require(id.size()==64&&std::all_of(id.begin(),id.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');}),"Use an exact journal ID from --list, not a disk path or number");
    const auto pending=pending_recoveries();std::optional<PendingRecovery> selected;
    for(const auto& item:pending)if(item.journal_id==id){require(!selected,"Duplicate recovery journal identity");selected=item;}
    require(selected&&selected->record,"The selected journal is missing or corrupt; no disk state was changed");
    const auto policy=recovery_state_allows_restore(*selected->record);
    if(!policy.available)throw std::runtime_error(policy.reason);
    auto runner=native_host_runner();const auto ready=runner->ready();if(!ready.available)throw std::runtime_error(ready.reason);
    const auto devices=runner->devices();
    const auto snapshot=inventory([&](const std::wstring& instance)->std::optional<bool>{
        std::optional<bool> owned;
        for(const auto& device:devices)if(equal_id(device.instance_id,instance)){
            if(owned)return {};
            const bool ownEjectedBinding=selected->record->state==RecoveryState::ejected&&
                _stricmp(utf8(device.binding_id).c_str(),selected->record->binding_id.c_str())==0&&device.bus_id==selected->record->bus_id;
            owned=(!device.binding_id.empty()&&!ownEjectedBinding)||device.attached||device.forced;
        }
        return owned;
    });
    require(snapshot.errors.empty(),"Current local storage inventory is incomplete");
    std::optional<Disk> disk;
    for(const auto& candidate:snapshot.disks)if(matches_recovery(candidate,*selected)){
        require(!disk,"More than one local disk matches this journal");disk=candidate;
    }
    require(disk.has_value(),"The original disk identity, number, or partition layout changed; recovery was refused");
    require(extrapane::usb::eligible(disk->facts).available,"Current storage inventory is protected, incomplete, or still owned; recovery was refused");
    const auto reserved=runner->reserve(disk->usb_instance_id);if(!reserved.available)throw std::runtime_error(reserved.reason);
    struct Release {HostRunner& runner;~Release(){runner.release();}} release{*runner};
    const auto result=recover_quarantined_disk(*disk,[&](const RecoveryRecord& record)->Availability{
        if(record!=*selected->record)return {false,"Recovery journal changed after selection"};
        auto allowed=recovery_state_allows_restore(record);if(!allowed.available)return allowed;
        std::optional<HostDevice> device;
        for(const auto& current:runner->devices()){
            if(!record.binding_id.empty()&&_stricmp(utf8(current.binding_id).c_str(),record.binding_id.c_str())==0&&
               !equal_id(current.instance_id,disk->usb_instance_id))return {false,"The journal's USB binding identifies a different device"};
            if(equal_id(current.instance_id,disk->usb_instance_id)){
                if(device)return {false,"USB instance identity is ambiguous"};device=current;
            }
        }
        if(!device||device->forced||device->attached)return {false,"Source USB device is missing or still attached"};
        if(!device->binding_id.empty()){
            if(record.state!=RecoveryState::ejected||device->bus_id!=record.bus_id||
               _stricmp(utf8(device->binding_id).c_str(),record.binding_id.c_str())!=0)return {false,"Source USB binding does not match this verified eject journal"};
            const auto adopted=runner->adopt_ejected_binding(*disk,*selected);if(!adopted.available)return adopted;
            const auto removed=runner->unbind(device->binding_id);if(!removed.available)return removed;
            bool found=false;
            for(const auto& after:runner->devices()){
                if(_stricmp(utf8(after.binding_id).c_str(),record.binding_id.c_str())==0)return {false,"Owned USB binding removal is incomplete"};
                if(equal_id(after.instance_id,disk->usb_instance_id)){
                    if(found||after.attached||after.forced||!after.binding_id.empty())return {false,"Source USB identity or ownership changed during release"};
                    found=true;
                }
            }
            if(!found)return {false,"Source USB disappeared during binding release; keep it offline"};
        }
        // Released is a protected durable receipt produced only after native
        // destination eject and exact owned GUID removal. Pre-export states have
        // never authorized a binding. No remote/UI boolean can substitute here.
        return {true,{}};
    });
    if(!result.available)throw std::runtime_error(result.reason);
    std::cout<<"{\"restored\":"<<json(utf8(id))<<"}\n";
}
void reconcile_online(){
    const auto pending=pending_recoveries();
    if(pending.empty()){std::cout<<"{\"retired\":0}\n";return;}
    auto runner=native_host_runner();const auto ready=runner->ready();if(!ready.available)throw std::runtime_error(ready.reason);
    unsigned retired=0;
    for(const auto& item:pending){
        require(item.record&&item.error.empty(),"Malformed USB recovery journal retained");
        const auto policy=preexport_retirement_policy(*item.record);if(!policy.available)throw std::runtime_error(policy.reason);
        const auto devices=runner->devices();
        const auto snapshot=inventory([&](const std::wstring& instance)->std::optional<bool>{
            std::optional<bool> owned;for(const auto& device:devices)if(equal_id(device.instance_id,instance)){
                if(owned)return {};owned=device.attached||device.forced||!device.binding_id.empty();
            }return owned;
        });
        require(snapshot.errors.empty(),"Local disk inventory is incomplete; journal retained");
        std::optional<Disk> disk;
        for(const auto& candidate:snapshot.disks)if(has_pending_recovery(candidate,std::span(&item,1))&&candidate.disk_number==item.record->disk_number){
            require(!disk,"Ambiguous local disk identity; journal retained");disk=candidate;
        }
        require(disk.has_value(),"Pre-export source disk is absent; journal retained");
        const auto reserved=runner->reserve(disk->usb_instance_id);if(!reserved.available)throw std::runtime_error(reserved.reason);
        struct Release {HostRunner& runner;~Release(){runner.release();}} release{*runner};
        const auto result=retire_online_preexport(*disk,item,[&](const RecoveryRecord& record)->Availability{
            if(record!=*item.record)return {false,"Journal changed after selection"};
            bool found=false;for(const auto& device:runner->devices())if(equal_id(device.instance_id,disk->usb_instance_id)){
                if(found||device.attached||device.forced||!device.binding_id.empty())return {false,"USB device is bound, attached or ambiguous"};found=true;
            }
            return found?Availability{true,{}}:Availability{false,"USB device absent from fresh backend inventory"};
        });
        if(!result.available)throw std::runtime_error(result.reason);++retired;
    }
    std::cout<<"{\"retired\":"<<retired<<"}\n";
}
void recover_interrupted(const std::wstring& id){
    require(id.size()==64&&std::all_of(id.begin(),id.end(),[](wchar_t c){return(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');}),"Use an exact journal ID from --list");
    std::optional<PendingRecovery> selected;for(const auto& item:pending_recoveries())if(item.journal_id==id){require(!selected,"Duplicate journal identity");selected=item;}
    require(selected&&selected->record&&selected->error.empty(),"Interrupted-return journal is missing or malformed");
    const auto policy=interrupted_return_policy(*selected->record);if(!policy.available)throw std::runtime_error(policy.reason);
    auto runner=native_host_runner();const auto ready=runner->ready();if(!ready.available)throw std::runtime_error(ready.reason);
    const auto devices=runner->devices();
    const auto snapshot=inventory([&](const std::wstring& instance)->std::optional<bool>{
        std::optional<bool> owned;for(const auto& device:devices)if(equal_id(device.instance_id,instance)){
            if(owned)return {};const bool exact=device.bus_id==selected->record->bus_id&&_stricmp(utf8(device.binding_id).c_str(),selected->record->binding_id.c_str())==0;
            owned=device.attached||device.forced||(!device.binding_id.empty()&&!exact);
        }return owned;
    });
    require(snapshot.errors.empty(),"Current storage inventory is incomplete");std::optional<Disk> disk;
    for(const auto& candidate:snapshot.disks)if(matches_recovery(candidate,*selected)){require(!disk,"Ambiguous interrupted source identity");disk=candidate;}
    require(disk&&extrapane::usb::eligible(disk->facts).available,"Original source identity changed, is protected, or is not exclusively available");
    const auto reserved=runner->reserve(disk->usb_instance_id);if(!reserved.available)throw std::runtime_error(reserved.reason);
    struct Release {HostRunner& runner;~Release(){runner.release();}} release{*runner};
    const auto result=recover_interrupted_disk(*disk,*selected,[&](const RecoveryRecord& record)->Availability{
        if(record!=*selected->record)return {false,"Interrupted journal changed after selection"};
        std::optional<HostDevice> device;for(const auto& current:runner->devices())if(equal_id(current.instance_id,disk->usb_instance_id)){
            if(device)return {false,"Ambiguous interrupted source USB instance"};device=current;
        }
        if(!device||device->attached||device->forced)return {false,"Interrupted source is absent or still attached"};
        if(!device->binding_id.empty()){
            const auto adopted=runner->adopt_interrupted_binding(*disk,*selected);if(!adopted.available)return adopted;
            const auto unbound=runner->unbind(device->binding_id);if(!unbound.available)return unbound;
        }
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        do{
            const auto idle=runner->verify_idle_source(*disk);
            if(idle.available&&matches_recovery(*disk,*selected))return {true,{}};
            if(std::chrono::steady_clock::now()>=deadline)return {false,"USB source has not returned with its exact medium identity and no active exporter/binding"};
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }while(true);
    });
    if(!result.available)throw std::runtime_error(result.reason);
    std::cout<<"{\"interruptedReturn\":"<<json(utf8(id))<<",\"cleanEjectClaimed\":false,\"auditRetained\":true}\n";
}
}
int wmain(int argc,wchar_t** argv){
    try{
        // Keep the apartment alive across repeated backend state reads. The
        // backend's WinRT JSON activation factories may be cached; tearing COM
        // down between inventory and the final fresh ownership check is unsafe.
        const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        require(SUCCEEDED(initialized),"Cannot initialize the USB recovery Windows runtime");
        struct Apartment {~Apartment(){CoUninitialize();}} apartment;
        if(argc==2&&std::wstring_view(argv[1])==L"--list"){list();return 0;}
        if(argc==3&&std::wstring_view(argv[1])==L"--inspect"){inspect(argv[2]);return 0;}
        if(argc==3&&std::wstring_view(argv[1])==L"--recover"){recover(argv[2]);return 0;}
        if(argc==2&&std::wstring_view(argv[1])==L"--reconcile-online"){reconcile_online();return 0;}
        if(argc==4&&std::wstring_view(argv[1])==L"--recover-interrupted"&&std::wstring_view(argv[3])==L"--acknowledge-unflushed-receiver"){recover_interrupted(argv[2]);return 0;}
        if(argc==2&&std::wstring_view(argv[1])==L"--help"){
            std::cout<<"ExtraPane.UsbRecovery --list\nExtraPane.UsbRecovery --inspect <journal-id> (read-only)\nExtraPane.UsbRecovery --recover <journal-id>\nExtraPane.UsbRecovery --reconcile-online (retire never-exported already-online journals only)\n"
                <<"ExtraPane.UsbRecovery --recover-interrupted <journal-id> --acknowledge-unflushed-receiver\n"
                <<"Ordinary recovery requires administrator rights, a verified local disk and durable eject/release evidence.\n"
                <<"Interrupted return is an explicit operator-only action for a v2 bound journal after both endpoints stop. It cannot prove receiver writes were flushed and retains a protected acknowledgment audit; it never claims a clean eject.\n";return 0;
        }
        throw std::runtime_error("Use --list, --inspect <journal-id>, --recover <journal-id>, or --help. No --force mode exists.");
    }catch(const std::exception& error){std::cout<<"{\"error\":"<<json(error.what())<<"}\n";return 1;}
}
