#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <sddl.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <dm/usb_recovery.hpp>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace dm::usb {
namespace {
using extrapane::Availability;
void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
struct Handle {HANDLE h=INVALID_HANDLE_VALUE;explicit Handle(HANDLE value=INVALID_HANDLE_VALUE):h(value){}~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}Handle(const Handle&)=delete;Handle(Handle&& other)noexcept:h(std::exchange(other.h,INVALID_HANDLE_VALUE)){};};
struct Key {HKEY h=nullptr;~Key(){if(h)RegCloseKey(h);}Key()=default;Key(const Key&)=delete;Key(Key&& k)noexcept:h(std::exchange(k.h,nullptr)){}Key& operator=(Key&& k)noexcept{if(h)RegCloseKey(h);h=std::exchange(k.h,nullptr);return *this;}};
struct Local {HLOCAL p=nullptr;~Local(){LocalFree(p);}};
std::array<std::uint8_t,32> hash(std::span<const std::uint8_t> data){
    need(data.size()<=4*1024*1024,"USB recovery identity exceeds bounds");std::array<std::uint8_t,32> result{};
    need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(data.data()),static_cast<ULONG>(data.size()),result.data(),static_cast<ULONG>(result.size()))>=0,"USB recovery identity hash unavailable");return result;
}
std::span<const std::uint8_t> raw(const std::wstring& value){return {reinterpret_cast<const std::uint8_t*>(value.data()),value.size()*sizeof(wchar_t)};}
std::wstring hex(std::span<const std::uint8_t> bytes){std::wstring result;constexpr wchar_t digits[]=L"0123456789abcdef";for(auto b:bytes){result+=digits[b>>4];result+=digits[b&15];}return result;}
std::wstring key_name(const Disk& disk){need(!disk.instance_id.empty()&&disk.instance_id.size()<MAX_DEVICE_ID_LEN,"USB disk instance identity is missing");return hex(hash(raw(disk.instance_id)));}
std::array<std::uint8_t,32> fingerprint(const Disk& disk){
    need(!disk.usb_instance_id.empty()&&disk.usb_instance_id.size()<MAX_DEVICE_ID_LEN&&!disk.descriptor_bytes.empty()&&!disk.layout_bytes.empty()&&disk.disk_number!=0xfffffffful,"USB recovery identity is incomplete");
    std::vector<std::uint8_t> bytes;
    auto append=[&](std::span<const std::uint8_t> part){need(part.size()<=1024*1024,"USB disk identity component exceeds bounds");for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<std::uint8_t>(part.size()>>(8*i)));bytes.insert(bytes.end(),part.begin(),part.end());};
    append(raw(disk.instance_id));append(raw(disk.usb_instance_id));append(disk.descriptor_bytes);append(disk.layout_bytes);
    for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<std::uint8_t>(disk.disk_number>>(8*i)));return hash(bytes);
}
bool privileged(PSID sid){return sid&&(IsWellKnownSid(sid,WinLocalSystemSid)||IsWellKnownSid(sid,WinBuiltinAdministratorsSid));}
void trusted_key(HKEY key){
    DWORD kind=0,length=0;const auto link=RegQueryValueExW(key,L"SymbolicLinkValue",nullptr,&kind,nullptr,&length);
    need(link==ERROR_FILE_NOT_FOUND,"USB recovery registry key is a link or contains unexpected link metadata");
    PSECURITY_DESCRIPTOR sd=nullptr;PSID owner=nullptr;PACL dacl=nullptr;
    need(GetSecurityInfo(key,SE_REGISTRY_KEY,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&dacl,nullptr,&sd)==ERROR_SUCCESS,"Cannot verify USB recovery registry security");Local free{sd};
    SECURITY_DESCRIPTOR_CONTROL control=0;DWORD revision=0;
    need(GetSecurityDescriptorControl(sd,&control,&revision)&&privileged(owner)&&dacl&&(control&SE_DACL_PROTECTED),"USB recovery registry ownership or ACL is not protected");
    constexpr DWORD writes=KEY_SET_VALUE|KEY_CREATE_SUB_KEY|KEY_CREATE_LINK|DELETE|WRITE_DAC|WRITE_OWNER|GENERIC_WRITE|GENERIC_ALL;
    for(DWORD i=0;i<dacl->AceCount;++i){void* ace=nullptr;need(GetAce(dacl,i,&ace),"Cannot inspect recovery registry ACL");const auto header=static_cast<ACE_HEADER*>(ace);
        if(header->AceType==ACCESS_DENIED_ACE_TYPE)continue;
        need(header->AceType==ACCESS_ALLOWED_ACE_TYPE,"Unsupported recovery registry ACL entry");const auto allow=static_cast<ACCESS_ALLOWED_ACE*>(ace);
        if(allow->Mask&writes)need(privileged(reinterpret_cast<PSID>(&allow->SidStart)),"USB recovery journal is writable by an untrusted account");
    }
}
Key child(HKEY parent,const std::wstring& name,bool create,bool requireNew=false){
    Key key;auto result=RegOpenKeyExW(parent,name.c_str(),REG_OPTION_OPEN_LINK,KEY_READ|KEY_WRITE|READ_CONTROL|KEY_WOW64_64KEY,&key.h);
    if(result==ERROR_FILE_NOT_FOUND&&create){
        PSECURITY_DESCRIPTOR sd=nullptr;need(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;KA;;;SY)(A;;KA;;;BA)(A;;KR;;;BU)",SDDL_REVISION_1,&sd,nullptr),"Create protected recovery ACL failed");Local free{sd};SECURITY_ATTRIBUTES security{sizeof(security),sd,FALSE};DWORD disposition=0;
        result=RegCreateKeyExW(parent,name.c_str(),0,nullptr,REG_OPTION_NON_VOLATILE,KEY_READ|KEY_WRITE|READ_CONTROL|KEY_WOW64_64KEY,&security,&key.h,&disposition);
        need(result==ERROR_SUCCESS&&(!requireNew||disposition==REG_CREATED_NEW_KEY),"A foreign or incomplete USB recovery journal already exists");
        // Reopen the name without following a registry link before touching data.
        RegCloseKey(key.h);key.h=nullptr;result=RegOpenKeyExW(parent,name.c_str(),REG_OPTION_OPEN_LINK,KEY_READ|KEY_WRITE|READ_CONTROL|KEY_WOW64_64KEY,&key.h);
    }else if(requireNew&&result==ERROR_SUCCESS)throw std::runtime_error("A foreign or incomplete USB recovery journal already exists");
    need(result==ERROR_SUCCESS,"USB recovery journal unavailable; administrator setup or explicit recovery is required");trusted_key(key.h);return key;
}
Key root(bool create){
    Key software;need(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE",REG_OPTION_OPEN_LINK,KEY_READ|KEY_WRITE|KEY_WOW64_64KEY,&software.h)==ERROR_SUCCESS,"Administrator access is required for USB quarantine");
    auto app=child(software.h,L"ExtraPane",create);auto components=child(app.h,L"Components",create);return child(components.h,L"UsbRecovery",create);
}
RecoveryRecord load(HKEY key){
    trusted_key(key);DWORD type=0,size=0;need(RegQueryValueExW(key,L"Journal",nullptr,&type,nullptr,&size)==ERROR_SUCCESS&&type==REG_BINARY&&size>=72&&size<=174,"USB recovery journal is missing or corrupt");
    std::vector<std::uint8_t> bytes(size);need(RegQueryValueExW(key,L"Journal",nullptr,&type,bytes.data(),&size)==ERROR_SUCCESS&&type==REG_BINARY&&size==bytes.size(),"USB recovery journal changed during read");return decode_recovery(bytes);
}
void store(HKEY key,const RecoveryRecord& record){
    trusted_key(key);const auto bytes=encode_recovery(record);need(RegSetValueExW(key,L"Journal",0,REG_BINARY,bytes.data(),static_cast<DWORD>(bytes.size()))==ERROR_SUCCESS,"Persist USB recovery journal failed");
    need(RegFlushKey(key)==ERROR_SUCCESS,"Flush USB recovery journal failed; do not export the device");
}
struct Lease {
    Handle handle;bool acquired=false;
    explicit Lease(const Disk& disk):handle(CreateMutexW(nullptr,FALSE,(L"Global\\ExtraPane.UsbRecovery."+key_name(disk)).c_str())){
        need(handle.h,"Create USB quarantine ownership mutex failed");const auto value=WaitForSingleObject(handle.h,0);need(value==WAIT_OBJECT_0||value==WAIT_ABANDONED,"Another USB quarantine or recovery operation owns this disk");acquired=true;
    }
    ~Lease(){if(acquired)ReleaseMutex(handle.h);}
};
std::vector<std::uint8_t> query(HANDLE handle,DWORD control,void* input=nullptr,DWORD inputSize=0){
    for(DWORD size=4096;size<=1024*1024;size*=2){std::vector<std::uint8_t> bytes(size);DWORD returned=0;
        if(DeviceIoControl(handle,control,input,inputSize,bytes.data(),size,&returned,nullptr)){need(returned<=size,"Invalid disk identity response");bytes.resize(returned);return bytes;}
        const auto error=GetLastError();
        if(error!=ERROR_MORE_DATA&&error!=ERROR_INSUFFICIENT_BUFFER)throw std::runtime_error("USB disk identity query failed (IOCTL "+std::to_string(control)+", Windows error "+std::to_string(error)+")");}
    throw std::runtime_error("USB disk identity exceeds bounds");
}
// Match the selected path against present SetupAPI disk interfaces, not a remote
// string or a constructed PhysicalDrive number. Then check disk/USB instance IDs.
void present(const Disk& disk){
    HDEVINFO set=SetupDiGetClassDevsW(&GUID_DEVINTERFACE_DISK,nullptr,nullptr,DIGCF_DEVICEINTERFACE|DIGCF_PRESENT);need(set!=INVALID_HANDLE_VALUE,"Enumerate present disks failed");struct Free{HDEVINFO h;~Free(){SetupDiDestroyDeviceInfoList(h);}}free{set};
    for(DWORD i=0;;++i){SP_DEVICE_INTERFACE_DATA data{sizeof(data)};if(!SetupDiEnumDeviceInterfaces(set,nullptr,&GUID_DEVINTERFACE_DISK,i,&data)){need(GetLastError()==ERROR_NO_MORE_ITEMS,"Disk interface enumeration failed");break;}
        DWORD required=0;SetupDiGetDeviceInterfaceDetailW(set,&data,nullptr,0,&required,nullptr);need(required>=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)&&required<=65536,"Invalid disk interface size");
        std::vector<std::uint8_t> storage(required);auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());detail->cbSize=sizeof(*detail);SP_DEVINFO_DATA info{sizeof(info)};
        need(SetupDiGetDeviceInterfaceDetailW(set,&data,detail,required,nullptr,&info),"Read disk interface identity failed");
        if(_wcsicmp(disk.interface_path.c_str(),detail->DevicePath)!=0)continue;
        wchar_t id[MAX_DEVICE_ID_LEN]{};need(CM_Get_Device_IDW(info.DevInst,id,MAX_DEVICE_ID_LEN,0)==CR_SUCCESS&&_wcsicmp(disk.instance_id.c_str(),id)==0,"Selected disk instance changed");
        DEVINST current=info.DevInst;for(unsigned n=0;n<32;++n){
            need(CM_Get_Device_IDW(current,id,MAX_DEVICE_ID_LEN,0)==CR_SUCCESS,"Cannot verify USB device ancestry");if(_wcsicmp(disk.usb_instance_id.c_str(),id)==0){
                ULONG caps=0,type=0,size=sizeof(caps);
                need(CM_Get_DevNode_Registry_PropertyW(current,CM_DRP_CAPABILITIES,&type,&caps,&size,0)==CR_SUCCESS&&type==REG_DWORD&&size==sizeof(caps)&&(caps&CM_DEVCAP_UNIQUEID),"USB device does not report a unique physical identity");
                const auto descriptor=detail::descriptor(disk.descriptor_bytes);
                need(disk.unique_identity&&descriptor&&descriptor->stable_serial,"USB storage serial is missing or unsuitable for durable recovery");return;
            }
            DEVINST parent=0;need(CM_Get_Parent(&parent,current,0)==CR_SUCCESS,"Selected disk no longer has the expected USB ancestor");current=parent;
        }throw std::runtime_error("USB device ancestry exceeds bounds");
    }throw std::runtime_error("Selected local disk interface is no longer present");
}
void validate_handle(HANDLE handle,const Disk& disk,bool checkLayout=true){
    present(disk);const auto bytes=query(handle,IOCTL_STORAGE_GET_DEVICE_NUMBER);need(bytes.size()>=sizeof(STORAGE_DEVICE_NUMBER),"Disk number is unavailable");STORAGE_DEVICE_NUMBER number{};std::memcpy(&number,bytes.data(),sizeof(number));
    need(number.DeviceType==FILE_DEVICE_DISK&&number.DeviceNumber==disk.disk_number,"Selected physical disk number changed");
    STORAGE_PROPERTY_QUERY request{};request.PropertyId=StorageDeviceProperty;request.QueryType=PropertyStandardQuery;
    need(query(handle,IOCTL_STORAGE_QUERY_PROPERTY,&request,sizeof(request))==disk.descriptor_bytes&&(!checkLayout||query(handle,IOCTL_DISK_GET_DRIVE_LAYOUT_EX)==disk.layout_bytes),"Disk descriptor or partition identity changed");
}
std::array<std::uint8_t,32> stable_fingerprint(HANDLE handle,const Disk& disk){
    validate_handle(handle,disk,false);
    const auto geometryBytes=query(handle,IOCTL_DISK_GET_DRIVE_GEOMETRY),lengthBytes=query(handle,IOCTL_DISK_GET_LENGTH_INFO);
    need(geometryBytes.size()>=sizeof(DISK_GEOMETRY)&&lengthBytes.size()>=sizeof(GET_LENGTH_INFORMATION),"Cannot read physical disk geometry");
    DISK_GEOMETRY geometry{};GET_LENGTH_INFORMATION length{};std::memcpy(&geometry,geometryBytes.data(),sizeof geometry);std::memcpy(&length,lengthBytes.data(),sizeof length);
    need(length.Length.QuadPart>0,"Invalid physical disk length");
    auto metadata=detail::partition_metadata([&](std::uint64_t offset,std::uint32_t count){
        LARGE_INTEGER position{};position.QuadPart=static_cast<LONGLONG>(offset);need(SetFilePointerEx(handle,position,nullptr,FILE_BEGIN),"Cannot seek USB partition metadata");
        std::vector<std::uint8_t> bytes(count);DWORD read=0;need(ReadFile(handle,bytes.data(),count,&read,nullptr)&&read==count,"Cannot read USB partition metadata");return bytes;
    },geometry.BytesPerSector,static_cast<std::uint64_t>(length.Length.QuadPart));
    for(unsigned i=0;i<8;++i)metadata.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(length.Length.QuadPart)>>(i*8)));
    for(unsigned i=0;i<4;++i)metadata.push_back(static_cast<std::uint8_t>(geometry.BytesPerSector>>(i*8)));
    Disk snapshot=disk;snapshot.layout_bytes=std::move(metadata);return fingerprint(snapshot);
}
std::uint64_t attributes(HANDLE handle){GET_DISK_ATTRIBUTES data{};DWORD returned=0;need(DeviceIoControl(handle,IOCTL_DISK_GET_DISK_ATTRIBUTES,nullptr,0,&data,sizeof(data),&returned,nullptr)&&returned>=sizeof(data)&&data.Version==sizeof(data),"Cannot read persistent disk attributes");return data.Attributes;}
void offline(HANDLE handle,bool value){
    SET_DISK_ATTRIBUTES data{};data.Version=sizeof(data);data.Persist=TRUE;data.Attributes=value?DISK_ATTRIBUTE_OFFLINE:0;data.AttributesMask=DISK_ATTRIBUTE_OFFLINE;DWORD returned=0;
    need(DeviceIoControl(handle,IOCTL_DISK_SET_DISK_ATTRIBUTES,&data,sizeof(data),nullptr,0,&returned,nullptr),"Windows refused persistent disk offline/online change; the journal requires recovery");
    need(((attributes(handle)&DISK_ATTRIBUTE_OFFLINE)!=0)==value,"Disk offline state did not match the requested persistent change");
}
Handle open_disk(const Disk& disk,bool checkLayout=true){present(disk);Handle h(CreateFileW(disk.interface_path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));if(h.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open selected disk for persistent quarantine (Windows error "+std::to_string(GetLastError())+")");validate_handle(h.h,disk,checkLayout);return h;}
std::string guid(const std::wstring& value){std::string result;for(wchar_t c:value){need(c<128,"Invalid USB binding identifier");result+=static_cast<char>(c);}return result;}
class NativeGuard final:public RecoveryGuard {
public:
    explicit NativeGuard(LockedDiskVerifier verifier):locked_(std::move(verifier)){need(static_cast<bool>(locked_),"USB quarantine requires a locked-handle verifier");}
    Availability quarantine(const Disk& disk)override{return attempt([&]{
        need(!lease_,"This USB recovery guard already owns a generation");need(extrapane::usb::eligible(disk.facts).available,"Selected USB disk is not eligible for exclusive sharing");
        lease_=std::make_unique<Lease>(disk);
        need(!has_pending_recovery(disk,pending_recoveries()),"This USB disk has a pending ExtraPane recovery record. Restore the source disk before exporting it again.");
        auto proof=locked_(disk);need(proof.available,"Source volume locks or locked physical identity cannot be verified");
        auto physical=open_disk(disk);const auto prior=attributes(physical.h);need(!(prior&DISK_ATTRIBUTE_OFFLINE),"A disk that was already offline cannot be claimed for export");
        const auto identity=stable_fingerprint(physical.h,disk);
        auto parent=root(true);key_=child(parent.h,key_name(disk),true,true);record_.version=2;record_.disk_number=disk.disk_number;record_.prior_attributes=prior;record_.fingerprint=identity;snapshot_identity_=fingerprint(disk);
        need(BCryptGenRandom(nullptr,record_.owner.data(),static_cast<ULONG>(record_.owner.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"USB recovery owner identity unavailable");store(key_.h,record_);
        // The durable PREPARED record precedes every disk mutation. Recheck the
        // independently held volume locks and same physical handle immediately.
        proof=locked_(disk);need(proof.available,"Source volume locks changed before offline");validate_handle(physical.h,disk);offline(physical.h,true);transition(RecoveryState::offline);
    });}
    Availability verify(const Disk& disk)override{return attempt([&]{owned(disk);need(record_.state!=RecoveryState::prepared&&record_.state!=RecoveryState::restored,"Disk quarantine was not completed");auto physical=open_disk(disk,record_.version==1);need((attributes(physical.h)&DISK_ATTRIBUTE_OFFLINE)!=0,"Source disk is no longer offline; export is forbidden");});}
    Availability binding_intent(const Disk& disk,const std::string& bus)override{return attempt([&]{owned(disk);need(record_.state==RecoveryState::offline,"USB binding intent requires completed quarantine");auto physical=open_disk(disk,record_.version==1);need(attributes(physical.h)&DISK_ATTRIBUTE_OFFLINE,"Disk must stay offline before binding");auto next=record_;next.bus_id=bus;next.state=RecoveryState::binding_intent;persist(next);});}
    Availability binding_acquired(const Disk& disk,const std::wstring& binding)override{return attempt([&]{owned_journal(disk);auto next=record_;next.binding_id=guid(binding);next.state=RecoveryState::bound;persist(next);});}
    // During native USB attachment the source disk interface may be absent. A
    // trusted backend's eject receipt advances only this protected generation;
    // physical medium validation remains mandatory before returning it online.
    Availability destination_ejected(const Disk& disk)override{return attempt([&]{owned_journal(disk);transition(RecoveryState::ejected);});}
    Availability binding_released(const Disk& disk,const std::wstring& binding)override{return attempt([&]{owned_journal(disk);need(guid(binding)==record_.binding_id,"USB backend released a different binding");transition(RecoveryState::released);});}
    Availability restore_source(const Disk& disk,const RecoveryReleaseVerifier& released)override{return attempt([&]{
        owned(disk);need(record_.state==RecoveryState::released&&static_cast<bool>(released),"Automatic return requires durable eject and exact binding release");
        const auto proof=released(record_);need(proof.available,"USB backend absence could not be verified for automatic return");
        auto physical=open_disk(disk,record_.version==1);need(stable_fingerprint(physical.h,disk)==record_.fingerprint,"Source medium changed before automatic return");
        need(load(key_.h)==record_,"Journal changed before automatic return");
        offline(physical.h,(record_.prior_attributes&DISK_ATTRIBUTE_OFFLINE)!=0);transition(RecoveryState::restored);
        auto parent=root(false);RegCloseKey(key_.h);key_.h=nullptr;
        need(RegDeleteKeyExW(parent.h,key_name(disk).c_str(),KEY_WOW64_64KEY,0)==ERROR_SUCCESS,"Source returned but journal retirement failed");need(RegFlushKey(parent.h)==ERROR_SUCCESS,"Source-return journal retirement could not be flushed");
    });}
    // Default destruction only closes registry/mutex handles; never online,
    // remove journal, release a remote binding or undo persistent quarantine.
private:
    LockedDiskVerifier locked_;std::unique_ptr<Lease> lease_;Key key_;RecoveryRecord record_;std::array<std::uint8_t,32> snapshot_identity_{};
    template<class F>Availability attempt(F action){try{action();return {true,{}};}catch(const std::exception& error){return {false,error.what()};}}
    void owned_journal(const Disk& disk){need(key_.h&&lease_&&record_.disk_number==disk.disk_number&&snapshot_identity_==fingerprint(disk),"USB quarantine generation or selected identity mismatch");need(load(key_.h)==record_,"USB recovery journal changed or belongs to another owner");}
    void owned(const Disk& disk){owned_journal(disk);need(record_.fingerprint==(record_.version==1?fingerprint(disk):stable_recovery_fingerprint(disk)),"USB source medium identity changed");}
    void persist(const RecoveryRecord& next){need(valid_recovery_transition(record_.state,next.state),"Invalid USB recovery journal transition");need(load(key_.h)==record_,"USB recovery journal changed before persistence");store(key_.h,next);record_=next;}
    void transition(RecoveryState state){auto next=record_;next.state=state;persist(next);}
};
}
std::unique_ptr<RecoveryGuard> native_recovery_guard(LockedDiskVerifier verifier){return std::make_unique<NativeGuard>(std::move(verifier));}
std::vector<PendingRecovery> pending_recoveries(){
    Key current;HKEY parent=HKEY_LOCAL_MACHINE;
    for(const auto* name:{L"SOFTWARE",L"ExtraPane",L"Components",L"UsbRecovery"}){
        Key next;
        const auto status=RegOpenKeyExW(parent,name,REG_OPTION_OPEN_LINK,KEY_READ|READ_CONTROL|KEY_WOW64_64KEY,&next.h);
        if(status==ERROR_FILE_NOT_FOUND)return {};
        need(status==ERROR_SUCCESS,"Cannot read protected USB recovery journals; administrator access may be required");
        if(std::wstring_view(name)!=L"SOFTWARE")trusted_key(next.h);
        current=std::move(next);parent=current.h;
    }
    std::vector<PendingRecovery> result;
    for(DWORD index=0;;++index){
        wchar_t name[256]{};DWORD length=256;
        const auto status=RegEnumKeyExW(current.h,index,name,&length,nullptr,nullptr,nullptr,nullptr);
        if(status==ERROR_NO_MORE_ITEMS)break;
        need(status==ERROR_SUCCESS&&result.size()<1024,"USB recovery inventory is incomplete or exceeds bounds");
        PendingRecovery pending;pending.journal_id.assign(name,length);
        try{
            need(length==64&&std::all_of(pending.journal_id.begin(),pending.journal_id.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');}),"Unrecognized USB recovery journal identifier");
            Key key;
            need(RegOpenKeyExW(current.h,name,REG_OPTION_OPEN_LINK,KEY_READ|READ_CONTROL|KEY_WOW64_64KEY,&key.h)==ERROR_SUCCESS,"Cannot read USB recovery journal");
            pending.record=load(key.h);
        }catch(const std::exception& error){pending.error=error.what();}
        result.push_back(std::move(pending));
    }
    return result;
}
bool has_pending_recovery(const Disk& disk,std::span<const PendingRecovery> pending){
    const auto id=key_name(disk);
    return std::any_of(pending.begin(),pending.end(),[&](const auto& item){return item.journal_id==id;});
}
std::array<std::uint8_t,32> recovery_fingerprint(const Disk& disk){return fingerprint(disk);}
std::array<std::uint8_t,32> stable_recovery_fingerprint(const Disk& disk){
    present(disk);Handle physical(CreateFileW(disk.interface_path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    need(physical.h!=INVALID_HANDLE_VALUE,"Cannot open USB partition metadata for identity verification");return stable_fingerprint(physical.h,disk);
}
bool matches_recovery(const Disk& disk,const PendingRecovery& pending){
    try{return pending.record&&key_name(disk)==pending.journal_id&&pending.record->disk_number==disk.disk_number&&pending.record->fingerprint==(pending.record->version==1?fingerprint(disk):stable_recovery_fingerprint(disk));}
    catch(...){return false;}
}
Availability recovery_state_allows_restore(const RecoveryRecord& record){
    if(record.state==RecoveryState::prepared||record.state==RecoveryState::offline){
        if(!record.bus_id.empty()||!record.binding_id.empty())return {false,"Pre-export recovery journal contains unexpected binding data"};
        return {true,{}};
    }
    if(record.state==RecoveryState::released||record.state==RecoveryState::restored||record.state==RecoveryState::ejected)return {true,{}};
    return {false,"Destination eject and exact owned binding release have not been confirmed; keep this disk offline"};
}
Availability preexport_retirement_policy(const RecoveryRecord& record){
    if((record.state!=RecoveryState::prepared&&record.state!=RecoveryState::offline)||
       !record.bus_id.empty()||!record.binding_id.empty()||(record.prior_attributes&DISK_ATTRIBUTE_OFFLINE))
        return {false,"Only a never-exported, originally online journal can be retired without restoring a disk"};
    return {true,{}};
}
Availability assess_online_preexport(const Disk& disk,const PendingRecovery& pending,const RecoveryReleaseVerifier& absent){
    try{
        need(pending.record&&pending.error.empty()&&pending.journal_id==key_name(disk)&&pending.record->disk_number==disk.disk_number,"Pre-export journal does not identify this local disk slot");
        need(preexport_retirement_policy(*pending.record).available,"Journal has export or binding history; automatic retirement refused");
        need(static_cast<bool>(absent),"Fresh local backend ownership verification is required");
        // Read-only handle: assessment neither writes the journal nor changes a
        // disk. A different card in the same reader is not asserted identical.
        present(disk);Handle physical(CreateFileW(disk.interface_path.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        need(physical.h!=INVALID_HANDLE_VALUE,"Cannot inspect selected disk attributes");validate_handle(physical.h,disk);
        need(!(attributes(physical.h)&DISK_ATTRIBUTE_OFFLINE),"Source disk is still offline; verified restoration is required");
        const auto proof=absent(*pending.record);if(!proof.available)return proof;
        validate_handle(physical.h,disk);need(!(attributes(physical.h)&DISK_ATTRIBUTE_OFFLINE),"Source disk went offline during reconciliation");
        return {true,{}};
    }catch(const std::exception& error){return {false,error.what()};}
}
Availability retire_online_preexport(const Disk& disk,const PendingRecovery& pending,const RecoveryReleaseVerifier& absent){
    try{
        Lease lease(disk);auto parent=root(false);auto key=child(parent.h,key_name(disk),false);
        need(pending.record&&load(key.h)==*pending.record,"Pre-export journal changed before reconciliation");
        const auto proof=assess_online_preexport(disk,pending,absent);if(!proof.available)return proof;
        need(load(key.h)==*pending.record,"Pre-export journal changed during reconciliation");
        RegCloseKey(key.h);key.h=nullptr;
        need(RegDeleteKeyExW(parent.h,key_name(disk).c_str(),KEY_WOW64_64KEY,0)==ERROR_SUCCESS,"Cannot retire already-online pre-export journal");
        need(RegFlushKey(parent.h)==ERROR_SUCCESS,"Pre-export journal retirement could not be flushed");
        return {true,{}};
    }catch(const std::exception& error){return {false,error.what()};}
}
Availability recover_quarantined_disk(const Disk& disk,const RecoveryReleaseVerifier& released){
    try{
        need(static_cast<bool>(released),"Explicit trusted destination eject and binding release verification is required");Lease lease(disk);auto parent=root(false);auto key=child(parent.h,key_name(disk),false);auto record=load(key.h);
        need(record.disk_number==disk.disk_number&&record.fingerprint==(record.version==1?fingerprint(disk):stable_recovery_fingerprint(disk)),"Recovery refused: disk number, descriptor or partitions no longer match the journal");
        need(recovery_state_allows_restore(record).available,"Journal does not contain sufficient pre-export or verified eject evidence for recovery");
        // Verify local identity and offline state before a recovery verifier may
        // finish removing this journal's exact owned binding after clean eject.
        auto physical=open_disk(disk,record.version==1);
        if(record.state==RecoveryState::ejected)need(attributes(physical.h)&DISK_ATTRIBUTE_OFFLINE,"An ejected disk lost its offline quarantine; recovery refused");
        const auto proof=released(record);need(proof.available,"Destination eject or exact USB backend release is unconfirmed; disk remains offline");
        need(load(key.h)==record,"USB recovery journal changed during release verification");
        if(record.state==RecoveryState::ejected){record.state=RecoveryState::released;store(key.h,record);}
        validate_handle(physical.h,disk,record.version==1);
        if(record.version==2)need(stable_fingerprint(physical.h,disk)==record.fingerprint,"USB medium changed during recovery");
        // Set only OFFLINE, preserving read-only and all unrelated current bits.
        const bool priorOffline=(record.prior_attributes&DISK_ATTRIBUTE_OFFLINE)!=0;
        offline(physical.h,priorOffline);validate_handle(physical.h,disk,record.version==1);
        record.state=RecoveryState::restored;store(key.h,record);
        // Deletion is explicit recovery's final action. A crash before deletion
        // leaves a journal that prevents new export until recovery is repeated.
        RegCloseKey(key.h);key.h=nullptr;need(RegDeleteKeyExW(parent.h,key_name(disk).c_str(),KEY_WOW64_64KEY,0)==ERROR_SUCCESS,"Restored disk still has a journal; repeat explicit recovery before export");
        need(RegFlushKey(parent.h)==ERROR_SUCCESS,"Recovered journal deletion could not be flushed");return {true,{}};
    }catch(const std::exception& error){return {false,error.what()};}
}
}
