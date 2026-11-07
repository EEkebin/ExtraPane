#include <dm/usb_session.hpp>
#include <dm/usb_host_backend.hpp>
#include <dm/usb_tunnel.hpp>
#include <dm/tls.hpp>
#include <windows.h>
#include <shlobj.h>
#include <cwctype>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <bcrypt.h>
#include <winioctl.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <mutex>
#include <thread>
#include <sstream>
#include <iomanip>
namespace dm::usb {
namespace {
using Clock=std::chrono::steady_clock;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
struct Cancellation {std::jthread thread;Cancellation(security::TlsStream& stream,std::atomic_bool& stop):thread([&stream,&stop](std::stop_token token){while(!token.stop_requested()&&!stop.load())std::this_thread::sleep_for(std::chrono::milliseconds(25));if(stop.load())stream.cancel();}){}};
struct Com{HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Com(){check(SUCCEEDED(result),"USB session COM initialization failed");}~Com(){CoUninitialize();}};
struct Handle{HANDLE value=INVALID_HANDLE_VALUE;explicit Handle(HANDLE h):value(h){}~Handle(){if(value!=INVALID_HANDLE_VALUE&&value)CloseHandle(value);}Handle(const Handle&)=delete;};
bool same(const std::wstring& a,const std::wstring& b){return CompareStringOrdinal(a.c_str(),static_cast<int>(a.size()),b.c_str(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;}
std::string hex(std::span<const std::uint8_t> bytes){std::string out;for(auto b:bytes){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;}
std::string virtual_serial(){std::string value;while(value.size()<15){std::array<std::uint8_t,32> bytes{};check(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Cannot create imported device identity");for(auto byte:bytes)if(byte<252&&value.size()<15)value+="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[byte%36];}return value;}
std::string clean(std::string text){for(auto& c:text)if(static_cast<unsigned char>(c)<32)c=' ';return text.substr(0,1024);}
Inventory current_inventory(){auto runner=native_host_runner();const auto devices=runner->devices();return inventory([&](const std::wstring& id)->std::optional<bool>{std::optional<bool> result;for(const auto& d:devices)if(same(id,d.instance_id)){if(result)return {};result=d.forced||d.attached||!d.binding_id.empty();}return result;});}
std::uint16_t endpoint_port(const std::string& endpoint){const auto at=endpoint.rfind(':');check(at!=std::string::npos,"Invalid local USB listener");unsigned value=0;const auto text=endpoint.substr(at+1);const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);check(error==std::errc{}&&end==text.data()+text.size()&&value&&value<=65535,"Invalid local USB port");return static_cast<std::uint16_t>(value);}
std::uint16_t reserve_port(){net::Options options{"127.0.0.1","",0,std::chrono::milliseconds(250)};net::TcpListener listener(options);return endpoint_port(listener.endpoints());}
std::array<std::uint8_t,8> control(unsigned kind){return {'E','P','U','S',1,static_cast<std::uint8_t>(kind),0,0};}
void expect(security::TlsStream& stream,unsigned kind,std::chrono::seconds timeout=std::chrono::seconds(15)){std::array<std::uint8_t,8> packet{};stream.read(packet,Clock::now()+timeout);check(packet==control(kind),"USB resource negotiation failed");}
struct Offer{std::array<std::uint8_t,16> nonce{};std::uint16_t vendor=0,product=0;std::string bus;};
std::array<std::uint8_t,64> encode(const Offer& offer){check(!offer.bus.empty()&&offer.bus.size()<16,"Invalid selected bus");std::array<std::uint8_t,64> bytes{};auto prefix=control(1);std::copy(prefix.begin(),prefix.end(),bytes.begin());std::copy(offer.nonce.begin(),offer.nonce.end(),bytes.begin()+8);bytes[24]=offer.vendor>>8;bytes[25]=offer.vendor;bytes[26]=offer.product>>8;bytes[27]=offer.product;bytes[28]=static_cast<std::uint8_t>(offer.bus.size());std::copy(offer.bus.begin(),offer.bus.end(),bytes.begin()+29);return bytes;}
Offer decode(const std::array<std::uint8_t,64>& bytes){const auto prefix=control(1);check(std::equal(prefix.begin(),prefix.end(),bytes.begin())&&bytes[28]>0&&bytes[28]<16,"Unsupported USB offer");Offer offer;std::copy_n(bytes.begin()+8,16,offer.nonce.begin());offer.vendor=(bytes[24]<<8)|bytes[25];offer.product=(bytes[26]<<8)|bytes[27];offer.bus.assign(bytes.begin()+29,bytes.begin()+29+bytes[28]);check(std::all_of(bytes.begin()+29+bytes[28],bytes.end(),[](auto b){return b==0;}),"USB offer reserved data");const auto dash=offer.bus.find('-');check(dash>0&&dash!=std::string::npos&&dash+1<offer.bus.size()&&offer.bus.find('-',dash+1)==std::string::npos&&std::all_of(offer.bus.begin(),offer.bus.end(),[](char c){return(c>='0'&&c<='9')||c=='-';}),"Invalid offered USB bus");return offer;}
std::array<std::uint8_t,32> receipt(const Offer& offer){std::array<std::uint8_t,32> bytes{};auto prefix=control(4);std::copy(prefix.begin(),prefix.end(),bytes.begin());std::copy(offer.nonce.begin(),offer.nonce.end(),bytes.begin()+8);bytes[24]=offer.vendor>>8;bytes[25]=offer.vendor;bytes[26]=offer.product>>8;bytes[27]=offer.product;return bytes;}
unsigned id_word(const std::wstring& id,const wchar_t* name){auto upper=id;std::transform(upper.begin(),upper.end(),upper.begin(),[](wchar_t c){return towupper(c);});const auto at=upper.find(name);check(at!=std::wstring::npos&&at+8<=upper.size(),"Physical USB VID/PID unavailable");unsigned value=0;for(auto c:upper.substr(at+4,4)){check((c>=L'0'&&c<=L'9')||(c>=L'A'&&c<=L'F'),"Invalid physical USB VID/PID");value=value*16+(c<=L'9'?c-L'0':c-L'A'+10);}return value;}
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
void eject_owned(ImportRunner& runner,unsigned port,std::uint16_t local,const Offer& offer,const std::string& serial){
    check(runner.verify_owned(port,local,offer.bus,serial),"Imported port identity changed; refusing eject");const auto node=own_node(offer,serial);const auto id=node_id(node);
    const auto disks=inventory([](const std::wstring&)->std::optional<bool>{return false;});check(disks.errors.empty(),"Cannot prove destination volume inventory");unsigned matched=0;
    for(const auto& disk:disks.disks)if(same(disk.usb_instance_id,id)){
        check(extrapane::usb::eligible(disk.facts).available,"Imported disk is not safely removable");++matched;
        for(auto volume:disk.volumes){if(volume.ends_with(L"\\"))volume.pop_back();Handle handle(CreateFileW(volume.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));check(handle.value!=INVALID_HANDLE_VALUE,"Cannot open destination volume for flush");verify_volume_disk(handle.value,disk.disk_number);check(FlushFileBuffers(handle.value),"Destination volume cannot be flushed; close its files and retry");}
    }
    check(matched==1,"Expected exactly one owned destination storage disk");check(runner.verify_owned(port,local,offer.bus,serial)&&own_node(offer,serial)==node&&same(node_id(node),id),"USB destination changed before eject");PNP_VETO_TYPE veto=PNP_VetoTypeUnknown;wchar_t name[512]{};
    check(CM_Request_Device_EjectW(node,&veto,name,512,0)==CR_SUCCESS,"Destination is busy or Windows vetoed safe eject; close its files and retry");
}
}
void list_storage(const Report& report){Com com;const auto all=current_inventory();check(all.errors.empty()&&all.disks.size()<=128,"USB ownership inventory is incomplete or exceeds the device limit");for(const auto& disk:all.disks){check(!disk.facts.id.empty()&&disk.facts.id.size()<=4096,"USB inventory identifier exceeds limit");const auto eligibility=extrapane::usb::eligible(disk.facts);report("USB_DEVICE_V1\t"+hex(std::span(reinterpret_cast<const std::uint8_t*>(disk.facts.id.data()),disk.facts.id.size()))+"\t"+clean(disk.label)+"\t"+(eligibility.available?"available":clean(eligibility.reason)));}report("USB_LIST_DONE");}
void send_storage(const net::Options& options,const std::string& id,std::atomic_bool& stop,const Report& report){
    Com com;check(IsUserAnAdmin(),"USB source requires explicit elevation");const auto all=current_inventory();check(all.errors.empty(),"USB ownership inventory incomplete");const auto found=std::find_if(all.disks.begin(),all.disks.end(),[&](const Disk& d){return d.facts.id==id;});check(found!=all.disks.end()&&extrapane::usb::eligible(found->facts).available,"Select an eligible current local USB storage device");const Disk selected=*found;
    security::TlsStream remote(net::connect_stream(options),false,"");Cancellation cancellation(remote,stop);remote.handshake(Clock::now()+std::chrono::seconds(15));remote.write(control(0),Clock::now()+std::chrono::seconds(5));expect(remote,2);
    Offer offer;check(BCryptGenRandom(nullptr,offer.nonce.data(),16,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"Cannot create USB session nonce");offer.vendor=static_cast<std::uint16_t>(id_word(selected.usb_instance_id,L"VID_"));offer.product=static_cast<std::uint16_t>(id_word(selected.usb_instance_id,L"PID_"));
    auto handles=std::make_unique<ProvenHandles>();auto* proof=handles.get();auto recovery=native_recovery_guard([proof](const Disk& disk){if(!proof->locked||!proof->dismounted)return extrapane::Availability{false,"Source volume locks are not proven"};return proof->verify_locked(disk);});std::unique_ptr<net::TcpStream> local;bool clean_eject=false;
    HostTransport transport;
    transport.start=[&](const std::string& bus,std::uint16_t port){offer.bus=bus;local=std::make_unique<net::TcpStream>(net::connect_stream({"127.0.0.1","127.0.0.1",port,std::chrono::seconds(5)}));return extrapane::Availability{true,{}};};
    transport.handoff_quarantined=[proof](const Disk& disk){return proof->handoff_quarantined(disk);};
    transport.close=[&]{if(local)local->cancel();};transport.flush_and_eject=[&]{return extrapane::Availability{clean_eject,clean_eject?"":"Destination has not provided a verified session eject receipt"};};
    StorageOperations operations(selected,current_inventory,usb_host_backend(native_host_runner(),std::move(recovery),transport,reserve_port()),std::move(handles));extrapane::usb::Transfer transfer(operations);
    check(!stop.load(),"USB source canceled before preparation");
    const auto result=transfer.start(selected.facts.id,selected.facts.generation,true);if(!result.available){report("USB_RECOVERY_REQUIRED "+result.reason);throw std::runtime_error(result.reason);}
    try{
        remote.write(encode(offer),Clock::now()+std::chrono::seconds(5));expect(remote,3);report("USB_STARTED sending");pump_tunnel(remote,*local,false,offer.bus,stop,true);
        if(!stop.load()){std::array<std::uint8_t,32> proof_receipt{};remote.read(proof_receipt,Clock::now()+std::chrono::seconds(10));clean_eject=proof_receipt==receipt(offer);}
        if(clean_eject){const auto ejected=transfer.eject();check(ejected.available,ejected.reason.c_str());report("USB_EJECTED Use Restore source disk to bring the source disk online after verified recovery");}
        else transfer.transport_lost();
    }catch(...){transfer.transport_lost();report("USB_RECOVERY_REQUIRED source remains quarantined after connection loss");throw;}
    if(!clean_eject)report("USB_RECOVERY_REQUIRED source remains quarantined");
}
void receive_storage(const net::Options& options,std::atomic_bool& stop,std::atomic_bool& eject,const Report& report){
    Com com;check(IsUserAnAdmin(),"USB receiver requires explicit elevation");auto runner=native_import_runner();auto readiness=runner->ready();check(readiness.available,readiness.reason.c_str());net::TcpListener listener(options);report("LISTENING "+listener.endpoints());
    std::unique_ptr<net::TcpStream> socket;while(!stop.load()){try{socket=std::make_unique<net::TcpStream>(listener.accept());break;}catch(const std::exception& e){if(stop.load())return;if(std::string(e.what())!="Accept timed out")throw;}}
    if(!socket)return;
    try{
    std::atomic_bool session_stop=false;
    std::jthread parent_cancel([&](std::stop_token token){while(!token.stop_requested()&&!session_stop.load()&&!stop.load())std::this_thread::sleep_for(std::chrono::milliseconds(25));if(stop.load())session_stop=true;});
security::TlsStream remote(std::move(*socket),true,"");Cancellation cancellation(remote,session_stop);remote.handshake(Clock::now()+std::chrono::seconds(15));expect(remote,0);remote.write(control(2),Clock::now()+std::chrono::seconds(5));std::array<std::uint8_t,64> packet{};remote.read(packet,Clock::now()+std::chrono::seconds(30));const auto offer=decode(packet);
    net::Options loopback{"127.0.0.1","",0,std::chrono::milliseconds(250)};net::TcpListener local_listener(loopback);const auto local_port=endpoint_port(local_listener.endpoints());const auto serial=virtual_serial();remote.write(control(3),Clock::now()+std::chrono::seconds(5));
    std::atomic_bool clean_eject=false,in_eject=false;std::atomic_uint owned_port=0;
    std::jthread importer([&](std::stop_token token){try{Com worker_com;const auto port=runner->attach(local_port,offer.bus,serial);owned_port=port;report("USB_STARTED receiving");while(!token.stop_requested()&&!session_stop.load()){
        if(eject.exchange(false)){in_eject=true;try{eject_owned(*runner,port,local_port,offer,serial);clean_eject=true;in_eject=false;report("USB_EJECTED receiving");return;}catch(const std::exception& e){in_eject=false;report("USB_EJECT_BUSY "+std::string(e.what()));}}std::this_thread::sleep_for(std::chrono::milliseconds(50));}
    }catch(const std::exception& e){report("error: "+std::string(e.what()));session_stop=true;}});
    std::unique_ptr<net::TcpStream> local;const auto deadline=Clock::now()+std::chrono::seconds(20);while(!session_stop.load()&&Clock::now()<deadline){try{local=std::make_unique<net::TcpStream>(local_listener.accept());break;}catch(const std::exception& e){if(std::string(e.what())!="Accept timed out")throw;}}
    check(local!=nullptr,"USB driver did not connect to its private tunnel");
    try{pump_tunnel(remote,*local,true,"",session_stop,true);const auto wait=Clock::now()+std::chrono::seconds(5);while(in_eject.load()&&!clean_eject.load()&&Clock::now()<wait)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(clean_eject.load()){remote.write(receipt(offer),Clock::now()+std::chrono::seconds(5));try{runner->detach_ejected(owned_port,local_port,offer.bus,serial);}catch(const std::exception& e){report("USB_PORT_CLEANUP "+std::string(e.what()));}}
        else report("USB_DISCONNECTED no clean eject receipt; source recovery required");
    }catch(...){session_stop=true;throw;}
    importer.request_stop();
    }catch(const std::exception& error){if(!stop.load())report("USB_SESSION_ERROR "+std::string(error.what()));}

}
}
