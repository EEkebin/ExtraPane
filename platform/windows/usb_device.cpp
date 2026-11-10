#include <dm/usb_device.hpp>
#include <algorithm>
#include <cctype>
#include <cwctype>
namespace dm::usb {
namespace {
bool same(const std::wstring& a,const std::wstring& b){return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](auto x,auto y){return towupper(x)==towupper(y);});}
std::string lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
std::string trim(std::string s){
    for(auto& c:s)if(static_cast<unsigned char>(c)<32)c=' ';
    const auto first=s.find_first_not_of(' ');if(first==std::string::npos)return {};
    return s.substr(first,s.find_last_not_of(' ')-first+1);
}
bool has(const std::string& text,const char* part){return text.find(part)!=std::string::npos;}
// Windows' driver-supplied names that say nothing about which physical device it is.
bool generic_name(const std::string& name){
    const auto n=lower(name);
    return n.empty()||n=="usb composite device"||n=="usb input device"||n=="usb device"||n=="usb receiver"||n=="hid-compliant device"||n=="unknown usb device";
}
// Manufacturer strings from class INFs rather than from the device itself.
bool generic_vendor(const std::string& vendor){
    const auto v=lower(vendor);
    return v.empty()||v.front()=='('||has(v,"standard")||has(v,"generic")||has(v,"compatible")||v=="microsoft";
}
}
std::string usb_function_name(const std::string& device_class,const std::string& description){
    const auto c=lower(device_class),d=lower(description);
    if(c=="keyboard")return "Keyboard";
    if(c=="mouse")return "Mouse";
    if(c=="hidclass"){
        if(has(d,"game controller")||has(d,"gamepad")||has(d,"joystick")||has(d,"xbox"))return "Game controller";
        if(has(d,"pen"))return "Pen";
        if(has(d,"touch screen")||has(d,"touchscreen"))return "Touch screen";
        if(has(d,"touch pad")||has(d,"touchpad"))return "Touchpad";
        return {};// vendor-defined/consumer-control collections are noise
    }
    if(c=="xnacomposite"||c=="xboxcomposite")return "Game controller";
    if(c=="media"||c=="audioendpoint")return "Audio";
    if(c=="camera"||c=="image")return "Camera";
    if(c=="diskdrive")return "Storage";
    if(c=="net")return "Network";
    if(c=="ports")return "Serial port";
    if(c=="bluetooth")return "Bluetooth";
    if(c=="printer")return "Printer";
    if(c=="smartcardreader")return "Smart card reader";
    if(c=="biometric")return "Fingerprint reader";
    if(c=="wpd")return "Portable device";
    return {};
}
std::string generic_label(const std::string& product,const std::string& manufacturer,const std::string& driver_name,const std::vector<std::string>& functions,std::uint16_t vendor,std::uint16_t product_id){
    auto reported=trim(product),vendor_name=trim(manufacturer),driver=trim(driver_name);
    std::string base=!reported.empty()?reported:!driver.empty()?driver:"USB device";
    if(!generic_vendor(vendor_name)&&!has(lower(base),lower(vendor_name).c_str()))base=vendor_name+" "+base;
    std::vector<std::string> unique;
    for(const auto& f:functions)if(!f.empty()&&std::find(unique.begin(),unique.end(),f)==unique.end())unique.push_back(f);
    std::string label=base;
    if(!unique.empty()){label+=" \xC2\xB7 ";for(std::size_t i=0;i<unique.size();++i){if(i)label+=", ";label+=unique[i];}}
    if(generic_name(base)&&(vendor||product_id)){
        static const char* hex="0123456789ABCDEF";std::string id;
        for(auto v:{vendor,product_id}){if(!id.empty())id+=':';for(int s=12;s>=0;s-=4)id+=hex[(v>>s)&0xF];}
        label+=" ("+id+")";
    }
    return label;
}
extrapane::Availability generic_eligibility(const GenericFacts& facts){
    if(!facts.complete)return {false,"USB device inventory is incomplete"};
    if(facts.hub||facts.controller)return {false,"USB hubs and controllers cannot be shared"};
    if(facts.composite&&!facts.descriptors_verified)return {false,"USB configuration descriptors could not be verified"};
    if(facts.storage)return {false,"Storage-containing devices require the storage sharing path"};
    if(facts.unknown||(!facts.hid&&!facts.audio))return {false,"USB device contains an unsupported function"};
    if(facts.owned)return {false,"USB device is already bound or attached elsewhere"};
    return {true,{}};
}
void disambiguate_labels(std::vector<GenericDevice>& devices){
    std::vector<std::string> original;for(const auto& device:devices)original.push_back(device.label);
    for(auto& device:devices){
        if(device.bus_id.empty())continue;
        if(std::count(original.begin(),original.end(),device.label)>1)device.label+=" \xC2\xB7 port "+device.bus_id;
    }
}
GenericExport::GenericExport(std::unique_ptr<HostRunner> runner,Inspect inspect):runner_(std::move(runner)),inspect_(std::move(inspect)){}
GenericExport::~GenericExport(){stop();}
extrapane::Availability GenericExport::start(const GenericDevice& selected,std::uint16_t port){
    if(reserved_||!runner_||!inspect_||!port||!selected.available||selected.id.empty()||selected.instance_id.empty()||selected.fingerprint.empty())
        return {false,"Select an available nonstorage USB device"};
    auto fail=[&](extrapane::Availability reason){const auto cleanup=stop();if(!cleanup.available)reason.reason+="; "+cleanup.reason;return reason;};
    try{
        auto result=runner_->ready();if(!result.available)return result;
        result=runner_->reserve(selected.instance_id);if(!result.available)return result;reserved_=true;
        const auto current=inspect_();const GenericDevice* found=nullptr;
        for(const auto& item:current)if(item.id==selected.id){if(found)return fail({false,"USB source identity is ambiguous"});found=&item;}
        if(!found||!found->available||!same(found->instance_id,selected.instance_id)||found->fingerprint!=selected.fingerprint||found->bus_id!=selected.bus_id)
            return fail({false,"USB source changed before export"});
        bus_=found->bus_id;result=runner_->bind_generic(bus_);bound_=true;if(!result.available)return fail(result);
        result=runner_->start(selected.instance_id,bus_,port);if(!result.available)return fail(result);
        if(!runner_->running())return fail({false,"USB exporter stopped before admission"});
        return {true,{}};
    }catch(const std::exception& e){return fail({false,e.what()});}
}
extrapane::Availability GenericExport::stop()noexcept{
    if(!runner_||!reserved_)return {true,{}};
    extrapane::Availability result{true,{}};
    try{runner_->stop();if(bound_)result=runner_->release_generic();}catch(const std::exception& e){result={false,e.what()};}catch(...){result={false,"Generic USB release failed"};}
    runner_->release();reserved_=bound_=false;bus_.clear();return result;
}
}
