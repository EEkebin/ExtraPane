#include <dm/usb_device.hpp>
#include <algorithm>
#include <cwctype>
namespace dm::usb {
namespace {
bool same(const std::wstring& a,const std::wstring& b){return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](auto x,auto y){return towupper(x)==towupper(y);});}
}
extrapane::Availability generic_eligibility(const GenericFacts& facts){
    if(!facts.complete)return {false,"USB device inventory is incomplete"};
    if(facts.hub||facts.controller)return {false,"USB hubs and controllers cannot be shared"};
    if(facts.composite)return {false,"Composite USB devices require complete configuration-descriptor validation"};
    if(facts.storage)return {false,"Storage-containing devices require the storage sharing path"};
    if(facts.unknown||(!facts.hid&&!facts.audio))return {false,"USB device contains an unsupported function"};
    if(facts.owned)return {false,"USB device is already bound or attached elsewhere"};
    return {true,{}};
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
