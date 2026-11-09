#include <dm/usb_device.hpp>
#include <windows.h>
#include <cfgmgr32.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <set>
#include <sstream>
#include <iomanip>
namespace dm::usb {
namespace {
void need(bool ok){if(!ok)throw std::runtime_error("USB device tree could not be verified");}
std::wstring upper(std::wstring s){for(auto& c:s)c=towupper(c);return s;}
std::string utf8(const std::wstring& value){if(value.empty())return {};auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);need(n>0);std::string result(n,0);need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),result.data(),n,nullptr,nullptr)==n);return result;}
std::wstring node_id(DEVINST node){wchar_t id[MAX_DEVICE_ID_LEN]{};need(CM_Get_Device_IDW(node,id,MAX_DEVICE_ID_LEN,0)==CR_SUCCESS);return upper(id);}
std::vector<std::wstring> prop(DEVINST node,ULONG which,bool required=false){
    ULONG type=0,size=0;const auto first=CM_Get_DevNode_Registry_PropertyW(node,which,&type,nullptr,&size,0);
    if(first==CR_NO_SUCH_VALUE&&!required)return {};need(first==CR_BUFFER_SMALL&&size>=2&&size<=65536);
    std::vector<wchar_t> data(size/2+2,0);need(CM_Get_DevNode_Registry_PropertyW(node,which,&type,data.data(),&size,0)==CR_SUCCESS&&(type==REG_SZ||type==REG_MULTI_SZ));
    std::vector<std::wstring> result;for(std::size_t i=0;i<data.size()&&data[i];){std::wstring value(data.data()+i);need(i+value.size()<data.size());result.push_back(value);i+=value.size()+1;if(type==REG_SZ)break;}return result;
}
std::string digest(const std::string& text){
    BCRYPT_ALG_HANDLE algorithm=nullptr;need(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);
    struct A{BCRYPT_ALG_HANDLE h;~A(){BCryptCloseAlgorithmProvider(h,0);}}a{algorithm};std::array<unsigned char,32> hash{};
    need(BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),ULONG(text.size()),hash.data(),ULONG(hash.size()))>=0);
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto byte:hash)out<<std::setw(2)<<unsigned(byte);return out.str();
}
unsigned word(const std::wstring& id,const wchar_t* field){auto at=id.find(field);need(at!=id.npos&&at+8<=id.size());unsigned value=0;for(auto c:id.substr(at+4,4)){need((c>='0'&&c<='9')||(c>='A'&&c<='F'));value=value*16+(c<='9'?c-'0':c-'A'+10);}return value;}
}
std::vector<GenericDevice> generic_inventory(HostRunner& runner){
    std::vector<GenericDevice> result;
    for(const auto& host:runner.devices()){
        GenericDevice item;item.instance_id=upper(host.instance_id);item.bus_id=host.bus_id;
        // Stable identity is the physical PnP instance, never a transient bus address.
        item.id="usb-generic-"+digest(utf8(item.instance_id));item.label="USB device";
        GenericFacts facts;facts.owned=host.forced||host.attached||!host.binding_id.empty();
        try{
            need(!item.bus_id.empty());DEVINST root=0;need(CM_Locate_DevNodeW(&root,item.instance_id.data(),CM_LOCATE_DEVNODE_NORMAL)==CR_SUCCESS);
            auto names=prop(root,CM_DRP_FRIENDLYNAME);if(names.empty())names=prop(root,CM_DRP_DEVICEDESC,true);if(!names.empty())item.label=utf8(names.front());
            item.vendor=static_cast<std::uint16_t>(word(item.instance_id,L"VID_"));item.product=static_cast<std::uint16_t>(word(item.instance_id,L"PID_"));
            std::set<std::string> snapshotParts;std::vector<DEVINST> queue{root};std::set<std::wstring> seen;
            for(std::size_t i=0;i<queue.size();++i){
                need(queue.size()<=128);const auto node=queue[i];const auto id=node_id(node);need(seen.insert(id).second);snapshotParts.insert(utf8(id));
                ULONG status=0,problem=0;need(CM_Get_DevNode_Status(&status,&problem,node,0)==CR_SUCCESS&&!(status&DN_HAS_PROBLEM));
                bool usbFunction=id.starts_with(L"USB\\");bool functionKnown=false,composite=false;
                auto services=prop(node,CM_DRP_SERVICE);for(auto service:services){service=upper(service);snapshotParts.insert(utf8(id)+":"+utf8(service));
                    if(service==L"USBSTOR"||service==L"UASPSTOR"||service==L"DISK"||service==L"PARTMGR")facts.storage=true;
                    if(service==L"USBHUB"||service==L"USBHUB3")facts.hub=true;
                    if(service==L"USBXHCI"||service==L"USBEHCI"||service==L"USBOHCI"||service==L"USBUHCI")facts.controller=true;
                    composite|=service==L"USBCCGP";
                }
                for(auto compatible:prop(node,CM_DRP_COMPATIBLEIDS)){compatible=upper(compatible);snapshotParts.insert(utf8(id)+":"+utf8(compatible));
                    if(compatible.starts_with(L"USB\\CLASS_08")){facts.storage=true;functionKnown=true;}
                    else if(compatible.starts_with(L"USB\\CLASS_09")){facts.hub=true;functionKnown=true;}
                    else if(compatible.starts_with(L"USB\\CLASS_03")){facts.hid=true;functionKnown=true;}
                    else if(compatible.starts_with(L"USB\\CLASS_01")){facts.audio=true;functionKnown=true;}
                    else if(compatible==L"USB\\COMPOSITE")composite=true;
                }
                if(composite)facts.composite=true;
                if(usbFunction&&!functionKnown&&!composite)facts.unknown=true;
                DEVINST child=0;auto childResult=CM_Get_Child(&child,node,0);if(childResult==CR_SUCCESS){for(;;){queue.push_back(child);need(queue.size()<=128);DEVINST sibling=0;const auto next=CM_Get_Sibling(&sibling,child,0);if(next==CR_NO_SUCH_DEVNODE)break;need(next==CR_SUCCESS);child=sibling;}}
                else need(childResult==CR_NO_SUCH_DEVNODE);
                if(composite&&queue.size()==i+1)facts.unknown=true;
            }
            std::string snapshot;for(const auto& part:snapshotParts)snapshot+=std::to_string(part.size())+":"+part;facts.complete=true;item.fingerprint=digest(snapshot);const auto eligible=generic_eligibility(facts);item.available=eligible.available;item.reason=eligible.reason;
        }catch(const std::exception& e){item.reason=e.what();}
        for(auto& c:item.label)if(static_cast<unsigned char>(c)<32)c=' ';if(item.label.size()>120)item.label.resize(120);
        result.push_back(std::move(item));
    }
    return result;
}
}
