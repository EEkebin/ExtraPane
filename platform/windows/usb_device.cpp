#include <dm/usb_device.hpp>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <string_view>
namespace dm::usb {
namespace {
bool same(const std::wstring& a,const std::wstring& b){return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](auto x,auto y){return towupper(x)==towupper(y);});}
std::string lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
std::string trim(std::string s){
    for(auto& c:s)if(static_cast<unsigned char>(c)<32||c==127)c=' ';
    const auto first=s.find_first_not_of(' ');if(first==std::string::npos)return {};
    return s.substr(first,s.find_last_not_of(' ')-first+1);
}
bool has(const std::string& text,const char* part){return text.find(part)!=std::string::npos;}
bool alnum(char c){return std::isalnum(static_cast<unsigned char>(c))!=0;}
// Whole-word match so "pen" does not match "open" or "dependent".
bool word(const std::string& text,const std::string& part){
    for(auto at=text.find(part);at!=std::string::npos;at=text.find(part,at+1)){
        const bool before=at==0||!alnum(text[at-1]),after=at+part.size()>=text.size()||!alnum(text[at+part.size()]);
        if(before&&after)return true;
    }
    return false;
}
// Cuts to at most `limit` bytes without splitting a UTF-8 sequence.
std::string cap(std::string text,std::size_t limit){
    if(text.size()<=limit)return text;
    std::size_t cut=limit;while(cut&&(static_cast<unsigned char>(text[cut])&0xC0)==0x80)--cut;
    text.resize(cut);
    while(!text.empty()&&text.back()==' ')text.pop_back();
    return text;
}
// Windows' driver-supplied names that say nothing about which physical device it is.
bool generic_name(const std::string& name){
    const auto n=lower(name);
    return n.empty()||n=="usb composite device"||n=="usb input device"||n=="usb device"||n=="usb receiver"||n=="hid-compliant device"||n=="unknown usb device";
}
// INF manufacturer placeholders that do not identify a vendor.
bool generic_vendor(const std::string& vendor){
    const auto v=lower(vendor);
    return v.empty()||v.front()=='('||has(v,"standard")||has(v,"generic")||has(v,"compatible")||v=="microsoft";
}
// "Logitech, Inc." -> "Logitech"; "Razer Inc" -> "Razer".
std::string vendor_brand(std::string vendor){
    static const char* suffixes[]={" corporation"," corp"," incorporated"," inc"," co., ltd"," co.,ltd"," co ltd"," ltd"," limited"," llc"," gmbh"," ag"," s.a"," b.v"};
    for(bool changed=true;changed;){
        changed=false;
        while(!vendor.empty()&&(vendor.back()=='.'||vendor.back()==','||vendor.back()==' ')){vendor.pop_back();changed=true;}
        const auto l=lower(vendor);
        for(const auto* suffix:suffixes){const std::string x=suffix;if(l.size()>x.size()&&l.ends_with(x)){vendor.resize(vendor.size()-x.size());changed=true;break;}}
    }
    return trim(vendor);
}
}
std::optional<std::uint16_t> usb_id_word(const std::wstring& instance,const wchar_t* key){
    std::wstring upper=instance;for(auto& c:upper)c=wchar_t(towupper(c));
    const auto at=upper.find(key);const auto length=std::wstring_view(key).size();
    if(at==std::wstring::npos||at+length+4>upper.size())return std::nullopt;
    unsigned value=0;
    for(auto c:upper.substr(at+length,4)){
        if(!((c>=L'0'&&c<=L'9')||(c>=L'A'&&c<=L'F')))return std::nullopt;
        value=value*16+unsigned(c<=L'9'?c-L'0':c-L'A'+10);
    }
    return std::uint16_t(value);
}
std::string usb_function_name(const std::string& device_class,const std::string& description){
    const auto c=lower(device_class),d=lower(description);
    if(c=="keyboard")return "Keyboard";
    if(c=="mouse")return "Mouse";
    if(c=="hidclass"){
        if(has(d,"game controller")||word(d,"gamepad")||word(d,"joystick")||word(d,"xbox"))return "Game controller";
        if(word(d,"pen")||word(d,"stylus")||word(d,"digitizer"))return "Pen";
        if(has(d,"touch screen")||word(d,"touchscreen"))return "Touch screen";
        if(has(d,"touch pad")||word(d,"touchpad"))return "Touchpad";
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
    const auto reported=trim(product),driver=trim(driver_name);
    const std::string name=!reported.empty()?reported:!driver.empty()?driver:"USB device";
    // Judge genericness on the device/driver name itself, before any vendor prefix.
    const bool generic=generic_name(name);
    std::string base=name;
    const auto vendor_text=trim(manufacturer);
    const auto brand=generic_vendor(vendor_text)?std::string{}:vendor_brand(vendor_text);
    if(!brand.empty()){
        const auto first=lower(brand.substr(0,brand.find(' ')));
        if(!word(lower(name),first))base=brand+" "+name;
    }
    std::vector<std::string> unique;
    for(const auto& f:functions)if(!f.empty()&&std::find(unique.begin(),unique.end(),f)==unique.end())unique.push_back(f);
    std::string tail;
    if(!unique.empty()){tail+=" \xC2\xB7 ";for(std::size_t i=0;i<unique.size();++i){if(i)tail+=", ";tail+=unique[i];}}
    if(generic&&(vendor||product_id)){
        static const char* hex="0123456789ABCDEF";std::string id;
        for(auto v:{vendor,product_id}){if(!id.empty())id+=':';for(int x=12;x>=0;x-=4)id+=hex[(v>>x)&0xF];}
        tail+=" ("+id+")";
    }
    // Keep identifying functions/VID:PID; shorten the name first if needed.
    tail=cap(tail,usb_label_limit/2);
    return cap(base,usb_label_limit-tail.size())+tail;
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
void disambiguate_labels(std::vector<GenericDevice>& devices,const std::vector<std::string>& other_labels){
    auto key=[](const GenericDevice& device)->const std::string&{return device.name.empty()?device.label:device.name;};
    std::vector<std::string> names;for(const auto& device:devices)names.push_back(key(device));
    for(auto& device:devices){
        if(device.bus_id.empty())continue;
        const auto& mine=key(device);
        const auto twins=std::count(names.begin(),names.end(),mine)+std::count(other_labels.begin(),other_labels.end(),mine);
        if(twins<2)continue;
        const auto suffix=" \xC2\xB7 port "+cap(device.bus_id,32);
        device.label=cap(device.label,usb_label_limit-suffix.size())+suffix;
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
