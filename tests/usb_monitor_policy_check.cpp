#include "../apps/usb-host-setup/monitor_policy.hpp"
#include "../apps/usb-host-setup/uninstall_policy.hpp"
#include <stdexcept>
int main(){using namespace extrapane::usb_monitor;
if(!reviewed(legacy_7_2_2,L"bundle")||!reviewed(L"bundle",L"bundle")||reviewed(L"unknown",L"bundle"))throw std::runtime_error("only reviewed shared files");
if(!compatible(5,0,8)||!compatible(5,1,8)||compatible(4,99,8)||compatible(6,0,8)||compatible(5,0,7)||compatible(5,0,9))throw std::runtime_error("exact version reply and supported ABI");
using namespace extrapane::usb_uninstall;
for(unsigned bits=0;bits<32;++bits){
    const bool exists=bits&1,created=bits&2,managed=bits&4,hash=bits&8,shared=bits&16;
    const auto result=monitor_action(exists,created,managed,hash,shared);
    if(result==MonitorAction::remove&&!(exists&&created&&managed&&hash&&!shared))throw std::runtime_error("never remove unknown/shared monitors");
    if(exists&&!created&&!managed&&result!=MonitorAction::preserve)throw std::runtime_error("preserve external monitors");
    if(!exists&&result!=MonitorAction::preserve)throw std::runtime_error("retry after service removal");
}
if(monitor_action(true,true,true,true,false)!=MonitorAction::remove)throw std::runtime_error("remove verified owned monitor");
for(auto name:{L"oem0.inf",L"oem123.inf"})if(!published_inf(name))throw std::runtime_error("valid published INF");
for(auto name:{L"oem.inf",L"VBoxUSB.inf",L"oem1.inf.old",L"../oem1.inf",L"oem-1.inf",L"oem1/2.inf"})if(published_inf(name))throw std::runtime_error("reject non-published INF identity");
}
