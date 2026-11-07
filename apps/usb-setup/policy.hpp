#pragma once
#include <array>
#include <string_view>
namespace extrapane::usb_setup {
inline constexpr std::wstring_view digest=L"38cad6d4432b52d5bb9409d9ad03b72fdffc4ada4cd3a48fbeca1a2752a8518a";
inline constexpr std::array<std::wstring_view,3> files{L"usbip.exe",L"libusbip.dll",L"resources.dll"};
constexpr bool allowed(std::wstring_view name){for(auto file:files)if(file==name)return true;return false;}
constexpr bool hash(std::wstring_view value){if(value.size()!=64)return false;for(auto c:value)if(!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')))return false;return true;}
constexpr bool compatible(unsigned a,unsigned b,unsigned c,unsigned d){return a==0&&b==9&&c==8&&d==1;}
constexpr bool install_backend(bool any_footprint,bool exact_compatible){return !any_footprint&&!exact_compatible;}
}
