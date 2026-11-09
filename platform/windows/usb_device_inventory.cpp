#include <dm/usb_device.hpp>
#include <dm/usb_descriptors.hpp>
#include <windows.h>
#include <cfgmgr32.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <sstream>
#include <iomanip>
namespace dm::usb { namespace {
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::wstring upper(std::wstring s){for(auto& c:s)c=towupper(c);return s;}
std::string utf8(const std::wstring& value){if(value.empty())return {};auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);need(n>0,"USB device name is invalid");std::string result(n,0);need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),result.data(),n,nullptr,nullptr)==n,"USB device name is invalid");return result;}
std::vector<std::wstring> prop(DEVINST node,ULONG which){
    ULONG type=0,size=0;const auto first=CM_Get_DevNode_Registry_PropertyW(node,which,&type,nullptr,&size,0);
    if(first==CR_NO_SUCH_VALUE)return {};need(first==CR_BUFFER_SMALL&&size>=2&&size<=65536&&size%2==0,"Cannot read USB device properties");
    std::vector<wchar_t> data(size/2+2,0);need(CM_Get_DevNode_Registry_PropertyW(node,which,&type,data.data(),&size,0)==CR_SUCCESS&&(type==REG_SZ||type==REG_MULTI_SZ),"USB device properties changed");
    std::vector<std::wstring> result;for(std::size_t i=0;i<data.size()&&data[i];){std::wstring value(data.data()+i);need(i+value.size()<data.size(),"USB device property is truncated");result.push_back(value);i+=value.size()+1;if(type==REG_SZ)break;}return result;
}
std::string digest(const std::string& text){
    BCRYPT_ALG_HANDLE algorithm=nullptr;need(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Cannot fingerprint USB descriptors");
    struct A{BCRYPT_ALG_HANDLE h;~A(){BCryptCloseAlgorithmProvider(h,0);}}a{algorithm};std::array<unsigned char,32> hash{};
    need(BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),ULONG(text.size()),hash.data(),ULONG(hash.size()))>=0,"Cannot fingerprint USB descriptors");
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto byte:hash)out<<std::setw(2)<<unsigned(byte);return out.str();
}
}
std::vector<GenericDevice> generic_inventory(HostRunner& runner){
    std::vector<GenericDevice> result;
    for(const auto& host:runner.devices()){
        GenericDevice item;item.instance_id=upper(host.instance_id);item.bus_id=host.bus_id;
        item.id="usb-generic-"+digest(utf8(item.instance_id));item.label="USB device";
        GenericFacts facts;facts.owned=host.forced||host.attached||!host.binding_id.empty();
        try{
            DEVINST root=0;need(CM_Locate_DevNodeW(&root,item.instance_id.data(),CM_LOCATE_DEVNODE_NORMAL)==CR_SUCCESS,"USB device is no longer present");
            auto names=prop(root,CM_DRP_FRIENDLYNAME);if(names.empty())names=prop(root,CM_DRP_DEVICEDESC);if(!names.empty())item.label=utf8(names.front());
            bool bluetooth=false;for(auto service:prop(root,CM_DRP_SERVICE))bluetooth|=upper(service)==L"BTHUSB";
            for(auto compatible:prop(root,CM_DRP_COMPATIBLEIDS))bluetooth|=upper(compatible).starts_with(L"USB\\CLASS_E0");
            need(!bluetooth,"Bluetooth USB adapters are not supported for forwarding");
            need(!item.bus_id.empty(),"USB device has no available bus address");
            ULONG status=0,problem=0;need(CM_Get_DevNode_Status(&status,&problem,root,0)==CR_SUCCESS,"USB device was removed during inspection");
            need(!(status&DN_HAS_PROBLEM),"Windows reports a problem with this USB device or its driver");
            need(!facts.owned,"USB device is already bound or attached elsewhere");
            const auto descriptors=read_device_descriptors(item.instance_id);
            item.vendor=std::uint16_t(descriptors.device[8]|(unsigned(descriptors.device[9])<<8));item.product=std::uint16_t(descriptors.device[10]|(unsigned(descriptors.device[11])<<8));
            facts.hid=descriptors.classification.hid;facts.audio=descriptors.classification.audio;facts.composite=descriptors.classification.composite;facts.descriptors_verified=true;facts.complete=true;
            std::string snapshot= utf8(item.instance_id);auto append=[&](const auto& blob){snapshot+=std::to_string(blob.size())+":";snapshot.append(reinterpret_cast<const char*>(blob.data()),blob.size());};append(descriptors.device);for(const auto& config:descriptors.configurations)append(config);
            item.fingerprint=digest(snapshot);const auto eligible=generic_eligibility(facts);item.available=eligible.available;item.reason=eligible.reason;
        }catch(const std::exception& e){item.reason=e.what();}
        for(auto& c:item.label)if(static_cast<unsigned char>(c)<32)c=' ';if(item.label.size()>120)item.label.resize(120);
        result.push_back(std::move(item));
    }
    return result;
}
}
