#include <dm/usb_session.hpp>
#include <dm/usb_host_backend.hpp>
#include <dm/usb_device.hpp>
#include <dm/usb_descriptors.hpp>
#include <extrapane/usb_offer.hpp>
#include <dm/usb_tunnel.hpp>
#include <dm/tls.hpp>
#include <windows.h>
#include <shlobj.h>
#include <cwctype>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <bcrypt.h>
#include <winioctl.h>
#include <usbioctl.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <exception>
#include <mutex>
#include <thread>
#include <sstream>
#include <iomanip>
namespace dm::usb {
namespace {
using Clock=std::chrono::steady_clock;
using TunnelControl=extrapane::usb::tunnel::Control;
struct EjectFailure:std::runtime_error{TunnelControl reason;EjectFailure(TunnelControl code,const char* message):std::runtime_error(message),reason(code){}};
void eject_check(bool value,TunnelControl code,const char* message){if(!value)throw EjectFailure(code,message);}
const char* return_reason(TunnelControl code){switch(code){
case TunnelControl::return_not_ready:return "The receiver has no ready USB device for this session. USB has not been returned.";
case TunnelControl::return_identity:return "The receiver could not verify this session's imported device identity. USB has not been returned.";
case TunnelControl::return_flush:return "The receiver could not flush the imported storage volumes. USB has not been returned.";
default:return "The receiver could not complete safe USB return. Check its USB diagnostics; ownership is retained.";
}}

void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
struct Cancellation {std::jthread thread;Cancellation(security::TlsStream& stream,std::atomic_bool& stop):thread([&stream,&stop](std::stop_token token){while(!token.stop_requested()&&!stop.load())std::this_thread::sleep_for(std::chrono::milliseconds(25));if(stop.load())stream.cancel();}){}};
struct Com{HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Com(){check(SUCCEEDED(result),"USB session COM initialization failed");}~Com(){CoUninitialize();}};
struct Handle{HANDLE value=INVALID_HANDLE_VALUE;explicit Handle(HANDLE h):value(h){}~Handle(){if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);}Handle(const Handle&)=delete;};
bool same(const std::wstring& a,const std::wstring& b){return CompareStringOrdinal(a.c_str(),static_cast<int>(a.size()),b.c_str(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;}
std::string hex(std::span<const std::uint8_t> bytes){std::string out;for(auto b:bytes){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;}
std::string virtual_serial(){std::string value;while(value.size()<15){std::array<std::uint8_t,32> bytes{};check(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Cannot create imported device identity");for(auto byte:bytes)if(byte<252&&value.size()<15)value+="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[byte%36];}return value;}
std::string clean(std::string text){for(auto& c:text)if(static_cast<unsigned char>(c)<32)c=' ';return text.substr(0,1024);}
Inventory current_inventory(){auto runner=native_host_runner();const auto devices=runner->devices();return inventory([&](const std::wstring& id)->std::optional<bool>{std::optional<bool> result;for(const auto& d:devices)if(same(id,d.instance_id)){if(result)return {};result=d.forced||d.attached||!d.binding_id.empty();}return result;});}
// Already-online records from a failed preparation may be retired without
// claiming that the card is unchanged or taking a disk online. Exported/bound
// histories stay blocked until their own durable recovery proof is satisfied.
extrapane::Availability reconcile_preexport(const Disk& disk,std::span<const PendingRecovery> pending,bool mutate){
    auto runner=native_host_runner();bool reserved=false;
    struct Release{HostRunner& runner;bool& reserved;~Release(){if(reserved)runner.release();}}release{*runner,reserved};
    for(const auto& item:pending){
        if(!has_pending_recovery(disk,std::span(&item,1)))continue;
        if(mutate&&!reserved){const auto claim=runner->reserve(disk.usb_instance_id);if(!claim.available)return claim;reserved=true;}
        auto absent=[&](const RecoveryRecord& record)->extrapane::Availability{
            if(!item.record||record!=*item.record)return {false,"USB recovery record changed"};
            bool found=false;for(const auto& device:runner->devices())if(same(device.instance_id,disk.usb_instance_id)){
                if(found||device.attached||device.forced||!device.binding_id.empty())return {false,"USB device is bound, attached or ambiguous"};found=true;
            }
            return found?extrapane::Availability{true,{}}:extrapane::Availability{false,"USB device absent from fresh backend inventory"};
        };
        const auto result=mutate?retire_online_preexport(disk,item,absent):assess_online_preexport(disk,item,absent);
        if(!result.available)return result;
    }
    return {true,{}};
}
std::uint16_t endpoint_port(const std::string& endpoint){const auto at=endpoint.rfind(':');check(at!=std::string::npos,"Invalid local USB listener");unsigned value=0;const auto text=endpoint.substr(at+1);const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);check(error==std::errc{}&&end==text.data()+text.size()&&value&&value<=65535,"Invalid local USB port");return static_cast<std::uint16_t>(value);}
std::uint16_t reserve_port(){net::Options options{"127.0.0.1","",0,std::chrono::milliseconds(250)};net::TcpListener listener(options);return endpoint_port(listener.endpoints());}
using extrapane::usb::wire::control;
using extrapane::usb::wire::DeviceKind;
using extrapane::usb::wire::Offer;
using extrapane::usb::wire::encode;
using extrapane::usb::wire::decode;
using extrapane::usb::wire::receipt;
using extrapane::usb::wire::greeting;
using extrapane::usb::wire::decode_greeting;
void expect(security::TlsStream& stream,unsigned kind,std::chrono::seconds timeout=std::chrono::seconds(15)){std::array<std::uint8_t,8> packet{};stream.read(packet,Clock::now()+timeout);check(packet==control(kind),"USB resource negotiation failed");}
unsigned id_word(const std::wstring& id,const wchar_t* name){const auto value=usb_id_word(id,name);check(value.has_value(),"Physical USB VID/PID unavailable or invalid");return *value;}
class ProvenHandles final:public StorageHandles{
public:std::unique_ptr<StorageHandles> base=native_storage_handles();bool locked=false,dismounted=false;
    extrapane::Availability open(const Disk& d)override{return base->open(d);}extrapane::Availability validate(const Disk& d)override{return base->validate(d);}
    extrapane::Availability verify_locked(const Disk& d)override{return base->verify_locked(d);}
    extrapane::Availability handoff_quarantined(const Disk& d)override{auto result=base->handoff_quarantined(d);if(result.available){locked=false;dismounted=false;}return result;}
    extrapane::Availability lock_all()override{auto result=base->lock_all();locked=result.available;return result;}
    extrapane::Availability dismount_all()override{auto result=base->dismount_all();dismounted=result.available;return result;}
    bool invalidated()const noexcept override{return base->invalidated();}void release(bool allowed)noexcept override{base->release(allowed);if(allowed){locked=false;dismounted=false;}}
};
std::wstring node_id(DEVINST node){wchar_t value[MAX_DEVICE_ID_LEN]{};check(CM_Get_Device_IDW(node,value,MAX_DEVICE_ID_LEN,0)==CR_SUCCESS,"USB device identity unavailable");return value;}
std::vector<wchar_t> property(DEVINST node,ULONG key){ULONG type=0,size=0;auto result=CM_Get_DevNode_Registry_PropertyW(node,key,&type,nullptr,&size,0);check(result==CR_BUFFER_SMALL&&size>0&&size<=65536,"USB ancestor property unavailable");std::vector<wchar_t> bytes(size/2+2,0);check(CM_Get_DevNode_Registry_PropertyW(node,key,&type,bytes.data(),&size,0)==CR_SUCCESS&&(type==REG_SZ||type==REG_MULTI_SZ),"USB ancestor property invalid");return bytes;}
bool ude_ancestor(DEVINST node){for(unsigned depth=0;depth<32;++depth){DEVINST parent=0;if(CM_Get_Parent(&parent,node,0)!=CR_SUCCESS)return false;node=parent;try{auto ids=property(node,CM_DRP_HARDWAREID);bool matched=false;for(std::size_t at=0;at<ids.size()&&ids[at];){std::wstring id(ids.data()+at);if(same(id,L"ROOT\\USBIP_WIN2\\UDE"))matched=true;at+=id.size()+1;}if(matched){auto service=property(node,CM_DRP_SERVICE);return same(service.data(),L"usbip2_ude");}}catch(...){}}return false;}
void verify_volume_disk(HANDLE handle,DWORD disk_number){
    VOLUME_DISK_EXTENTS extents{};DWORD bytes=0;check(DeviceIoControl(handle,IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,nullptr,0,&extents,sizeof(extents),&bytes,nullptr)&&bytes>=sizeof(extents)&&extents.NumberOfDiskExtents==1&&extents.Extents[0].DiskNumber==disk_number&&extents.Extents[0].StartingOffset.QuadPart>=0&&extents.Extents[0].ExtentLength.QuadPart>0,"Destination volume no longer belongs exclusively to the owned disk");
}
DEVINST own_node(const Offer& offer,const std::string& serial){
    const auto set=SetupDiGetClassDevsW(nullptr,L"USB",nullptr,DIGCF_ALLCLASSES|DIGCF_PRESENT);check(set!=INVALID_HANDLE_VALUE,"Cannot enumerate imported USB node");struct Set{HDEVINFO value;~Set(){SetupDiDestroyDeviceInfoList(value);}}cleanup{set};
    std::wostringstream prefix;prefix<<L"USB\\VID_"<<std::uppercase<<std::hex<<std::setfill(L'0')<<std::setw(4)<<offer.vendor<<L"&PID_"<<std::setw(4)<<offer.product;const std::wstring suffix(serial.begin(),serial.end());DEVINST found=0;
    for(DWORD i=0;i<4096;++i){SP_DEVINFO_DATA device{sizeof(device)};if(!SetupDiEnumDeviceInfo(set,i,&device)){check(GetLastError()==ERROR_NO_MORE_ITEMS,"USB node enumeration failed");break;}const auto id=node_id(device.DevInst);const auto slash=id.rfind(L'\\');if(slash==id.npos||!same(id.substr(slash+1),suffix)||id.size()<prefix.str().size()||!same(id.substr(0,prefix.str().size()),prefix.str()))continue;if(!ude_ancestor(device.DevInst))continue;check(!found,"Imported USB identity is ambiguous");found=device.DevInst;}
    check(found!=0,"Owned imported USB device has not appeared; retry eject after enumeration");return found;
}
std::wstring unique_ude_controller(){
    const auto devices=SetupDiGetClassDevsW(nullptr,L"ROOT",nullptr,DIGCF_PRESENT|DIGCF_ALLCLASSES);
    check(devices!=INVALID_HANDLE_VALUE,"Cannot inspect USB controller identity");
    struct Set{HDEVINFO value;~Set(){SetupDiDestroyDeviceInfoList(value);}}cleanup{devices};
    std::wstring result;bool complete=false;
    for(DWORD index=0;index<4096;++index){
        SP_DEVINFO_DATA device{sizeof(device)};
        if(!SetupDiEnumDeviceInfo(devices,index,&device)){check(GetLastError()==ERROR_NO_MORE_ITEMS,"Cannot enumerate USB controller identity");complete=true;break;}
        ULONG type=0,size=0;const auto queried=CM_Get_DevNode_Registry_PropertyW(device.DevInst,CM_DRP_HARDWAREID,&type,nullptr,&size,0);
        if(queried==CR_NO_SUCH_VALUE)continue;check(queried==CR_BUFFER_SMALL,"Cannot inspect USB controller hardware IDs");
        const auto ids=property(device.DevInst,CM_DRP_HARDWAREID);bool match=false;
        for(std::size_t at=0;at<ids.size()&&ids[at];){const std::wstring id(ids.data()+at);match|=same(id,L"ROOT\\USBIP_WIN2\\UDE");at+=id.size()+1;}
        if(!match)continue;const auto id=node_id(device.DevInst);
        const auto service=property(device.DevInst,CM_DRP_SERVICE);check(same(service.data(),L"usbip2_ude"),"Unexpected imported USB controller service");
        check(result.empty(),"Multiple USB/IP controllers are present; cannot prove import ownership");result=id;
    }
    check(complete&&!result.empty(),"Cannot prove a unique USB/IP controller");return result;
}
bool below_controller(DEVINST node,const std::wstring& controller){
    for(unsigned depth=0;depth<32;++depth){DEVINST parent=0;const auto result=CM_Get_Parent(&parent,node,0);if(result!=CR_SUCCESS){check(result==CR_NO_SUCH_DEVNODE,"Cannot inspect USB controller ancestry");return false;}node=parent;if(same(node_id(node),controller))return true;}throw std::runtime_error("USB controller ancestry exceeds limit");
}
DEVINST generic_node(unsigned port,const Offer& offer){
    // Pinned USBip assigns its globally unique port directly to UDECX's USB2
    // or USB3 root-hub connection index. Query that hub's driver key rather
    // than relying on the pinned driver's broken serial-override handling.
    const auto controller=unique_ude_controller();
    static constexpr GUID hub_guid{0xf18a0e88,0xc30c,0x11d0,{0x88,0x15,0x00,0xa0,0xc9,0x06,0xbe,0xd8}};
    const auto hubs=SetupDiGetClassDevsW(&hub_guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    check(hubs!=INVALID_HANDLE_VALUE,"Cannot enumerate USB root hubs");
    struct Set{HDEVINFO value;~Set(){SetupDiDestroyDeviceInfoList(value);}}cleanup{hubs};
    DEVINST found=0;bool complete=false;
    for(DWORD index=0;index<256;++index){
        SP_DEVICE_INTERFACE_DATA iface{sizeof(iface)};
        if(!SetupDiEnumDeviceInterfaces(hubs,nullptr,&hub_guid,index,&iface)){check(GetLastError()==ERROR_NO_MORE_ITEMS,"Cannot enumerate USB hub interfaces");complete=true;break;}
        DWORD required=0;SetupDiGetDeviceInterfaceDetailW(hubs,&iface,nullptr,0,&required,nullptr);
        check(required>=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)&&required<=65536,"Invalid USB hub path size");
        std::vector<std::uint8_t> detail_bytes(required);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detail_bytes.data());detail->cbSize=sizeof(*detail);
        SP_DEVINFO_DATA hub{sizeof(hub)};check(SetupDiGetDeviceInterfaceDetailW(hubs,&iface,detail,required,nullptr,&hub),"Cannot read USB hub path");
        if(!below_controller(hub.DevInst,controller))continue;
        Handle handle(CreateFileW(detail->DevicePath,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
        check(handle.value!=INVALID_HANDLE_VALUE,"Cannot inspect imported USB root hub");
        alignas(USB_NODE_CONNECTION_INFORMATION_EX) std::array<std::uint8_t,sizeof(USB_NODE_CONNECTION_INFORMATION_EX)+32*sizeof(USB_PIPE_INFO)> info_bytes{};
        auto& info=*reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(info_bytes.data());info.ConnectionIndex=port;DWORD bytes=0;
        if(!DeviceIoControl(handle.value,IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,&info,static_cast<DWORD>(info_bytes.size()),&info,static_cast<DWORD>(info_bytes.size()),&bytes,nullptr))continue;
        check(bytes>=offsetof(USB_NODE_CONNECTION_INFORMATION_EX,PipeList),"Truncated imported USB connection information");
        if(info.ConnectionStatus!=DeviceConnected)continue;
        if(info.DeviceDescriptor.idVendor!=offer.vendor||info.DeviceDescriptor.idProduct!=offer.product)continue;
        USB_NODE_CONNECTION_DRIVERKEY_NAME initial{};initial.ConnectionIndex=port;
        check(DeviceIoControl(handle.value,IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,&initial,sizeof(initial),&initial,sizeof(initial),&bytes,nullptr),"Cannot query imported USB driver key");
        check(initial.ActualLength>sizeof(initial)&&initial.ActualLength<=65536,"Invalid imported USB driver key length");
        std::vector<std::uint8_t> key_bytes(initial.ActualLength+sizeof(wchar_t),0);auto* key=reinterpret_cast<USB_NODE_CONNECTION_DRIVERKEY_NAME*>(key_bytes.data());key->ConnectionIndex=port;
        check(DeviceIoControl(handle.value,IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME,key,static_cast<DWORD>(key_bytes.size()),key,static_cast<DWORD>(key_bytes.size()),&bytes,nullptr),"Cannot read imported USB driver key");
        check(bytes>=offsetof(USB_NODE_CONNECTION_DRIVERKEY_NAME,DriverKeyName)+sizeof(wchar_t)&&key->ActualLength>=offsetof(USB_NODE_CONNECTION_DRIVERKEY_NAME,DriverKeyName)+sizeof(wchar_t)&&key->ActualLength<=bytes&&key->ActualLength<=key_bytes.size()-sizeof(wchar_t)&&(key->ActualLength-offsetof(USB_NODE_CONNECTION_DRIVERKEY_NAME,DriverKeyName))%sizeof(wchar_t)==0,"Imported USB driver key changed");
        const auto key_chars=(key->ActualLength-offsetof(USB_NODE_CONNECTION_DRIVERKEY_NAME,DriverKeyName))/sizeof(wchar_t);
        check(key->DriverKeyName[0]&&std::find(key->DriverKeyName,key->DriverKeyName+key_chars,L'\0')!=key->DriverKeyName+key_chars,"Unterminated imported USB driver key");
        DEVINST child=0;const auto child_status=CM_Get_Child(&child,hub.DevInst,0);if(child_status!=CR_SUCCESS){check(child_status==CR_NO_SUCH_DEVNODE,"Cannot enumerate imported hub children");continue;}
        unsigned children=0;
        do{
            check(++children<=256,"USB root hub child limit exceeded");
            const auto driver=property(child,CM_DRP_DRIVER);
            if(!driver.empty()&&same(driver.data(),key->DriverKeyName)){check(!found,"Imported USB port maps to multiple devices");found=child;}
            DEVINST next=0;const auto sibling_status=CM_Get_Sibling(&next,child,0);if(sibling_status!=CR_SUCCESS){check(sibling_status==CR_NO_SUCH_DEVNODE,"Cannot enumerate imported USB siblings");break;}child=next;
        }while(true);
    }
    check(same(unique_ude_controller(),controller),"USB controller changed during ownership validation");check(complete,"USB root hub enumeration exceeded limit");check(found,"Owned imported USB port has no unique PnP device yet");return found;
}
bool verified_import(ImportRunner& runner,unsigned port,std::uint16_t local,const Offer& offer,const std::string& serial){return offer.kind==DeviceKind::storage?runner.verify_owned(port,local,offer.bus,serial):runner.verify_owned_generic(port,local,offer.bus);}
DEVINST imported_node(unsigned port,const Offer& offer,const std::string& serial){return offer.kind==DeviceKind::storage?own_node(offer,serial):generic_node(port,offer);}
Disk owned_disk(const Offer& offer,const std::string& serial){
    DEVINST node=0;try{node=own_node(offer,serial);}catch(const std::exception& error){throw EjectFailure(TunnelControl::return_not_ready,error.what());}
    const auto id=node_id(node);const auto disks=inventory([](const std::wstring&)->std::optional<bool>{return false;});
    eject_check(disks.errors.empty(),TunnelControl::return_failed,"Cannot prove destination volume inventory");
    std::optional<Disk> found;
    for(const auto& disk:disks.disks)if(same(disk.usb_instance_id,id)){
        eject_check(!found,TunnelControl::return_identity,"Multiple destination disks match the imported device");
        const auto eligibility=extrapane::usb::eligible(disk.facts);
        eject_check(eligibility.available,TunnelControl::return_failed,eligibility.reason.c_str());found=disk;
    }
    eject_check(found.has_value(),TunnelControl::return_not_ready,"Owned USB port exists, but its storage disk has not enumerated");return *found;
}
void verify_generic_node(DEVINST node,unsigned port){
    (void)read_device_descriptors(node_id(node),port); // Every configuration and alternate interface must be HID/audio.
    std::vector<DEVINST> pending{node};unsigned visited=0;
    while(!pending.empty()){
        const auto current=pending.back();pending.pop_back();
        eject_check(++visited<=256,TunnelControl::return_identity,"Imported USB tree exceeds validation limit");
        ULONG status=0,problem=0;
        eject_check(CM_Get_DevNode_Status(&status,&problem,current,0)==CR_SUCCESS&&problem==0&&(status&DN_STARTED),TunnelControl::return_not_ready,"Imported USB device is not ready or requires a driver");
        ULONG type=0;wchar_t service[256]{};ULONG size=sizeof(service);
        const auto result=CM_Get_DevNode_Registry_PropertyW(current,CM_DRP_SERVICE,&type,service,&size,0);
        if(result==CR_SUCCESS)eject_check(!same(service,L"USBSTOR")&&!same(service,L"UASPStor")&&!same(service,L"disk"),TunnelControl::return_identity,"Storage USB devices require the storage protocol");
        else eject_check(result==CR_NO_SUCH_VALUE,TunnelControl::return_identity,"Cannot verify imported USB device class");
        DEVINST child=0;const auto child_status=CM_Get_Child(&child,current,0);check(child_status==CR_SUCCESS||child_status==CR_NO_SUCH_DEVNODE,"Cannot enumerate imported USB children");if(child_status==CR_SUCCESS){
            do{pending.push_back(child);DEVINST next=0;const auto sibling_status=CM_Get_Sibling(&next,child,0);if(sibling_status!=CR_SUCCESS){check(sibling_status==CR_NO_SUCH_DEVNODE,"Cannot enumerate imported USB siblings");break;}child=next;}while(pending.size()<=256);
            eject_check(pending.size()<=256,TunnelControl::return_identity,"Imported USB tree exceeds validation limit");
        }
    }
}
void eject_owned(ImportRunner& runner,unsigned port,std::uint16_t local,const Offer& offer,const std::string& serial){
    eject_check(verified_import(runner,port,local,offer,serial),TunnelControl::return_identity,"Imported port identity changed; refusing eject");
    const auto node=imported_node(port,offer,serial);const auto id=node_id(node);
    if(offer.kind==DeviceKind::storage){const auto disk=owned_disk(offer,serial);
    for(auto volume:disk.volumes){
        if(volume.ends_with(L"\\"))volume.pop_back();Handle handle(CreateFileW(volume.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
        eject_check(handle.value!=INVALID_HANDLE_VALUE,TunnelControl::return_flush,"Cannot open destination volume for flush");
        try{verify_volume_disk(handle.value,disk.disk_number);}catch(const std::exception& error){throw EjectFailure(TunnelControl::return_identity,error.what());}
        eject_check(FlushFileBuffers(handle.value),TunnelControl::return_flush,"Destination volume cannot be flushed");
    }}else verify_generic_node(node,port);
    eject_check(verified_import(runner,port,local,offer,serial)&&imported_node(port,offer,serial)==node&&same(node_id(node),id),TunnelControl::return_identity,"USB destination changed before eject");
    PNP_VETO_TYPE veto=PNP_VetoTypeUnknown;wchar_t name[512]{};const auto result=CM_Request_Device_EjectW(node,&veto,name,512,0);
    if(result!=CR_SUCCESS){const auto detail="Windows refused device eject: CONFIGRET="+std::to_string(result)+", veto="+std::to_string(static_cast<unsigned>(veto));throw EjectFailure(result==CR_REMOVE_VETOED?TunnelControl::return_busy:TunnelControl::return_failed,detail.c_str());}
}
}
void list_storage(const Report& report){
    Com com;
    auto row=[&](const std::string& id,const std::string& label,bool available,const std::string& reason){
        check(!id.empty()&&id.size()<=4096,"USB inventory identifier exceeds limit");
        report("USB_DEVICE_V1\t"+hex(std::span(reinterpret_cast<const std::uint8_t*>(id.data()),id.size()))+"\t"+clean(label)+"\t"+(available?"available":clean(reason)));
    };
    // Failed disk ownership inventory must never turn a generic device into a
    // storage fallback, or hide otherwise inspectable generic device entries.
    std::optional<Inventory> storage;std::string storage_error;
    try{storage=current_inventory();}catch(const std::exception& error){storage_error=error.what();}
    try{
        auto runner=native_host_runner();std::vector<GenericDevice> shown;
        for(auto& device:generic_inventory(*runner)){if(storage&&std::any_of(storage->disks.begin(),storage->disks.end(),[&](const auto& disk){return same(disk.usb_instance_id,device.instance_id);}))continue;shown.push_back(std::move(device));}
        std::vector<std::string> storage_labels;if(storage)for(const auto& disk:storage->disks)storage_labels.push_back(disk.label);
        disambiguate_labels(shown,storage_labels);
        for(const auto& device:shown)row(device.id,device.label,device.available,device.reason);
    }
    catch(const std::exception& error){row("usb-generic-inventory-error","USB devices",false,error.what());}
    try{
        check(storage.has_value(),storage_error.c_str());const auto& all=*storage;check(all.errors.empty()&&all.disks.size()<=128,"USB storage ownership inventory is incomplete or exceeds the device limit");
        const auto pending=pending_recoveries();
        for(const auto& disk:all.disks){auto eligibility=extrapane::usb::eligible(disk.facts);if(eligibility.available&&has_pending_recovery(disk,pending))eligibility=reconcile_preexport(disk,pending,false);row(disk.facts.id,disk.label,eligibility.available,eligibility.reason);}
    }catch(const std::exception& error){row("usb-storage-inventory-error","USB storage",false,error.what());}
    report("USB_LIST_DONE");
}

void send_generic(const net::Options& options,const std::string& id,std::atomic_bool& stop,std::atomic_bool& return_requested,const Report& report){
    Com com;check(IsUserAnAdmin(),"USB source requires explicit elevation");
    {auto cleanup=native_host_runner();const auto restored=cleanup->cleanup_generic();check(restored.available,restored.reason.c_str());}
    const auto inspect=[]{auto runner=native_host_runner();return generic_inventory(*runner);};
    const auto devices=inspect();const auto found=std::find_if(devices.begin(),devices.end(),[&](const auto& d){return d.id==id;});
    check(found!=devices.end(),"Select a current local USB device");check(found->available,found->reason.c_str());
    const auto selected=*found;
    security::TlsStream remote(net::connect_stream(options),false,"");Cancellation cancellation(remote,stop);
    remote.handshake(Clock::now()+std::chrono::seconds(15));remote.write(greeting(0,DeviceKind::generic),Clock::now()+std::chrono::seconds(5));{std::array<std::uint8_t,8> reply{};remote.read(reply,Clock::now()+std::chrono::seconds(15));check(reply==greeting(2,DeviceKind::generic),"Generic USB receiver negotiation failed before export");}
    Offer offer;offer.kind=DeviceKind::generic;offer.vendor=selected.vendor;offer.product=selected.product;
    check(BCryptGenRandom(nullptr,offer.nonce.data(),16,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Cannot create USB session nonce");
    GenericExport transfer(native_host_runner(),inspect);const auto port=reserve_port();
    if(stop.load()||return_requested.load()){report("USB_RETURNED USB was canceled before export.");return;}
    const auto started=transfer.start(selected,port);check(started.available,started.reason.c_str());
    bool clean_eject=false;
    try{
        auto local=net::connect_stream({"127.0.0.1","127.0.0.1",port,std::chrono::seconds(5)});offer.bus=transfer.bus();
        remote.write(encode(offer),Clock::now()+std::chrono::seconds(5));expect(remote,3);
        report("USB_WAITING Waiting for the receiver to enumerate the USB device.");
        pump_tunnel(remote,local,false,offer.bus,stop,true,&return_requested,nullptr,[&](TunnelControl event){
            if(event==TunnelControl::storage_ready)report("USB_STARTED sending");
            else if(event==TunnelControl::storage_not_ready)report("USB_NOT_READY The imported USB device requires attention on the receiver.");
            else{report("USB_REVOKING "+std::string(return_reason(event)));stop=true;}
        });
        if(!stop.load()){std::array<std::uint8_t,32> proof{};remote.read(proof,Clock::now()+std::chrono::seconds(10));clean_eject=proof==receipt(offer);}
    }catch(const std::exception& error){report("USB_REVOKING "+clean(error.what()));}
    remote.cancel();const auto returned=transfer.stop();
    if(returned.available)report(clean_eject?"USB_RETURNED USB forwarding stopped; the local driver is reconnecting.":"USB_RETURNED_INTERRUPTED USB forwarding stopped; the local driver is reconnecting.");
    else{report("USB_RECOVERY_REQUIRED "+returned.reason);throw std::runtime_error(returned.reason);}
}
void send_storage(const net::Options& options,const std::string& id,std::atomic_bool& stop,std::atomic_bool& return_requested,const Report& report){
    if(id.starts_with("usb-generic-")){send_generic(options,id,stop,return_requested,report);return;}
    Com com;check(IsUserAnAdmin(),"USB source requires explicit elevation");const auto all=current_inventory();check(all.errors.empty(),"USB ownership inventory incomplete");const auto found=std::find_if(all.disks.begin(),all.disks.end(),[&](const Disk& d){return d.facts.id==id;});check(found!=all.disks.end()&&extrapane::usb::eligible(found->facts).available,"Select an eligible current local USB storage device");const Disk selected=*found;
    const auto reconciled=reconcile_preexport(selected,pending_recoveries(),true);check(reconciled.available,reconciled.reason.c_str());
    security::TlsStream remote(net::connect_stream(options),false,"");Cancellation cancellation(remote,stop);remote.handshake(Clock::now()+std::chrono::seconds(15));remote.write(greeting(0,DeviceKind::storage),Clock::now()+std::chrono::seconds(5));{std::array<std::uint8_t,8> reply{};remote.read(reply,Clock::now()+std::chrono::seconds(15));check(reply==greeting(2,DeviceKind::storage),"Storage USB receiver negotiation failed before export");}
    Offer offer;check(BCryptGenRandom(nullptr,offer.nonce.data(),16,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Cannot create USB session nonce");offer.vendor=static_cast<std::uint16_t>(id_word(selected.usb_instance_id,L"VID_"));offer.product=static_cast<std::uint16_t>(id_word(selected.usb_instance_id,L"PID_"));
    auto handles=std::make_unique<ProvenHandles>();auto* proof=handles.get();auto recovery=native_recovery_guard([proof](const Disk& disk){if(!proof->locked||!proof->dismounted)return extrapane::Availability{false,"Source volume locks are not proven"};return proof->verify_locked(disk);});std::unique_ptr<net::TcpStream> local;bool clean_eject=false;extrapane::Availability source_restored{false,"Source backend has not confirmed automatic return"};
    HostTransport transport;
    transport.source_restored=[&](extrapane::Availability result){source_restored=std::move(result);};
    transport.start=[&](const std::string& bus,std::uint16_t port){offer.bus=bus;local=std::make_unique<net::TcpStream>(net::connect_stream({"127.0.0.1","127.0.0.1",port,std::chrono::seconds(5)}));return extrapane::Availability{true,{}};};
    transport.handoff_quarantined=[proof](const Disk& disk){return proof->handoff_quarantined(disk);};
    transport.close=[&]{if(local)local->cancel();};transport.flush_and_eject=[&]{return extrapane::Availability{clean_eject,clean_eject?"":"Destination has not provided a verified session eject receipt"};};
    StorageOperations operations(selected,current_inventory,usb_host_backend(native_host_runner(),std::move(recovery),transport,reserve_port()),std::move(handles));extrapane::usb::Transfer transfer(operations);
    check(!stop.load(),"USB source canceled before preparation");
    if(return_requested.load()){report("USB_RETURNED USB was canceled before export; the source disk was not changed.");return;}
    const auto result=transfer.start(selected.facts.id,selected.facts.generation,true);if(!result.available){report(source_restored.available?"USB_RETURNED USB source returned after setup failed.":"USB_RECOVERY_REQUIRED "+result.reason);throw std::runtime_error(result.reason);}
    try{
        remote.write(encode(offer),Clock::now()+std::chrono::seconds(5));expect(remote,3);report("USB_WAITING Waiting for the receiver to enumerate the storage disk.");pump_tunnel(remote,*local,false,offer.bus,stop,true,&return_requested,nullptr,[&](TunnelControl event){
            if(event==TunnelControl::storage_ready)report("USB_STARTED sending");
            else if(event==TunnelControl::storage_not_ready)report("USB_NOT_READY The receiver has an imported USB port, but its storage disk has not become available. Check receiver diagnostics.");
            else {report("USB_REVOKING "+std::string(return_reason(event))+" Revoking export before returning the source; pending writes may be interrupted.");stop=true;}
        });
        if(!stop.load()){std::array<std::uint8_t,32> proof_receipt{};remote.read(proof_receipt,Clock::now()+std::chrono::seconds(10));clean_eject=proof_receipt==receipt(offer);}
        if(clean_eject){const auto ejected=transfer.eject();check(ejected.available,ejected.reason.c_str());check(source_restored.available,source_restored.reason.c_str());report("USB_RETURNED USB safely returned to this computer.");}
        else {remote.cancel();transfer.transport_lost();}
    }catch(const std::exception& error){
        report("USB_REVOKING "+clean(error.what())+" Closing export and returning the source.");
        remote.cancel();transfer.transport_lost();
        if(source_restored.available){report("USB_RETURNED_INTERRUPTED "+(source_restored.reason.empty()?std::string("USB returned to this computer. The transfer was interrupted; pending writes may not have been flushed."):source_restored.reason));return;}
        report("USB_RECOVERY_REQUIRED "+source_restored.reason);throw;
    }
    if(!clean_eject){
        if(source_restored.available)report("USB_RETURNED_INTERRUPTED "+(source_restored.reason.empty()?std::string("USB returned to this computer. The transfer was interrupted; pending writes may not have been flushed."):source_restored.reason));
        else report("USB_RECOVERY_REQUIRED "+source_restored.reason);
    }
}
void receive_storage_admitted(security::TlsStream& remote,std::atomic_bool& stop,std::atomic_bool& eject,const Report& report){
    Com com;check(IsUserAnAdmin(),"USB receiver requires explicit elevation");auto runner=native_import_runner();auto readiness=runner->ready();check(readiness.available,readiness.reason.c_str());
    std::atomic_bool session_stop=false;
    std::jthread parent_cancel([&](std::stop_token token){while(!token.stop_requested()&&!session_stop.load()&&!stop.load())std::this_thread::sleep_for(std::chrono::milliseconds(25));if(stop.load())session_stop=true;});
Cancellation cancellation(remote,session_stop);std::array<std::uint8_t,8> hello{};remote.read(hello,Clock::now()+std::chrono::seconds(15));
    const auto negotiated=decode_greeting(hello,0);if(negotiated==DeviceKind::generic)(void)unique_ude_controller();
    remote.write(greeting(2,negotiated),Clock::now()+std::chrono::seconds(5));std::array<std::uint8_t,64> packet{};remote.read(packet,Clock::now()+std::chrono::seconds(30));const auto offer=decode(packet);check(offer.kind==negotiated,"USB device kind changed after negotiation");
    net::Options loopback{"127.0.0.1","",0,std::chrono::milliseconds(250)};net::TcpListener local_listener(loopback);const auto local_port=endpoint_port(local_listener.endpoints());const auto serial=virtual_serial();remote.write(control(3),Clock::now()+std::chrono::seconds(5));
    std::atomic_bool clean_eject=false,in_eject=false;std::atomic<TunnelControl> notice=TunnelControl::none;std::atomic_uint owned_port=0;
    // Construct before the worker so its destructor joins before port teardown.
    struct PortCleanup { std::function<void()> run; ~PortCleanup(){try{run();}catch(...){}} } port_cleanup{[&]{
        remote.cancel();
        if(const auto port=owned_port.load()){
            try{if(offer.kind==DeviceKind::storage)runner->detach_interrupted(port,local_port,offer.bus,serial);else runner->detach_generic(port,local_port,offer.bus);report("USB_PORT_CLEANUP Session import port detached.");}
            catch(const std::exception& error){report("USB_PORT_CLEANUP "+clean(error.what()));}
        }
    }};
    // Written only by the worker and read after join: retain the import error
    // instead of replacing it with a secondary TLS cancellation/cleanup error.
    std::exception_ptr import_failure;
    std::jthread importer([&](std::stop_token token){try{
        Com worker_com;const auto port=offer.kind==DeviceKind::storage?runner->attach(local_port,offer.bus,serial):runner->attach_generic(local_port,offer.bus);owned_port=port;
        report("USB_WAITING Import port allocated; waiting for the owned USB device.");
        bool ready=false;std::string last_enumeration_error;
        auto next_probe=Clock::now();const auto enumeration_deadline=Clock::now()+std::chrono::seconds(30);
        while(!token.stop_requested()&&!session_stop.load()){
            if(!ready&&Clock::now()>=next_probe){
                try{if(offer.kind==DeviceKind::storage)(void)owned_disk(offer,serial);else verify_generic_node(generic_node(port,offer),port);eject_check(verified_import(*runner,port,local_port,offer,serial),TunnelControl::return_identity,"Imported port identity changed during enumeration");ready=true;notice=TunnelControl::storage_ready;report("USB_STARTED receiving");}
                catch(const std::exception& error){const std::string detail=clean(error.what());if(detail!=last_enumeration_error){last_enumeration_error=detail;report("USB_ENUMERATING "+detail);}}
                next_probe=Clock::now()+std::chrono::milliseconds(500);
                if(!ready&&Clock::now()>=enumeration_deadline){
                    report("USB_NOT_READY "+last_enumeration_error);
                    throw std::runtime_error("USB device did not enumerate within 30 seconds: "+last_enumeration_error);
                }
            }
            if(eject.exchange(false)){
                in_eject=true;
                try{eject_owned(*runner,port,local_port,offer,serial);clean_eject=true;in_eject=false;report("USB_EJECTED receiving");return;}
                catch(const EjectFailure& error){in_eject=false;notice=error.reason;report(std::string(error.reason==TunnelControl::return_busy?"USB_EJECT_BUSY ":"USB_RETURN_FAILED ")+clean(error.what()));}
                catch(const std::exception& error){in_eject=false;notice=TunnelControl::return_failed;report("USB_RETURN_FAILED "+clean(error.what()));}
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }catch(const std::exception& error){import_failure=std::current_exception();report("USB_IMPORT_FAILED "+clean(error.what()));session_stop=true;}});
    try{
    std::unique_ptr<net::TcpStream> local;const auto deadline=Clock::now()+std::chrono::seconds(20);while(!session_stop.load()&&Clock::now()<deadline){try{local=std::make_unique<net::TcpStream>(local_listener.accept());break;}catch(const std::exception& e){if(std::string(e.what())!="Accept timed out")throw;}}
    check(local!=nullptr,"USB driver did not connect to its private tunnel");
    pump_tunnel(remote,*local,true,"",session_stop,true,&eject,&notice,{},[&](bool local_failure){
        // Device eject may close/reset its loopback socket before the PnP call
        // returns. Only a completed successful eject authorizes the receipt.
        const auto limit=Clock::now()+std::chrono::seconds(2);
        while(local_failure&&in_eject.load()&&!clean_eject.load()&&Clock::now()<limit)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return clean_eject.load();});const auto wait=Clock::now()+std::chrono::seconds(5);while(in_eject.load()&&!clean_eject.load()&&Clock::now()<wait)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(clean_eject.load()){remote.write(receipt(offer),Clock::now()+std::chrono::seconds(5));try{if(offer.kind==DeviceKind::storage)runner->detach_ejected(owned_port,local_port,offer.bus,serial);else runner->detach_generic(owned_port,local_port,offer.bus);owned_port=0;}catch(const std::exception& e){report("USB_PORT_CLEANUP "+std::string(e.what()));}}
        else report("USB_DISCONNECTED no clean eject receipt; source will revoke export and attempt local return");
    }catch(...){session_stop=true;importer.request_stop();importer.join();if(import_failure)std::rethrow_exception(import_failure);throw;}
    importer.request_stop();importer.join();if(import_failure)std::rethrow_exception(import_failure);
}
void receive_storage(const net::Options& options,std::atomic_bool& stop,std::atomic_bool& eject,const Report& report){
    Com com;check(IsUserAnAdmin(),"USB receiver requires explicit elevation");auto runner=native_import_runner();auto readiness=runner->ready();check(readiness.available,readiness.reason.c_str());net::TcpListener listener(options);report("LISTENING "+listener.endpoints());
    std::unique_ptr<net::TcpStream> socket;while(!stop.load()){try{socket=std::make_unique<net::TcpStream>(listener.accept());break;}catch(const std::exception& e){if(stop.load())return;if(std::string(e.what())!="Accept timed out")throw;}}
    if(!socket)return;
    try{security::TlsStream remote(std::move(*socket),true,"");Cancellation cancellation(remote,stop);remote.handshake(Clock::now()+std::chrono::seconds(15));receive_storage_admitted(remote,stop,eject,report);}
    catch(const std::exception& error){if(!stop.load())report("USB_SESSION_ERROR "+std::string(error.what()));}
}
}
