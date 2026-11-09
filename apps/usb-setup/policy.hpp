#pragma once
#include <array>
#include <string_view>
namespace extrapane::usb_setup {
inline constexpr std::wstring_view digest=L"38cad6d4432b52d5bb9409d9ad03b72fdffc4ada4cd3a48fbeca1a2752a8518a";
inline constexpr std::array<std::wstring_view,3> files{L"usbip.exe",L"libusbip.dll",L"resources.dll"};
inline constexpr std::array<std::wstring_view,3> payload_hashes{
 L"817da219fa153b4b37c3bc1aa8fcc63f33e046188c57cc3dd2c60a6ef0883c56",
 L"f123ef5d288b817a890cbe39a3c2d212b3d8b1765c691c8703ccb76c7337986c",
 L"4bfc862e5891d9b4c12a8ebd43d3ba871ebf94b65f7a00c22527c905949e0fff"};
constexpr bool trusted_payload(std::wstring_view name,std::wstring_view hash){for(std::size_t i=0;i<files.size();++i)if(files[i]==name)return payload_hashes[i]==hash;return false;}
constexpr bool allowed(std::wstring_view name){for(auto file:files)if(file==name)return true;return false;}
constexpr bool hash(std::wstring_view value){if(value.size()!=64)return false;for(auto c:value)if(!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')))return false;return true;}
constexpr bool compatible(unsigned a,unsigned b,unsigned c,unsigned d){return a==0&&b==9&&c==8&&d==1;}
constexpr bool install_backend(bool any_footprint,bool exact_compatible){return !any_footprint&&!exact_compatible;}
}
