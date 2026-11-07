#include "../apps/usb-setup/policy.hpp"
#include <stdexcept>
int main(){using namespace extrapane::usb_setup;
if(!allowed(L"usbip.exe")||!allowed(L"libusbip.dll")||!allowed(L"resources.dll")||allowed(L"../usbip.exe")||allowed(L"devnode.exe")||allowed(L"USBIP.exe"))throw std::runtime_error("allowlist");
if(!hash(digest)||hash(L"no")||hash(std::wstring_view(L"g0000000000000000000000000000000000000000000000000000000000000000")))throw std::runtime_error("digest");
if(!compatible(0,9,8,1)||compatible(0,9,8,0)||compatible(1,0,0,0))throw std::runtime_error("version");
if(install_backend(true,false)||install_backend(true,true)||!install_backend(false,false)||install_backend(false,true))throw std::runtime_error("preserve shared backend");
}
