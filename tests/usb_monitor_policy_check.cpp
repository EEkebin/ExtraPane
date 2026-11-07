#include "../apps/usb-host-setup/monitor_policy.hpp"
#include <stdexcept>
int main(){using namespace extrapane::usb_monitor;
if(!reviewed(legacy_7_2_2,L"bundle")||!reviewed(L"bundle",L"bundle")||reviewed(L"unknown",L"bundle"))throw std::runtime_error("only reviewed shared files");
if(!compatible(5,0,8)||!compatible(5,1,8)||compatible(4,99,8)||compatible(6,0,8)||compatible(5,0,7)||compatible(5,0,9))throw std::runtime_error("exact version reply and supported ABI");
}
